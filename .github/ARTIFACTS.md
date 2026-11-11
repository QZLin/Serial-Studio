# 从 GitHub Actions 获取 Windows 程序

这个 fork 的 **Build Windows Artifact (GPLv3)** 工作流会在 GitHub 托管的
Windows runner 上编译、打包和测试。无需本地 Qt SDK，也无需本地编译。

## 下载

1. 打开仓库 **Actions → Build Windows Artifact (GPLv3)**。
2. 选择一次绿色、成功的运行，在页面底部 **Artifacts** 中下载名称带
   `Windows-x64-Portable` 的 artifact。下载需要登录 GitHub。
3. 解压 GitHub 下载的外层 ZIP，再解压其中的便携版 ZIP。
4. 保留解压后的整个目录，双击 **Serial Studio GPLv3.bat**。
   也可以直接运行目录内的 **bin/Serial-Studio-GPL3.exe**。

不要只复制 EXE；Qt DLL、QML、插件及 WebEngine 资源必须一起保留。
程序包带有 MSVC x64 运行库、许可证说明、构建提交信息和 SHA-256 校验文件。
这是未签名的 Windows 10/11 x64 程序，SmartScreen 可能提示未知发布者。

## 重新构建

- 推送代码到 `master` 会自动构建；纯 Markdown 或 `doc/` 变更除外。
- Pull request 也会构建，不读取任何仓库 secrets。
- 也可以在 Actions 页面点击 **Run workflow**，选择分支后手动构建。
- 程序和对应源码 artifact 保存 **30 天**；诊断日志保存 **14 天**。
  过期后可以再次运行工作流。

工作流只使用公开的 Qt 6.11.2 二进制包，不需要 Qt 用户名、密码、私有图标、
签名证书或 Serial Studio 商业许可证。GitHub PAT 不需要保存为 Actions secret；
通常的 checkout、cache 和 artifact 操作由 GitHub 自带的短期令牌处理。

## 版本与测试范围

构建显式启用 `BUILD_GPL3=ON`、关闭 `BUILD_COMMERCIAL` 和 gRPC，保留
WebEngine、串口、BLE、网络、项目编辑器和 GPL 版可用功能。Pro 专有功能不在
这个程序中；它不是上游发布的 Serial Studio Pro 二进制。

程序 artifact 只会在以下检查成功后上传：

- 部署 EXE、Qt 库、QML 和插件，以及 app-local MSVC 运行库。
- 生成便携 ZIP 并通过 ZIP 完整性检查。
- 将该 ZIP 解压到包含空格的全新目录，从一个空工作目录运行。
- 清除 Qt/QML 环境变量，并将 PATH 限制为 Windows 系统目录。
- 运行独立 GPL Console 日志单元测试：ANSI 清理、UTF-8 BOM、按设备分文件和防覆盖。
- 打包程序实际创建并核验一份 Console 文本日志。
- 打包程序的 QML 自测实际报告 PASS，且原生 Windows 界面成功创建并持续运行。

这些检查验证打包和启动，不代替真实串口/BLE 硬件测试。
`Windows-GPLv3-smoke-logs-*` artifact 提供自测和 GUI 启动日志。
另一个 `Serial-Studio-GPL3-Source-*` artifact 保存该提交的应用与 vendored 源码；
Qt 和 FetchContent 依赖的源码位置见程序包中的说明以及仓库 CMake 文件。

## 工作流文件

- [build-artifacts.yml](./workflows/build-artifacts.yml)：fork 的 Windows 构建。
- [ci.yml](./workflows/ci.yml)：原上游商业发布/测试矩阵，现在仅在
  `Serial-Studio/Serial-Studio` 官方仓库运行，避免普通 fork 因缺 secrets 失败。
- [package-windows-gpl3.ps1](../scripts/ci/package-windows-gpl3.ps1)：依赖检查、
  程序 ZIP、校验和、许可证和源码归档。
- [test-windows-portable.ps1](../scripts/ci/test-windows-portable.ps1)：解压后
  Console 日志、QML 与 GUI 启动测试。

以上两个 PowerShell 脚本带有 GitHub Actions runner 限制，不用于本地构建。
