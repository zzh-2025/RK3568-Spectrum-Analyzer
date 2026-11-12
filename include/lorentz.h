
#pragma once

#include <vector>

// 一个光谱采样点
struct SpectrumPoint {
    double wavelength;
    double intensity;
};

// Lorentz 拟合结果
struct LorentzFitResult {
    double peak_position_nm;
    double fwhm_nm;
    double peak_height;
    double peak_area;
    double r_squared;

    int iterations;
    bool converged;
};

// Lorentz 峰函数
double lorentz(double x, double xc, double w, double A);

// 执行拟合
LorentzFitResult fit_lorentz(
    const std::vector<SpectrumPoint>& spectrum
);
