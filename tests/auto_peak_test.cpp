#include "autopeak.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: auto_peak_test <file.asc> <peak_count> [--full]\n";
        std::cerr << "Example: auto_peak_test data/3-大凹陷.asc 4\n";
        return 1;
    }

    try {
        const int peak_count = std::stoi(argv[2]);
        AutoPeakSearchMode mode = AutoPeakSearchMode::Adaptive;
        if (argc == 4) {
            if (std::string(argv[3]) != "--full")
                throw std::invalid_argument("Unknown option. Use --full.");
            mode = AutoPeakSearchMode::Full;
        }
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("Cannot open input ASC file.");

        std::vector<SpectrumPoint> spectrum;
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream iss(line);
            double wavelength = 0.0, intensity = 0.0;
            if (iss >> wavelength >> intensity &&
                std::isfinite(wavelength) && std::isfinite(intensity))
                spectrum.push_back({wavelength, intensity});
        }

        const auto result = fit_auto_centers(spectrum, peak_count, 350, mode);
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "Points: " << spectrum.size() << '\n';
        std::cout << "Requested peak count: " << peak_count << '\n';
        std::cout << "Search mode: " << (mode == AutoPeakSearchMode::Full ? "full" : "adaptive") << '\n';
        std::cout << "Initialization trials: " << result.trials_attempted << '\n';
        std::cout << "Converged trials: " << result.converged_trials << '\n';
        std::cout << "Fallback used: " << (result.used_fallback ? "yes" : "no") << '\n';
        std::cout << "Elapsed milliseconds: " << result.elapsed_ms << '\n';
        std::cout << "Chosen initial centers (nm):";
        for (double c : result.initial_centers_nm) std::cout << ' ' << c;
        std::cout << '\n';
        std::cout << "R2: " << result.fit.r_squared << '\n';
        std::cout << "RMSE: " << result.fit.rmse << '\n';
        std::cout << "SSE: " << result.fit.sse << '\n';
        std::cout << "Iterations: " << result.fit.iterations << '\n';
        std::cout << "Converged: " << (result.fit.converged ? "yes" : "no") << '\n';
        std::cout << "Index,Center_nm,FWHM_nm,PeakHeight,Area\n";
        for (std::size_t i = 0; i < result.fit.peaks.size(); ++i) {
            const auto& p = result.fit.peaks[i];
            std::cout << i+1 << ',' << p.peak_position_nm << ','
                      << p.fwhm_nm << ',' << p.peak_height << ','
                      << p.peak_area << '\n';
        }
        return result.fit.converged ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "Auto initialization failed: " << e.what() << '\n';
        return 3;
    }
}
