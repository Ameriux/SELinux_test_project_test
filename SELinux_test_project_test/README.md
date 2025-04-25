# SELinux 不可变文件测试项目

本项目为测试性质，旨在使用SELinux实现具有以下特性的不可变文件系统：

1. **不可变属性**：通过SELinux控制的不可变文件只能被特定特权进程修改或删除
2. **增量更新**：支持使用rsync进行文件的增量更新
3. **时间限制删除**：文件只有达到最小保留期后才能被删除
4. **permissive模式**：SELinux在permissive模式下运行，仅记录违规操作而不强制阻止

## 架构设计

本系统包含以下关键组件：

1. **SELinux策略模块**：定义了必要的安全类型和规则
2. **特权服务进程**：在特权域中运行，提供文件修改和删除API
3. **客户端工具**：与特权服务通信，代表用户执行操作
4. **审计与日志**：记录所有操作和安全违规行为

## 系统要求

- Linux系统（最好是支持SELinux的版本）
- GCC编译器
- Make
- rsync（用于增量更新）
- SELinux工具集（可选，用于测试）

## 安装

请按照以下步骤安装和配置系统：

```bash
# 克隆仓库
git clone https://github.com/your-username/SELinux_test_project_test.git
cd SELinux_test_project_test

# 运行安装脚本
chmod +x scripts/setup.sh
./scripts/setup.sh
```

安装脚本会自动：
- 编译所有必要的组件
- 创建数据目录
- 设置SELinux上下文（如果支持）
- 确保SELinux处于permissive模式（如果支持）

## 使用方法

启动特权服务：

```bash
./immutable_service
```

使用客户端工具管理不可变文件：

```bash
# 创建或修改文件
./immutable_client modify test.txt "这是测试内容"

# 使用rsync增量更新文件
./immutable_client update test.txt "这是更新后的内容"

# 查看文件信息
./immutable_client info test.txt

# 删除文件（仅当文件满足保留期要求时）
./immutable_client delete test.txt
```

## 测试安全机制

运行安全测试脚本检查系统安全特性：

```bash
chmod +x scripts/test_security.sh
./scripts/test_security.sh
```

该脚本会测试：
- 直接修改文件（绕过API）的行为
- 文件删除时的保留期限制
- 错误的认证令牌处理
- 增量更新功能
- 安全审计日志

## 目录结构

- `src/` - 源代码文件
- `policy/` - SELinux策略定义
- `scripts/` - 安装和测试脚本
- `data/` - 数据目录（所有受保护的文件都存储在此）

## 安全特性

- **API认证**：使用令牌、时间戳和请求验证
- **时间限制**：文件在创建后24小时内不可删除
- **增量更新**：允许对文件进行增量更新而非完全重写
- **SELinux保护**：利用SELinux类型强制访问控制
- **审计日志**：详细记录所有操作和尝试

## 仅供测试

注意：此项目仅用于测试目的，不推荐用于生产环境。在实际应用中，应使用更严格的安全措施和更健壮的实现。 