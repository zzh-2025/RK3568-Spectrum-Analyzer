
#include "lorentz.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;


// 生成时间戳，避免覆盖之前的批次
std::string make_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    localtime_r(&time, &local_time);

    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y%m%d_%H%M%S");

    return oss.str();
}


// 转义 CSV 字段，避免文件名中的逗号等字符破坏格式
std::string csv_escape(const std::string& value) {
    std::string result = "\"";

    for (char ch : value) {
        if (ch == '"') {
            result += "\"\"";
        } else {
            result += ch;
        }
    }

    result += '"';
    return result;
}


// 读取 ASC 文件的前两列：波长、光强
std::vector<SpectrumPoint> load_spectrum(const fs::path& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Cannot open input file.");
    }

    std::vector<SpectrumPoint> spectrum;
    std::string line;

    while (std::getline(file, line)) {
        std::istringstream iss(line);

        double wavelength;
        double intensity;

        // 空行、表头或无法解析的行会被跳过
        if (iss >> wavelength >> intensity) {
            if (std::isfinite(wavelength) &&
                std::isfinite(intensity)) {
                spectrum.push_back({wavelength, intensity});
            }
        }
    }

    if (spectrum.size() < 3) {
        throw std::runtime_error(
            "Fewer than 3 valid spectrum points."
        );
    }

    return spectrum;
}


// 保存单条光谱的原始数据、拟合数据和残差
void save_fit_csv(
    const fs::path& path,
    const std::vector<SpectrumPoint>& spectrum,
    const LorentzFitResult& result
) {
    std::ofstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error(
            "Cannot create fit CSV: " + path.string()
        );
    }

    file << "Wavelength_nm,Raw_Intensity,"
            "Fitted_Intensity,Residual\n";

    file << std::setprecision(12);

    for (const auto& point : spectrum) {
        const double fitted = lorentz(
            point.wavelength,
            result.peak_position_nm,
            result.fwhm_nm,
            result.peak_area
        );

        const double residual = point.intensity - fitted;

        file << point.wavelength << ','
             << point.intensity << ','
             << fitted << ','
             << residual << '\n';
    }

    if (!file) {
        throw std::runtime_error(
            "Failed while writing fit CSV: " + path.string()
        );
    }
}


int main(int argc, char* argv[]) {
    // 可通过命令行传入数据目录和结果根目录
    const fs::path data_dir =
        argc >= 2 ? fs::path(argv[1]) : fs::path("data");

    const fs::path results_root =
        argc >= 3 ? fs::path(argv[2]) : fs::path("results");

    if (!fs::is_directory(data_dir)) {
        std::cerr << "Error: data directory not found: "
                  << data_dir << '\n';
        return 1;
    }

    // 收集所有 ASC 文件
    std::vector<fs::path> files;

    try {
        for (const auto& entry : fs::directory_iterator(data_dir)) {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".asc") {
                files.push_back(entry.path());
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error scanning data directory: "
                  << e.what() << '\n';
        return 1;
    }

    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cerr << "Error: no .asc files found in "
                  << data_dir << '\n';
        return 1;
    }

    // 创建独立的 C++ 批次目录
    const fs::path batch_dir =
        results_root / ("cpp_batch_" + make_timestamp());

    const fs::path fits_dir = batch_dir / "fits";

    try {
        fs::create_directories(fits_dir);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating output directories: "
                  << e.what() << '\n';
        return 1;
    }

    const fs::path summary_path = batch_dir / "summary.csv";
    const fs::path errors_path = batch_dir / "errors.txt";

    std::ofstream summary(summary_path);
    std::ofstream errors(errors_path);

    if (!summary.is_open() || !errors.is_open()) {
        std::cerr << "Error: cannot create batch output files.\n";
        return 1;
    }

    summary
        << "filename,peak_position_nm,fwhm_nm,"
        << "peak_height,peak_area,r_squared,"
        << "iterations,converged\n";

    summary << std::setprecision(12);

    int success_count = 0;
    int failure_count = 0;

    std::cout << "===== C++ Batch Spectrum Analysis =====\n";
    std::cout << "Input directory: " << data_dir << '\n';
    std::cout << "Total ASC files: " << files.size() << "\n\n";

    for (const auto& path : files) {
        const std::string filename = path.filename().string();

        std::cout << "Processing: " << filename << '\n';

        try {
            const auto spectrum = load_spectrum(path);
            const auto result = fit_lorentz(spectrum);

            const fs::path fit_path =
                fits_dir / (path.stem().string() + "_fit.csv");

            save_fit_csv(fit_path, spectrum, result);

            summary
                << csv_escape(filename) << ','
                << result.peak_position_nm << ','
                << result.fwhm_nm << ','
                << result.peak_height << ','
                << result.peak_area << ','
                << result.r_squared << ','
                << result.iterations << ','
                << (result.converged ? "true" : "false")
                << '\n';

            if (!summary) {
                throw std::runtime_error(
                    "Failed while writing summary.csv."
                );
            }

            ++success_count;

            std::cout << "  Peak: "
                      << result.peak_position_nm
                      << " nm, FWHM: "
                      << result.fwhm_nm
                      << " nm, R2: "
                      << result.r_squared << '\n';

            if (!result.converged) {
                errors << filename
                       << ": optimizer did not confirm convergence.\n";

                std::cerr
                    << "  Warning: convergence not confirmed.\n";
            }

        } catch (const std::exception& e) {
            ++failure_count;

            errors << filename << ": " << e.what() << '\n';

            std::cerr << "  Failed: " << e.what() << '\n';
        }
    }

    summary.close();
    errors.close();

    // 只更新 C++ 专用指针，不覆盖 Python 的 latest_batch.txt
    const fs::path latest_path =
        results_root / "latest_cpp_batch.txt";

    std::ofstream latest(latest_path);

    if (!latest.is_open()) {
        std::cerr << "Error: cannot write "
                  << latest_path << '\n';
        return 1;
    }

    latest << batch_dir.generic_string() << '\n';
    latest.close();

    std::cout << "\n===== Batch Analysis Finished =====\n";
    std::cout << "Successful: " << success_count << '\n';
    std::cout << "Failed: " << failure_count << '\n';
    std::cout << "Summary: " << summary_path << '\n';
    std::cout << "Fit CSV directory: " << fits_dir << '\n';
    std::cout << "Error log: " << errors_path << '\n';
    std::cout << "Latest C++ batch: " << latest_path << '\n';

    // 即使个别文件失败，也保留其他文件的分析结果
    return failure_count == 0 ? 0 : 2;
}
