#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace disk {
struct Item { std::filesystem::path path; bool directory=false; std::uint64_t bytes=0, files=0; };
enum class SkipCategory { Error, Intentional };
enum class SkipReason { PermissionDenied, ReparsePoint, FileNotFound, MetadataError, FileSizeError, DirectoryReadError, Other };
struct SkippedEntry {
    std::filesystem::path path;
    SkipCategory category=SkipCategory::Error;
    SkipReason reason=SkipReason::Other;
    std::string message;
    int errorCode=0;
    std::chrono::system_clock::time_point occurredAt{};
};
struct Result {
    std::filesystem::path root;
    std::vector<Item> items;
    std::vector<SkippedEntry> skippedEntries;
    std::uint64_t bytes=0, files=0, directories=0, skipped=0;
    std::uint64_t skippedErrors=0, skippedIntentional=0, skippedNotStored=0;
    bool cancelled=false;
};
Result scan(const std::filesystem::path&, const std::atomic_bool* cancel=nullptr,
            std::atomic<std::uint64_t>* progressFiles=nullptr,
            std::atomic<std::uint64_t>* progressBytes=nullptr,
            std::atomic<std::uint64_t>* progressDirectories=nullptr);
std::string utf8(const std::filesystem::path&);
int runCli(int argc, char** argv);
int runServer(unsigned short port, bool openBrowser=true);
}
