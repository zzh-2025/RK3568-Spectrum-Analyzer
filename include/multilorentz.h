#pragma once

#include "lorentz.h"

#include <vector>

// 一个分峰的拟合参数。FWHM 单位为 nm，area 为 Lorentzian 积分面积。
struct MultiLorentzPeak {
    double peak_position_nm = 0.0;
    double fwhm_nm = 0.0;
    double peak_height = 0.0;
    double peak_area = 0.0;
};

// 多峰联合拟合的结果。
struct MultiLorentzFitResult {
    std::vector<MultiLorentzPeak> peaks;
    double r_squared = 0.0;
    double rmse = 0.0;
    double sse = 0.0;
    int iterations = 0;
    bool converged = false;
};

// 根据用户提供的初始峰中心位置进行 N 峰 Lorentzian 联合拟合。
// initial_centers_nm 至少包含 1 个中心位置，且应位于光谱波长范围附近。
// 初版使用无基线的多峰叠加模型，与当前单峰函数及 Origin 四峰案例保持一致。
MultiLorentzFitResult fit_multi_lorentz(
    const std::vector<SpectrumPoint>& spectrum,
    const std::vector<double>& initial_centers_nm,
    int max_iterations = 1000
);

// 计算多个 Lorentzian 分峰叠加后的拟合强度。
double multi_lorentz(
    double wavelength_nm,
    const std::vector<MultiLorentzPeak>& peaks
);
