#include "emuscale/core/Benchmark.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <utility>
#include <windows.h>

namespace emuscale {
void Benchmark::start(std::int64_t now_qpc, int duration_seconds) {
    if (duration_seconds != 30 && duration_seconds != 60 && duration_seconds != 120)
        throw std::invalid_argument("Benchmark duration must be 30, 60, or 120 seconds");
    duration_seconds_ = duration_seconds;
    start_qpc_ = now_qpc;
    rows_.clear();
    active_ = true;
}
double Benchmark::elapsed(std::int64_t now_qpc, std::int64_t frequency) const noexcept {
    if (!active_ || frequency <= 0) return 0;
    return static_cast<double>(now_qpc - start_qpc_) / frequency;
}
void Benchmark::add(BenchmarkRow row) {
    if (active_) rows_.push_back(std::move(row));
}
bool Benchmark::expired(std::int64_t now_qpc, std::int64_t frequency) const noexcept {
    return active_ && elapsed(now_qpc, frequency) >= duration_seconds_;
}
std::filesystem::path Benchmark::stop_and_export(
    const std::filesystem::path& directory, const std::wstring& gpu_name) {
    if (!active_) return {};
    active_ = false;
    std::filesystem::create_directories(directory);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::filesystem::path path;
    for (int suffix = 0; suffix < 1000; ++suffix) {
        std::wostringstream name;
        name << L"emuscale_" << std::put_time(&local, L"%Y%m%d_%H%M%S")
             << L'_' << suffix << L".csv";
        path = directory / name.str();
        if (!std::filesystem::exists(path)) break;
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) throw std::runtime_error("Cannot create benchmark CSV");
    file << "elapsed_s,distinct_capture_fps,callback_fps,presented_fps,target_fps,"
            "average_source_ms,average_callback_ms,average_present_ms,"
            "one_percent_low_fps,pacing_stddev_ms,"
            "overwritten_total,repeated_timestamp_total,capped_total,generated_total,"
            "gpu,capture_width,capture_height,output_width,output_height,vsync\n";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, gpu_name.data(),
        static_cast<int>(gpu_name.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("Cannot encode GPU name as UTF-8");
    std::string gpu(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, gpu_name.data(),
        static_cast<int>(gpu_name.size()), gpu.data(), bytes, nullptr, nullptr);
    for (char& c : gpu) if (c == ',' || c == '"' || c == '\n') c = ' ';
    file << std::fixed << std::setprecision(3);
    for (const auto& row : rows_) {
        const auto& m = row.metrics;
        file << row.elapsed_seconds << ',' << m.distinct_capture_fps << ','
             << m.callback_fps << ',' << m.presented_fps << ',' << row.target_fps << ','
             << m.average_source_ms << ',' << m.average_callback_ms << ','
             << m.average_present_ms << ',' << m.one_percent_low_fps << ','
             << m.pacing_stddev_ms << ',' << row.overwritten << ','
             << m.repeated_timestamp_total << ',' << m.capped_total << ','
             << m.generated_total << ',' << gpu << ',' << row.capture_width << ','
             << row.capture_height << ',' << row.output_width << ',' << row.output_height
             << ',' << (row.vsync == VSyncMode::off ? "Off" :
                         row.vsync == VSyncMode::on ? "On" : "Auto") << '\n';
    }
    if (!file) throw std::runtime_error("Benchmark CSV write failed");
    return path;
}
} // namespace emuscale
