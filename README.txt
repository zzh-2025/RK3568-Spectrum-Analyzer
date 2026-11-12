第一阶段：固定峰数多峰 Lorentzian 拟合核心（独立测试版）

包含：
- multilorentz.h / multilorentz.cpp：N 峰 Lorentzian 联合拟合。
- multi_lorentz_test.cpp：命令行测试程序，输入 ASC 文件和每个初始峰中心。
- CMakeLists.txt：在原有 spectrum_reader、spectrum_batch 基础上增加 multi_lorentz_test 目标。

重要说明：
1. 这是第一阶段“固定峰数 + 初始峰位”的独立测试，不包含自动寻峰，也没有接入现有批量处理或 Qt。
2. 不要覆盖 qt_app 目录中的 CMakeLists.txt。这里的 CMakeLists.txt 是 cpp-version 根目录下的配置。
3. 替换前先备份 cpp-version/CMakeLists.txt；新增三个源文件到 cpp-version 根目录。
4. 此实现使用与现有 lorentz() 一致的面积参数化，拟合无基线 N 峰叠加模型。
5. 在 RK3568 上编译成功并验证后，再考虑接入 batch_analysis.cpp 与 Qt。

开发板操作步骤：
A. 进入项目根目录：
   cd /home/kickpi/RK3568_optical_project/spectrum_analysis/source_import/代码/cpp-version

B. 备份根目录 CMake：
   cp CMakeLists.txt CMakeLists.txt.bak_multilorentz

C. 将 multilorentz.h、multilorentz.cpp、multi_lorentz_test.cpp 放到当前目录。
   用本包的 CMakeLists.txt 替换当前目录的 CMakeLists.txt（不要替换 qt_app/CMakeLists.txt）。

D. 重新配置并编译：
   cmake -S . -B build
   cmake --build build -j2

E. 运行四峰验证（先把 3-大凹陷.asc 上传到开发板，例如放到 data/ 目录）：
   ./build/multi_lorentz_test "data/3-大凹陷.asc" 551.3 595.8 639.0 705.3

如果 ASC 文件不在 data/，请将命令中的路径改为实际路径。
该命令的四个中心位置只是初始猜测，拟合程序会联合优化所有峰的中心、FWHM 和面积。

本地测试记录：
- 使用 3-大凹陷.asc，共读取 1600 个数据点。
- 四峰模型完成拟合，R² ≈ 0.987166，RMSE ≈ 34.6683。
- 拟合中心约为 551.263、595.756、639.031、705.261 nm。
- 这是本地参考测试；仍需在 RK3568 上编译并复核。