/*
 * rk_web_api.c
 *
 * RK3566 泰山派多线程轻量级 REST API Server
 *
 * 功能：
 *  1. 监听 8080 端口
 *  2. 支持 PC 通过 curl / 浏览器访问设备状态
 *  3. 提供 REST API：
 *      GET /
 *      GET /api/status
 *      GET /api/version
 *      GET /api/ota/status
 *      GET /api/vision/status
 *      GET /api/logs
 *      GET /video
 *      GET /hls/stream.m3u8
 *
 * 多线程设计：
 *  - 主线程负责 accept()
 *  - 每来一个客户端连接，就创建一个 pthread 工作线程
 *  - 工作线程处理 HTTP 请求并关闭连接
 *  - 设置 MAX_CLIENTS，避免无限创建线程导致系统资源耗尽
 *
 * 编译：
 *  aarch64-buildroot-linux-gnu-gcc -O2 -Wall -pthread rk_web_api.c -o rk_web_api
 *
 * 注意：
 *  当前仍然是 Day3 工程闭环版本，不是完整工业级 HTTP Server。
 *  后续可继续升级为线程池、select/epoll、HTTPS、鉴权等。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <pthread.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

/* 默认监听端口 */
#define DEFAULT_PORT 8080

/* 接收 HTTP 请求的缓冲区大小 */
#define BUF_SIZE 4096

/*
 * 最大并发客户端数量。
 *
 * 嵌入式设备资源有限，不能无限创建线程。
 * 这里限制最多同时处理 16 个客户端请求。
 */
#define MAX_CLIENTS 16

/*
 * 每个客户端线程的参数。
 *
 * client_fd：
 *  当前客户端 socket fd。
 */
typedef struct {
    int client_fd;
} client_arg_t;

/*
 * 全局并发连接计数。
 *
 * active_clients：
 *  当前正在处理的客户端数量。
 *
 * active_clients_mutex：
 *  多线程下保护 active_clients，避免并发读写出错。
 */
static int active_clients = 0;
static pthread_mutex_t active_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * log_mutex：
 *  防止多个线程同时 printf，导致日志输出混在一起。
 */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * thread_log()
 *
 * 作用：
 *  多线程安全地打印日志。
 */
static void thread_log(const char *fmt, ...)
{
    va_list args;

    pthread_mutex_lock(&log_mutex);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);

    pthread_mutex_unlock(&log_mutex);
}

/*
 * send_bytes()
 *
 * 作用：
 *  按指定长度向客户端发送二进制数据。
 *
 * 为什么需要它？
 *  原来的 send_all() 适合发送字符串，因为它使用 strlen()。
 *  但 HLS 的 .ts 分片是二进制文件，内容中可能包含 '\0'。
 *  如果继续用 strlen()，会导致视频分片发送不完整。
 */
static int send_bytes(int client, const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t total = 0;
    ssize_t sent;

    if (data == NULL) {
        return -1;
    }

    while (total < len) {
        sent = send(client, p + total, len - total, 0);

        if (sent <= 0) {
            return -1;
        }

        total += sent;
    }

    return 0;
}

/*
 * send_all()
 *
 * 作用：
 *  尽量完整地向客户端发送字符串。
 *
 * 为什么不用一次 send()？
 *  因为 TCP send() 可能只发送部分数据。
 *  所以这里循环发送，直到全部发完或连接出错。
 */
static void send_all(int client, const char *data)
{
    if (data == NULL) {
        return;
    }

    send_bytes(client, data, strlen(data));
}

/*
 * send_format()
 *
 * 作用：
 *  类似 printf，把格式化后的内容发送给客户端。
 *
 * 这样就不用频繁写 dprintf(client, ...)
 * 也方便统一走 send_all()。
 */
static void send_format(int client, const char *fmt, ...)
{
    char buf[BUF_SIZE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    send_all(client, buf);
}

/*
 * send_http_header()
 *
 * 作用：
 *  发送 HTTP 200 响应头。
 *
 * Content-Type：
 *  - application/json：返回 JSON
 *  - text/plain：返回普通文本
 *
 * Connection: close：
 *  表示本次响应结束后关闭连接。
 *
 * Access-Control-Allow-Origin: *：
 *  允许浏览器跨域访问，后续做前端页面时会方便。
 */
static void send_http_header(int client, const char *content_type)
{
    send_all(client, "HTTP/1.1 200 OK\r\n");
    send_all(client, "Server: rk3566-web-api\r\n");
    send_all(client, "Connection: close\r\n");
    send_all(client, "Access-Control-Allow-Origin: *\r\n");
    send_all(client, "Content-Type: ");
    send_all(client, content_type);
    send_all(client, "\r\n\r\n");
}

/*
 * send_404()
 *
 * 作用：
 *  API 不存在时返回 404。
 */
static void send_404(int client)
{
    send_all(client, "HTTP/1.1 404 Not Found\r\n");
    send_all(client, "Server: rk3566-web-api\r\n");
    send_all(client, "Connection: close\r\n");
    send_all(client, "Content-Type: application/json\r\n\r\n");
    send_all(client, "{ \"error\": \"api not found\" }\n");
}

/*
 * send_405()
 *
 * 作用：
 *  当前只支持 GET。
 *  如果用户用 POST / PUT / DELETE，返回 405。
 */
static void send_405(int client)
{
    send_all(client, "HTTP/1.1 405 Method Not Allowed\r\n");
    send_all(client, "Server: rk3566-web-api\r\n");
    send_all(client, "Connection: close\r\n");
    send_all(client, "Content-Type: application/json\r\n\r\n");
    send_all(client, "{ \"error\": \"method not allowed, only GET is supported\" }\n");
}

/*
 * send_503()
 *
 * 作用：
 *  当前并发连接数超过 MAX_CLIENTS 时返回 503。
 *
 * 为什么需要这个？
 *  嵌入式设备资源有限，如果每个请求都创建线程，
 *  很容易因为大量请求导致内存或调度资源耗尽。
 */
static void send_503(int client)
{
    send_all(client, "HTTP/1.1 503 Service Unavailable\r\n");
    send_all(client, "Server: rk3566-web-api\r\n");
    send_all(client, "Connection: close\r\n");
    send_all(client, "Content-Type: application/json\r\n\r\n");
    send_all(client, "{ \"error\": \"too many clients, try again later\" }\n");
}

/*
 * send_command_output_body()
 *
 * 作用：
 *  执行固定 shell 命令，并把输出返回给客户端。
 *
 * 当前用于：
 *  - /api/status 调用 /usr/bin/device_status.sh
 *  - /api/logs 调用 tail 查看日志
 *
 * 安全注意：
 *  popen() 会调用 shell。
 *  不要把 HTTP 请求里的用户输入拼进命令。
 *  这里命令都是程序内部固定字符串，因此风险较低。
 */
static void send_command_output_body(int client, const char *cmd)
{
    char buf[BUF_SIZE];
    FILE *fp;

    fp = popen(cmd, "r");

    if (fp == NULL) {
        send_all(client, "{ \"error\": \"popen failed\" }\n");
        return;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        send_all(client, buf);
    }

    pclose(fp);
}

/*
 * send_command_response()
 *
 * 作用：
 *  先发送 HTTP 头，再发送命令输出。
 */
static void send_command_response(int client, const char *cmd, const char *content_type)
{
    send_http_header(client, content_type);
    send_command_output_body(client, cmd);
}

/*
 * get_content_type()
 *
 * 作用：
 *  根据文件扩展名返回 HTTP Content-Type。
 *
 * 当前主要用于 HLS：
 *  - .m3u8 播放列表
 *  - .ts   视频分片
 */
static const char *get_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (ext == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".m3u8") == 0) {
        return "application/vnd.apple.mpegurl";
    }

    if (strcmp(ext, ".ts") == 0) {
        return "video/MP2T";
    }

    if (strcmp(ext, ".html") == 0) {
        return "text/html; charset=utf-8";
    }

    if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    }

    if (strcmp(ext, ".css") == 0) {
        return "text/css";
    }

    return "application/octet-stream";
}

/*
 * serve_hls_file()
 *
 * 作用：
 *  把 URL /hls/... 映射到板端 /tmp/hls/...，
 *  从而让浏览器可以通过当前 Web Server 访问 HLS 视频流。
 *
 * 例如：
 *  GET /hls/stream.m3u8
 *      -> /tmp/hls/stream.m3u8
 *
 *  GET /hls/segment00001.ts
 *      -> /tmp/hls/segment00001.ts
 *
 * 安全限制：
 *  禁止路径中出现 ".."，防止目录穿越攻击。
 */
static void serve_hls_file(int client, const char *url_path)
{
    char file_path[512];
    char header[512];
    char buf[4096];

    struct stat st;
    int fd;

    if (strstr(url_path, "..") != NULL) {
        send_all(client,
            "HTTP/1.1 403 Forbidden\r\n"
            "Server: rk3566-web-api\r\n"
            "Connection: close\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "\r\n"
            "Forbidden"
        );
        return;
    }

    /*
     * URL 路径必须以 /hls/ 开头。
     */
    if (strncmp(url_path, "/hls/", 5) != 0) {
        send_404(client);
        return;
    }

    /*
     * 映射规则：
     *  /hls/xxx -> /tmp/hls/xxx
     */
    snprintf(file_path, sizeof(file_path), "/tmp%s", url_path);

    fd = open(file_path, O_RDONLY);

    if (fd < 0) {
        send_all(client,
            "HTTP/1.1 404 Not Found\r\n"
            "Server: rk3566-web-api\r\n"
            "Connection: close\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "\r\n"
            "Not Found"
        );
        return;
    }

    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);

        send_all(client,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Server: rk3566-web-api\r\n"
            "Connection: close\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 12\r\n"
            "\r\n"
            "Server Error"
        );
        return;
    }

    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: rk3566-web-api\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        get_content_type(file_path),
        (long)st.st_size
    );

    send_all(client, header);

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n <= 0) {
            break;
        }

        if (send_bytes(client, buf, (size_t)n) < 0) {
            break;
        }
    }

    close(fd);
}

/*
 * api_video_page()
 *
 * GET /video
 *
 * 返回一个最小网页：
 *  - 使用 hls.js 播放 /hls/stream.m3u8
 *  - 适合 Day4 演示“网页直接看摄像头画面”
 *
 * 注意：
 *  hls.js 来自 CDN，所以 PC 浏览器需要能访问互联网。
 *  如果没有互联网，可以后续把 hls.min.js 下载后放到板端本地提供。
 */
static void api_video_page(int client)
{
    const char *html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>RK3566 Vision Stream</title>\n"
        "  <script src=\"https://cdn.jsdelivr.net/npm/hls.js@latest\"></script>\n"
        "  <style>\n"
        "    body { background:#111; color:#eee; font-family:Arial, sans-serif; text-align:center; margin:0; }\n"
        "    header { padding:20px; background:#1d1d1d; }\n"
        "    h1 { margin:0; font-size:24px; }\n"
        "    video { width:90%; max-width:960px; margin-top:24px; background:#000; border:1px solid #333; }\n"
        "    .info { margin:18px auto; width:90%; max-width:960px; text-align:left; color:#ccc; font-size:14px; }\n"
        "    code { color:#8fd; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <header>\n"
        "    <h1>RK3566 Vision Stream</h1>\n"
        "  </header>\n"
        "\n"
        "  <video id=\"video\" controls autoplay muted playsinline></video>\n"
        "\n"
        "  <div class=\"info\">\n"
        "    <p>HLS URL: <code>/hls/stream.m3u8</code></p>\n"
        "    <p>Status API: <code>/api/vision/status</code></p>\n"
        "    <p id=\"status\">Loading...</p>\n"
        "  </div>\n"
        "\n"
        "  <script>\n"
        "    const video = document.getElementById('video');\n"
        "    const statusEl = document.getElementById('status');\n"
        "    const videoSrc = '/hls/stream.m3u8';\n"
        "\n"
        "    function setStatus(msg) {\n"
        "      statusEl.textContent = msg;\n"
        "    }\n"
        "\n"
        "    if (window.Hls && Hls.isSupported()) {\n"
        "      const hls = new Hls({ lowLatencyMode: true });\n"
        "      hls.loadSource(videoSrc);\n"
        "      hls.attachMedia(video);\n"
        "      hls.on(Hls.Events.MANIFEST_PARSED, function () {\n"
        "        setStatus('HLS manifest loaded. Playing video...');\n"
        "        video.play().catch(function () {});\n"
        "      });\n"
        "      hls.on(Hls.Events.ERROR, function (event, data) {\n"
        "        setStatus('HLS error: ' + data.type + ' / ' + data.details);\n"
        "      });\n"
        "    } else if (video.canPlayType('application/vnd.apple.mpegurl')) {\n"
        "      video.src = videoSrc;\n"
        "      video.addEventListener('loadedmetadata', function () {\n"
        "        setStatus('Native HLS loaded. Playing video...');\n"
        "        video.play().catch(function () {});\n"
        "      });\n"
        "    } else {\n"
        "      setStatus('This browser does not support HLS. Please use Chrome/Edge with hls.js available.');\n"
        "    }\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n";

    char header[256];

    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: rk3566-web-api\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        (long)strlen(html)
    );

    send_all(client, header);
    send_all(client, html);
}

/*
 * api_index()
 *
 * GET /
 *
 * 返回 API Server 的基本信息和接口列表。
 */
static void api_index(int client)
{
    send_http_header(client, "application/json");

    send_all(client,
        "{\n"
        "  \"name\": \"RK3566 Web API Server\",\n"
        "  \"version\": \"dev-day3-threaded\",\n"
        "  \"mode\": \"pthread-per-connection\",\n"
        "  \"max_clients\": 16,\n"
        "  \"endpoints\": [\n"
        "    \"/api/status\",\n"
        "    \"/api/version\",\n"
        "    \"/api/ota/status\",\n"
        "    \"/api/vision/status\",\n"
        "    \"/api/logs\"\n"
        "  ]\n"
        "}\n"
    );
}

/*
 * api_status()
 *
 * GET /api/status
 *
 * 返回设备总状态。
 *
 * 优先调用：
 *  /usr/bin/device_status.sh
 *
 * 这个脚本是 Day2 的 Device Manager 状态聚合脚本。
 */
static void api_status(int client)
{
    if (access("/usr/bin/device_status.sh", X_OK) == 0) {
        send_command_response(client, "/usr/bin/device_status.sh", "application/json");
        return;
    }

    if (access("./scripts/device_status.sh", X_OK) == 0) {
        send_command_response(client, "./scripts/device_status.sh", "application/json");
        return;
    }

    send_http_header(client, "application/json");

    send_all(client,
        "{\n"
        "  \"error\": \"device_status.sh not found\",\n"
        "  \"hint\": \"copy device_status.sh to /usr/bin/device_status.sh and chmod +x it\"\n"
        "}\n"
    );
}

/*
 * api_version()
 *
 * GET /api/version
 *
 * 返回当前固件版本。
 *
 * 版本文件：
 *  /etc/rk3566_vision_terminal/version
 */
static void api_version(int client)
{
    char version[128] = "dev-day3-threaded";
    FILE *fp;

    fp = fopen("/etc/rk3566_vision_terminal/version", "r");

    if (fp != NULL) {
        if (fgets(version, sizeof(version), fp) != NULL) {
            version[strcspn(version, "\r\n")] = '\0';
        }

        fclose(fp);
    }

    send_http_header(client, "application/json");

    send_format(client,
        "{\n"
        "  \"device\": \"RK3566-TaishanPi\",\n"
        "  \"version\": \"%s\",\n"
        "  \"build\": \"dev-day3-threaded\",\n"
        "  \"api_server\": \"rk_web_api\",\n"
        "  \"server_mode\": \"pthread-per-connection\"\n"
        "}\n",
        version
    );
}

/*
 * api_ota_status()
 *
 * GET /api/ota/status
 *
 * 返回 A/B RootFS OTA 状态。
 *
 * 当前通过 /proc/cmdline 判断：
 *  android_slotsufix=_a
 *  android_slotsufix=_b
 *
 * 你的 Rockchip 系统中目前就是 android_slotsufix 这个字段。
 */
static void api_ota_status(int client)
{
    char cmdline[2048] = {0};

    char current_slot[16] = "_unknown";
    char inactive_slot[16] = "_unknown";
    char rootfs[32] = "unknown";

    FILE *fp;

    fp = fopen("/proc/cmdline", "r");

    if (fp != NULL) {
        fgets(cmdline, sizeof(cmdline), fp);
        fclose(fp);
    }

    if (strstr(cmdline, "android_slotsufix=_a") != NULL) {
        strcpy(current_slot, "_a");
        strcpy(inactive_slot, "_b");
        strcpy(rootfs, "system_a");
    } else if (strstr(cmdline, "android_slotsufix=_b") != NULL) {
        strcpy(current_slot, "_b");
        strcpy(inactive_slot, "_a");
        strcpy(rootfs, "system_b");
    }

    send_http_header(client, "application/json");

    send_format(client,
        "{\n"
        "  \"ota\": {\n"
        "    \"current_slot\": \"%s\",\n"
        "    \"inactive_slot\": \"%s\",\n"
        "    \"rootfs\": \"%s\",\n"
        "    \"status\": \"idle\",\n"
        "    \"rollback_available\": true\n"
        "  }\n"
        "}\n",
        current_slot,
        inactive_slot,
        rootfs
    );
}

/*
 * process_exists()
 *
 * 作用：
 *  判断某个进程关键字是否存在。
 *
 * 当前用于判断视频服务是否运行：
 *  - gst-launch
 *  - rk_camera_preview
 *  - mpph264
 *
 * 注意：
 *  这里也是固定关键字，不从 HTTP 请求读取参数。
 */
static int process_exists(const char *keyword)
{
    char cmd[256];
    FILE *fp;
    char buf[128];

    snprintf(cmd, sizeof(cmd), "ps | grep '%s' | grep -v grep", keyword);

    fp = popen(cmd, "r");

    if (fp == NULL) {
        return 0;
    }

    if (fgets(buf, sizeof(buf), fp) != NULL) {
        pclose(fp);
        return 1;
    }

    pclose(fp);
    return 0;
}

/*
 * api_vision_status()
 *
 * GET /api/vision/status
 *
 * 返回摄像头和视频流状态。
 *
 * camera:
 *  如果 /dev/video1 或 /dev/video0 存在，认为 online。
 *
 * stream:
 *  如果相关视频进程存在，认为 running。
 */
static void api_vision_status(int client)
{
    int camera_online = 0;
    int stream_running = 0;

    if (access("/dev/video1", F_OK) == 0 ||
        access("/dev/video0", F_OK) == 0) {
        camera_online = 1;
    }

    if (process_exists("gst-launch") ||
        process_exists("rk_camera_preview") ||
        process_exists("mpph264")) {
        stream_running = 1;
    }

    send_http_header(client, "application/json");

    send_format(client,
        "{\n"
        "  \"vision\": {\n"
        "    \"camera\": \"%s\",\n"
        "    \"stream\": \"%s\",\n"
        "    \"video_device\": \"/dev/video1\"\n"
        "  }\n"
        "}\n",
        camera_online ? "online" : "offline",
        stream_running ? "running" : "stopped"
    );
}

/*
 * api_logs()
 *
 * GET /api/logs
 *
 * 返回最近日志。
 *
 * 当前返回 text/plain，而不是 JSON。
 */
static void api_logs(int client)
{
    send_http_header(client, "text/plain");

    send_command_output_body(
        client,
        "echo '===== device_manager.log ====='; "
        "tail -n 50 /tmp/device_manager.log 2>/dev/null || echo 'no /tmp/device_manager.log'; "
        "echo ''; "
        "echo '===== web_api.log ====='; "
        "tail -n 50 /tmp/web_api.log 2>/dev/null || echo 'no /tmp/web_api.log'; "
        "echo ''; "
        "echo '===== ota.log ====='; "
        "tail -n 50 /tmp/ota.log 2>/dev/null || echo 'no /tmp/ota.log'; "
        "echo ''; "
        "echo '===== vision_service.log ====='; "
        "tail -n 50 /tmp/vision_service.log 2>/dev/null || echo 'no /tmp/vision_service.log'"
    );
}

/*
 * handle_client()
 *
 * 作用：
 *  处理单个客户端请求。
 *
 * 多线程版中：
 *  每个线程都会调用这个函数。
 */
static void handle_client(int client)
{
    char req[BUF_SIZE];
    char method[16] = {0};
    char path[256] = {0};
    char *query;

    memset(req, 0, sizeof(req));

    if (recv(client, req, sizeof(req) - 1, 0) <= 0) {
        return;
    }

    /*
     * 解析 HTTP 请求第一行：
     *
     * GET /api/status HTTP/1.1
     *
     * 解析结果：
     *  method = GET
     *  path   = /api/status
     */
    sscanf(req, "%15s %255s", method, path);

    /*
     * 去掉 URL 查询参数。
     *
     * 例如：
     *  /api/status?x=1
     *
     * 处理后：
     *  /api/status
     */
    query = strchr(path, '?');

    if (query != NULL) {
        *query = '\0';
    }

    thread_log("[web_api] thread %lu request: %s %s\n",
               (unsigned long)pthread_self(), method, path);

    if (strcmp(method, "GET") != 0) {
        send_405(client);
        return;
    }

    /*
     * REST API / HLS 路由分发。
     */
    if (strcmp(path, "/video") == 0) {
        api_video_page(client);
    } else if (strncmp(path, "/hls/", 5) == 0) {
        serve_hls_file(client, path);
    } else if (strcmp(path, "/") == 0) {
        api_index(client);
    } else if (strcmp(path, "/api/status") == 0) {
        api_status(client);
    } else if (strcmp(path, "/api/version") == 0) {
        api_version(client);
    } else if (strcmp(path, "/api/ota/status") == 0) {
        api_ota_status(client);
    } else if (strcmp(path, "/api/vision/status") == 0) {
        api_vision_status(client);
    } else if (strcmp(path, "/api/logs") == 0) {
        api_logs(client);
    } else {
        send_404(client);
    }
}

/*
 * client_thread()
 *
 * 作用：
 *  每个客户端连接对应的工作线程函数。
 *
 * 流程：
 *  1. 取出 client_fd
 *  2. 释放参数内存
 *  3. 调用 handle_client()
 *  4. 关闭客户端 socket
 *  5. active_clients--
 */
static void *client_thread(void *arg)
{
    client_arg_t *client_arg = (client_arg_t *)arg;
    int client = client_arg->client_fd;

    free(client_arg);

    handle_client(client);

    close(client);

    pthread_mutex_lock(&active_clients_mutex);
    active_clients--;
    thread_log("[web_api] client disconnected, active_clients=%d\n", active_clients);
    pthread_mutex_unlock(&active_clients_mutex);

    return NULL;
}

/*
 * main()
 *
 * 程序入口。
 *
 * 使用方式：
 *  rk_web_api
 *  rk_web_api 8080
 */
int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    int server_fd;
    int client;
    int opt = 1;

    struct sockaddr_in addr;

    /*
     * 忽略 SIGPIPE。
     *
     * 如果客户端断开后服务器继续 send()，
     * 默认可能收到 SIGPIPE 并退出进程。
     * 嵌入式服务不应该因为一个客户端断开就崩掉。
     */
    signal(SIGPIPE, SIG_IGN);

    if (argc >= 2) {
        port = atoi(argv[1]);

        if (port <= 0) {
            port = DEFAULT_PORT;
        }
    }

    /*
     * 创建 TCP socket。
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    /*
     * 设置端口复用，方便服务重启。
     */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    /*
     * 绑定到所有网卡的指定端口。
     */
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /*
     * 开始监听。
     */
    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("[web_api] RK3566 threaded Web API Server started on port %d\n", port);
    printf("[web_api] mode: pthread-per-connection\n");
    printf("[web_api] max clients: %d\n", MAX_CLIENTS);
    printf("[web_api] endpoints:\n");
    printf("  GET /\n");
    printf("  GET /api/status\n");
    printf("  GET /api/version\n");
    printf("  GET /api/ota/status\n");
    printf("  GET /api/vision/status\n");
    printf("  GET /api/logs\n");
    printf("  GET /video\n");
    printf("  GET /hls/stream.m3u8\n");

    /*
     * 主线程循环 accept。
     *
     * 每来一个客户端连接：
     *  1. 判断当前并发是否超过 MAX_CLIENTS
     *  2. 没超过则创建工作线程
     *  3. 主线程继续 accept 下一个连接
     */
    while (1) {
        pthread_t tid;
        client_arg_t *client_arg;

        client = accept(server_fd, NULL, NULL);

        if (client < 0) {
            perror("accept");
            continue;
        }

        /*
         * 检查并发连接数量。
         */
        pthread_mutex_lock(&active_clients_mutex);

        if (active_clients >= MAX_CLIENTS) {
            pthread_mutex_unlock(&active_clients_mutex);

            send_503(client);
            close(client);

            thread_log("[web_api] reject client: too many clients\n");
            continue;
        }

        active_clients++;

        thread_log("[web_api] client connected, active_clients=%d\n", active_clients);

        pthread_mutex_unlock(&active_clients_mutex);

        /*
         * 为当前客户端准备线程参数。
         */
        client_arg = malloc(sizeof(client_arg_t));

        if (client_arg == NULL) {
            pthread_mutex_lock(&active_clients_mutex);
            active_clients--;
            pthread_mutex_unlock(&active_clients_mutex);

            send_503(client);
            close(client);

            perror("malloc");
            continue;
        }

        client_arg->client_fd = client;

        /*
         * 创建工作线程。
         */
        if (pthread_create(&tid, NULL, client_thread, client_arg) != 0) {
            pthread_mutex_lock(&active_clients_mutex);
            active_clients--;
            pthread_mutex_unlock(&active_clients_mutex);

            free(client_arg);
            send_503(client);
            close(client);

            perror("pthread_create");
            continue;
        }

        /*
         * 分离线程。
         *
         * 分离后线程结束时会自动释放线程资源，
         * 主线程不需要 pthread_join。
         */
        pthread_detach(tid);
    }

    close(server_fd);

    return 0;
}