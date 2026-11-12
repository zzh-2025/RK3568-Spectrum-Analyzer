# RK3568 Spectrum Analyzer

基于 **RK3568 嵌入式 Linux + C++17 + Qt5** 的暗场散射光谱分析平台，面向金纳米颗粒等样品的 ASC 光谱读取、Lorentz 拟合、批量分析与谱峰耦合参数初步评估。

> **项目阶段：** 核心算法与 Qt 应用源码已建立；OTA 应用升级尚未实现。当前仓库是 OTA 开发前的源码基线，尚未标记正式发布版本。

## 功能

- **单峰 Lorentz 拟合：** 提取峰位、FWHM、峰高、面积及拟合质量指标。
- **多峰联合拟合：** 支持多个 Lorentz 分量及其拟合参数。
- **自动寻峰与模型选择：** 为指定峰数提供自动初值搜索；结合基线比较与模型选择筛选候选模型。
- **基线比较：** 支持无基线、常数基线、线性基线等选择。
- **批量分析：** 处理目录中的 `.asc` 文件，保存汇总、曲线与残差数据。
- **耦合参数初筛：** 按目标波长、峰序号、波段窗口等方式选择谱峰，计算表观能量分裂等指标，并标记待复核结果。
- **Qt 可视化：** 数据目录选择、分析参数设置、历史批次、拟合曲线、分峰曲线、残差、统计表及运行日志。

**科研解释提醒：** 拟合峰间的表观能量间隔及线宽判据只能用于光谱初筛，不能单独证明强耦合，也不能直接等同于经完整模型验证的耦合强度。

## 硬件与软件环境

- 目标开发板：KICKPI-K1MINI（RK3568，ARM64 / aarch64）
- 已使用系统：Ubuntu 20.04.6 LTS
- 开发语言：C++17
- 构建：CMake 3.16+，GCC/G++
- 界面：Qt5 Widgets、Qt5 Charts（Qt 5.12+）

已在上述开发板上检查主要可执行程序的动态链接依赖；**本独立仓库的完整重新构建及数据路径部署验证仍需执行**。

## 源码目录

```text
RK3568-Spectrum-Analyzer/
├── CMakeLists.txt          # 核心库、命令行工具与算法测试
├── apps/                   # 单文件、批量及耦合分析入口
├── include/                # 算法头文件
├── src/                    # 拟合、寻峰、基线、模型选择、耦合计算
├── qt_app/                 # Qt5 Widgets / Charts 桌面应用
│   └── CMakeLists.txt
├── tests/                  # 独立算法测试程序
└── .gitignore
```

`data/`、`results/`、编译目录和原始 `.asc` 文件不纳入 Git 版本管理，以保护实验数据并保持仓库精简。

## 在 RK3568 / Ubuntu 上构建

先确保安装 GCC/G++、CMake、Qt5 Widgets 与 Qt5 Charts 开发依赖。Ubuntu 20.04 上通常可使用：

```bash
sudo apt update
sudo apt install build-essential cmake qtbase5-dev libqt5charts5-dev
```

在仓库根目录分别构建命令行工具和 Qt：

```bash
cd ~/RK3568-Spectrum-Analyzer

# 核心算法、命令行工具与测试
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j2

# Qt 应用（独立 CMake 工程）
cmake -S qt_app -B qt_app/build_stage8 -DCMAKE_BUILD_TYPE=Release
cmake --build qt_app/build_stage8 -j2
```

构建成功后，主要程序应位于：

- `build_release/spectrum_batch`：单峰批量分析
- `build_release/coupling_batch`：多峰和耦合参数批量分析
- `qt_app/build_stage8/spectrum_qt`：Qt 图形界面

## 运行

请自行准备实验 `.asc` 数据文件；本仓库**不附带原始实验数据**。

```bash
cd ~/RK3568-Spectrum-Analyzer
mkdir -p data results

# 将你自己的 ASC 数据放到 data/ 后：
./build_release/spectrum_batch data results

# 多峰/耦合分析示例
./build_release/coupling_batch data --out results --peaks auto --baseline auto

# 在板端桌面会话中启动 Qt
./qt_app/build_stage8/spectrum_qt
```

Qt 当前会通过项目目录识别 `data/`、`results/`，并从 `build_release/` 或 `build/` 查找批量分析程序。因此请先按上述方式构建核心命令行程序，再运行 Qt。**这套路径机制后续需要为 OTA 部署重构。**

`spectrum_reader` 是单文件测试入口，当前实现还会向工作目录下的 `cpp_version/` 写入 CSV；单独使用该程序前请创建此目录。

## OTA 开发计划（未完成）

目标是 **只升级应用程序，不升级 Ubuntu、内核或 U-Boot**。

1. 版本号与可复现的 ARM64 Release 打包；
2. 局域网发布端与 `manifest.json`；
3. RK3568 下载客户端、文件大小与 SHA-256 校验；
4. 分版本部署、安装失败恢复及启动健康检查；
5. Qt 界面中的检查更新与进度反馈；
6. 后续再考虑 HTTPS、数字签名与远程发布。

正式 OTA 方案将把应用发布目录和 `data/`、`results/` 用户数据目录分离，避免升级覆盖科研数据。

## 开源状态

仓库目前为私有仓库；尚未添加 LICENSE，**暂不声明任何开源许可证**。在公开前请确认代码、第三方依赖及示例数据的发布权限，并选择合适的许可证。

---

**历史说明：** 仓库中的 `README.txt` 是早期多峰拟合独立测试的旧说明，不能代表当前项目状态；请以本 `README.md` 为准。
