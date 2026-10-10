#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) { std::cerr << "Nao foi possivel iniciar a varredura: " << ec.message() << "\n"; return 1; }

    while (it != end) {
        const fs::directory_entry entry = *it;
        if (!csvPath.empty() && entry.path().lexically_normal() == csvPath) {
            it.increment(ec);
            if (ec) { ++skipped; ec.clear(); }
            continue;
        }
        std::error_code local;
        // Nunca segue links simbolicos/juncoes: evita ciclos e dupla contagem.
        bool symlink = entry.is_symlink(local);
        if (local) { ++skipped; }
        else if (symlink) { if (entry.is_directory(local)) it.disable_recursion_pending(); ++skipped; }
        else if (entry.is_regular_file(local) && !local) {
            const auto bytes = entry.file_size(local);
            if (!local) {
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
                // Agrega bytes em cada pasta ancestral ate a raiz escolhida.
                for (fs::path parent = entry.path().parent_path(); ; parent = parent.parent_path()) {
                    auto& f = folders[parent];
                    f.path = parent;
                    f.size += bytes;
                    ++f.files;
                    if (parent == root || parent == parent.parent_path()) break;
                }
            } else ++skipped;
        } else if (local) ++skipped;
        it.increment(ec);
        if (ec) { ++skipped; ec.clear(); }
    }
    std::vector<FolderInfo> direct;
    for (const auto& [path, info] : folders)
        if (path != root && path.parent_path() == root) direct.push_back(info);
    std::sort(direct.begin(), direct.end(), [](const auto& a, const auto& b) { return a.size > b.size; });
    std::sort(largest.begin(), largest.end(), [](const auto& a, const auto& b) { return a.size > b.size; });

    std::cout << "\nPasta analisada: " << utf8(root) << '\n'
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
