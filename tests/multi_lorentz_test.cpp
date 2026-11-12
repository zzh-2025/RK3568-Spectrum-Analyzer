#include "multilorentz.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: multi_lorentz_test <file.asc> <center1> [center2 ...]\n";
        std::cerr << "Example: multi_lorentz_test 3-大凹陷.asc 551.3 595.8 639.0 705.3\n";
        return 1;
    }

    std::vector<SpectrumPoint> spectrum;
    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "Cannot open input file: " << argv[1] << '\n';
        return 1;
    }

    std::string line;
    while (std::getline(input, line)) {
        std::istringstream iss(line);
        double x = 0.0, y = 0.0;
        if (iss >> x >> y && std::isfinite(x) && std::isfinite(y))
            spectrum.push_back({x, y});
    }

    std::vector<double> centers;
    for (int i = 2; i < argc; ++i) {
        try {
            centers.push_back(std::stod(argv[i]));
        } catch (...) {
            std::cerr << "Invalid center: " << argv[i] << '\n';
            return 1;
        }
    }

    try {
        const auto result = fit_multi_lorentz(spectrum, centers);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "Points: " << spectrum.size() << '\n';
        std::cout << "Peak count: " << result.peaks.size() << '\n';
        std::cout << "R2: " << result.r_squared << '\n';
        std::cout << "RMSE: " << result.rmse << '\n';
        std::cout << "SSE: " << result.sse << '\n';
        std::cout << "Iterations: " << result.iterations << '\n';
        std::cout << "Converged: " << (result.converged ? "yes" : "no") << "\n\n";

        std::cout << "Index,Center_nm,FWHM_nm,PeakHeight,Area\n";
        for (std::size_t i = 0; i < result.peaks.size(); ++i) {
            const auto& p = result.peaks[i];
            std::cout << (i + 1) << ','
                      << p.peak_position_nm << ','
                      << p.fwhm_nm << ','
                      << p.peak_height << ','
                      << p.peak_area << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "Fit failed: " << e.what() << '\n';
        return 2;
    }

    return 0;
}
