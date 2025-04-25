#!/bin/bash
set -e

BASE_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd $BASE_DIR

echo "===== SELinux 不可变文件测试项目安装 ====="

# 创建目录
echo "创建必要的目录..."
mkdir -p data

# 编译代码
echo "编译代码..."
make clean
make all

# 确保SELinux处于permissive模式
if command -v getenforce &> /dev/null; then
    current_mode=$(getenforce)
    if [ "$current_mode" == "Enforcing" ]; then
        echo "将SELinux设置为permissive模式..."
        sudo setenforce 0
        echo "注意: 系统将保持在permissive模式直到重启或手动更改"
    fi
else
    echo "警告: 未检测到SELinux，某些功能可能无法正常工作"
fi

# 设置SELinux上下文 (即使在permissive模式下也能工作)
if command -v chcon &> /dev/null; then
    echo "设置SELinux上下文..."
    sudo chcon -R -t immutable_data_dir_t data/ 2>/dev/null || true
    sudo chcon -t immutable_service_exec_t immutable_service 2>/dev/null || true
else
    echo "警告: 未检测到chcon命令，跳过上下文设置"
fi

echo "安装完成!"
echo 
echo "您可以运行以下命令测试功能:"
echo "1. 启动服务: ./immutable_service"
echo "2. 在另一个终端中运行客户端命令:"
echo "   ./immutable_client modify test.txt \"这是测试内容\""
echo "   ./immutable_client info test.txt"
echo "   ./immutable_client update test.txt \"这是更新后的内容\""
echo "   ./immutable_client delete test.txt"
echo
echo "或者直接运行测试示例: make example" 