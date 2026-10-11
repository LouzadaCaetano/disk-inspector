#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct FileInfo { fs::path path; std::uintmax_t size; };
struct FolderInfo { fs::path path; std::uintmax_t size = 0; std::uintmax_t files = 0; };

static std::string utf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
static std::string csvEscape(const std::string& value) {
    std::string result = "\"";
    for (char c : value) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
static double gib(std::uintmax_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024 * 1024); }
static void usage() {
    std::cout << "Disk Inspector - analisador de armazenamento (somente leitura)\n\n"
              << "Uso: disk-inspector --path CAMINHO [--top N] [--csv ARQUIVO]\n"
              << "Exemplo: disk-inspector --path C:\\ --top 30 --csv relatorio.csv\n"
              << "Opcoes: --help, --path, --top (1 a 1000), --csv\n";
}
int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Mantem o console compatível com o UTF-8 produzido por filesystem::u8string.
    // Para acentos no CMD, use 'chcp 65001' antes de executar.
#endif
    fs::path root;
    fs::path csvPath;
    std::size_t topN = 30;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { usage(); return 0; }
        if ((arg == "--path" || arg == "--csv" || arg == "--top") && i + 1 >= argc) {
            std::cerr << "Falta valor para " << arg << "\n"; return 2;
        }
        if (arg == "--path") root = fs::u8path(argv[++i]);
        else if (arg == "--csv") csvPath = fs::u8path(argv[++i]);
        else if (arg == "--top") {
            try {
                std::string input = argv[++i];
                std::size_t used = 0;
                unsigned long n = std::stoul(input, &used);
                if (used != input.size() || n < 1 || n > 1000) throw std::out_of_range("top");
                topN = static_cast<std::size_t>(n);
            } catch (...) { std::cerr << "--top deve ser um inteiro entre 1 e 1000.\n"; return 2; }
        } else { std::cerr << "Opcao desconhecida: " << arg << "\n"; usage(); return 2; }
    }
    if (root.empty()) { usage(); return 2; }
    std::error_code ec;
    root = fs::absolute(root, ec);
    if (ec || !fs::is_directory(root, ec)) { std::cerr << "Pasta invalida ou inacessivel.\n"; return 2; }
    root = root.lexically_normal();

    std::ofstream csv;
    if (!csvPath.empty()) {
        csvPath = fs::absolute(csvPath).lexically_normal();
        csv.open(csvPath, std::ios::binary | std::ios::trunc);
        if (!csv) { std::cerr << "Nao foi possivel abrir o arquivo CSV.\n"; return 2; }
        csv << "caminho,tamanho_bytes,tamanho_gib\n";
    }

    std::map<fs::path, FolderInfo> folders;
    std::vector<FileInfo> largest;
    std::uintmax_t totalBytes = 0, fileCount = 0, skipped = 0;
    // Iteracao explicita por pasta: erro de acesso em uma pasta nao encerra a varredura inteira.
    std::vector<fs::path> pending{root};
    std::uintmax_t scannedDirectories = 0;
    while (!pending.empty()) {
        fs::path dir = std::move(pending.back());
        pending.pop_back();
        ++scannedDirectories;
        std::error_code dirError;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, dirError), end;
        if (dirError) { ++skipped; continue; }
        while (it != end) {
            const fs::directory_entry entry = *it;
            std::error_code local;
            bool isReparse = entry.is_symlink(local);
#ifdef _WIN32
            // Juncoes de diretorio no Windows nem sempre sao reportadas como symlinks.
            DWORD attrs = GetFileAttributesW(entry.path().c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) isReparse = true;
#endif
            if (local) { ++skipped; }
            else if (isReparse) { ++skipped; }
            else if (entry.is_directory(local) && !local) {
                pending.push_back(entry.path());
            } else if (entry.is_regular_file(local) && !local) {
                if (!csvPath.empty() && entry.path().lexically_normal() == csvPath) {
                    // Nunca mede o CSV que esta sendo escrito durante a leitura.
                } else {
                    auto bytes = entry.file_size(local);
                    if (local) { ++skipped; }
                    else {
                        ++fileCount;
                        totalBytes += bytes;
                        if (csv) csv << csvEscape(utf8(entry.path())) << ',' << bytes << ','
                                     << std::fixed << std::setprecision(6) << gib(bytes) << '\n';
                        if (largest.size() < topN) largest.push_back({entry.path(), bytes});
                        else {
                            auto smallest = std::min_element(largest.begin(), largest.end(),
                                [](const FileInfo& a, const FileInfo& b) { return a.size < b.size; });
                            if (bytes > smallest->size) *smallest = {entry.path(), bytes};
                        }
                        for (fs::path parent = entry.path().parent_path(); ; parent = parent.parent_path()) {
                            auto& f = folders[parent];
                            f.path = parent;
                            f.size += bytes;
                            ++f.files;
                            if (parent == root || parent == parent.parent_path()) break;
                        }
                    }
                }
            } else if (local) { ++skipped; }
            it.increment(dirError);
            if (dirError) {
                ++skipped;
                // A iteracao dessa pasta pode ter terminado, mas a pilha ainda possui outras pastas.
                break;
            }
        }
    }
    std::vector<FolderInfo> direct;
    for (const auto& [path, info] : folders)
        if (path != root && path.parent_path() == root) direct.push_back(info);
    std::sort(direct.begin(), direct.end(), [](const auto& a, const auto& b) { return a.size > b.size; });
    std::sort(largest.begin(), largest.end(), [](const auto& a, const auto& b) { return a.size > b.size; });

    std::cout << "\nPasta analisada: " << utf8(root) << '\n'
              << "Pastas visitadas: " << scannedDirectories << '\n'
              << "Arquivos encontrados: " << fileCount << '\n'
              << "Tamanho total encontrado: " << std::fixed << std::setprecision(2) << gib(totalBytes) << " GiB\n"
              << "Entradas ignoradas/erros: " << skipped << "\n\n"
              << "=== SUBPASTAS DIRETAS MAIS PESADAS ===\n";
    for (const auto& f : direct)
        std::cout << std::setw(10) << gib(f.size) << " GiB | " << f.files << " arquivos | " << utf8(f.path) << '\n';
    std::cout << "\n=== " << largest.size() << " MAIORES ARQUIVOS ===\n";
    for (const auto& f : largest)
        std::cout << std::setw(10) << gib(f.size) << " GiB | " << utf8(f.path) << '\n';
    if (csv) std::cout << "\nRelatorio salvo: " << utf8(csvPath) << '\n';
    std::cout << "\nAviso: totais representam arquivos acessiveis; nao equivalem necessariamente ao espaco usado no disco.\n";
    return 0;
}
