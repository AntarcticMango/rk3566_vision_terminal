#!/bin/bash

SDK_DIR="${1:-$HOME/tspi/linux}"

echo "========================================"
echo " RK3566 SDK 修改状态检查"
echo "========================================"
echo "SDK 路径: $SDK_DIR"
echo

if [ ! -d "$SDK_DIR" ]; then
    echo "错误：SDK 目录不存在：$SDK_DIR"
    exit 1
fi

if [ -d "$SDK_DIR/.repo" ]; then
    echo "[OK] 检测到 .repo，当前 SDK 是 repo 多仓库结构"
else
    echo "[提示] 未检测到 .repo"
fi

echo
echo "========================================"
echo " 重点子仓库 Git 状态"
echo "========================================"

REPOS=(
    "buildroot"
    "device/rockchip"
    "u-boot"
    "kernel"
    "external/rkwifibt"
)

for repo in "${REPOS[@]}"; do
    echo
    echo "---------- $repo ----------"

    if [ ! -d "$SDK_DIR/$repo" ]; then
        echo "目录不存在"
        continue
    fi

    if [ ! -d "$SDK_DIR/$repo/.git" ]; then
        echo "不是 Git 子仓库"
        continue
    fi

    status=$(git -C "$SDK_DIR/$repo" status --short)

    if [ -z "$status" ]; then
        echo "干净，无修改"
    else
        echo "$status"
    fi
done

echo
echo "========================================"
echo " 可能不应进入 patch 的文件"
echo "========================================"

echo
echo "[1] 备份文件："
find "$SDK_DIR" \
    -path "$SDK_DIR/.repo" -prune -o \
    -name "*.bak" -print 2>/dev/null | head -50

echo
echo "[2] 备份目录："
find "$SDK_DIR" \
    -path "$SDK_DIR/.repo" -prune -o \
    -type d \( -name "*backup*" -o -name "*bak*" \) -print 2>/dev/null | head -50

echo
echo "[3] overlay 中的 ELF 二进制文件："
find "$SDK_DIR/buildroot" \
    -path "*/fs-overlay/*" -type f -exec file {} \; 2>/dev/null | grep "ELF" || true

echo
echo "========================================"
echo " 检查完成"
echo "========================================"
echo
echo "建议："
echo "1. 每天收工前运行一次本脚本"
echo "2. 将关键输出记录到 docs/DEVLOG.md"
echo "3. 阶段性完成后再整理 patches/sdk/*.patch"
echo "4. .bak、backup 目录、ELF 二进制文件通常不要放进 patch"
