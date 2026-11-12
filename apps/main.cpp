
#include "lorentz.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr
            << "Usage: spectrum_reader <file.asc>\n";
        return 1;
    }

    const std::string filename = argv[1];
    const std::filesystem::path input_path(filename);

    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: cannot open "
                  << filename << '\n';
        return 1;
    }

    std::vector<SpectrumPoint> spectrum;
    std::string line;

    while (std::getline(file, line)) {
        std::istringstream iss(line);

        double wavelength, intensity;

        if (iss >> wavelength >> intensity) {
            if (std::isfinite(wavelength) &&
                std::isfinite(intensity)) {
                spectrum.push_back({
                    wavelength,
                    intensity
                });
            }
        }
    }

    if (spectrum.size() < 3) {
        std::cerr
            << "Error: insufficient valid spectrum data.\n";
        return 1;
    }

    try {
        const LorentzFitResult result =
            fit_lorentz(spectrum);

        std::cout << std::fixed << std::setprecision(8);

        std::cout << "\n===== C++ Lorentz Fit Results =====\n";
        std::cout << "File: " << filename << '\n';
        std::cout << "Data points: "
                  << spectrum.size() << '\n';

        std::cout << "Peak position (nm): "
                  << result.peak_position_nm << '\n';

        std::cout << "FWHM (nm): "
                  << result.fwhm_nm << '\n';

        std::cout << "Peak height: "
                  << result.peak_height << '\n';

        std::cout << "Peak area: "
                  << result.peak_area << '\n';

        std::cout << "R-squared: "
                  << result.r_squared << '\n';

        std::cout << "Iterations: "
                  << result.iterations << '\n';

        std::cout << "Converged: "
                  << (result.converged ? "yes" : "no")
                  << '\n';

        if (!result.converged) {
            std::cerr
                << "Warning: optimizer did not confirm convergence.\n";
        }

        // 保存拟合曲线和残差，供后续与 Python 对照
        const std::filesystem::path output_path =
            std::filesystem::path("cpp_version") /
            (input_path.stem().string() + "_cpp_fit.csv");

        std::ofstream output(output_path);

        if (!output.is_open()) {
            std::cerr
                << "Error: cannot create output CSV: "
                << output_path << '\n';
            return 1;
        }

        output << "Wavelength_nm,Raw_Intensity,"
                  "Fitted_Intensity,Residual\n";

        output << std::setprecision(12);

        for (const auto& point : spectrum) {
            const double fitted = lorentz(
                point.wavelength,
                result.peak_position_nm,
                result.fwhm_nm,
                result.peak_area
            );

            const double residual =
                point.intensity - fitted;

            output << point.wavelength << ','
                   << point.intensity << ','
                   << fitted << ','
                   << residual << '\n';
        }

        std::cout << "Fit data saved to: "
                  << output_path << '\n';

    } catch (const std::exception& e) {
        std::cerr << "Fitting failed: "
                  << e.what() << '\n';
        return 1;
    }

    return 0;
}
