#include "scanner.h"
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace disk {
std::string utf8(const fs::path& p) { auto s=p.u8string(); return {s.begin(),s.end()}; }
Result scan(const fs::path& input, const std::atomic_bool* cancel, std::atomic<std::uint64_t>* progress, std::atomic<std::uint64_t>* progressBytes, std::atomic<std::uint64_t>* progressDirectories) {
 Result r; std::error_code ec; r.root=fs::weakly_canonical(input,ec); if(ec) r.root=fs::absolute(input,ec).lexically_normal();
 std::vector<fs::path> pending{r.root}; std::unordered_map<std::string,std::size_t> directories;
 while(!pending.empty()) {
  if(cancel && cancel->load()) { r.cancelled=true; break; }
  auto dir=std::move(pending.back()); pending.pop_back(); ++r.directories; if(progressDirectories)progressDirectories->store(r.directories);
  std::error_code err; fs::directory_iterator it(dir,fs::directory_options::skip_permission_denied,err), end;
  if(err) { ++r.skipped; continue; }
  while(it!=end) {
   if(cancel && cancel->load()) { r.cancelled=true; break; }
   const auto entry=*it; std::error_code e; bool reparse=entry.is_symlink(e);
#ifdef _WIN32
   auto attrs=GetFileAttributesW(entry.path().c_str()); if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) reparse=true;
#endif
   if(e || reparse) ++r.skipped;
   else if(entry.is_directory(e) && !e) { pending.push_back(entry.path()); directories.emplace(utf8(entry.path()),r.items.size()); r.items.push_back({entry.path(),true,0,0}); }
   else if(entry.is_regular_file(e) && !e) {
    auto n=entry.file_size(e); if(e) ++r.skipped; else { r.bytes+=n; ++r.files; if(progress) progress->store(r.files); if(progressBytes)progressBytes->store(r.bytes); r.items.push_back({entry.path(),false,n,1});
     for(auto p=entry.path().parent_path();;p=p.parent_path()) { auto found=directories.find(utf8(p)); if(found!=directories.end()){auto& d=r.items[found->second];d.bytes+=n;d.files+=1;} if(p==r.root||p==p.parent_path()) break; }
    }
   } else if(e) ++r.skipped;
   it.increment(err); if(err) { ++r.skipped; break; }
  }
 }
 return r;
}
int runCli(int argc,char** argv) {
 fs::path root,csvPath; std::size_t top=30;
 for(int i=1;i<argc;++i){std::string a=argv[i]; if(a=="--help"||a=="-h"){std::cout<<"Disk Inspector v0.3\nUso: disk-inspector --path CAMINHO [--top N] [--csv ARQUIVO]\n       disk-inspector --server [PORTA]\n";return 0;} if((a=="--path"||a=="--csv"||a=="--top")&&i+1>=argc)return 2; if(a=="--path")root=fs::u8path(argv[++i]);else if(a=="--csv")csvPath=fs::u8path(argv[++i]);else if(a=="--top"){try{top=std::stoul(argv[++i]);}catch(...){return 2;}}else return 2;}
 std::error_code ec; if(root.empty()||!fs::is_directory(root,ec)){std::cerr<<"Pasta invalida ou inacessivel.\n";return 2;} auto r=scan(root); std::ofstream csv; if(!csvPath.empty()){csv.open(csvPath,std::ios::binary);if(!csv)return 2;csv<<"caminho,tamanho_bytes,tipo,arquivos\n";}
 std::vector<Item> largest;for(const auto& x:r.items){if(csv)csv<<'"'<<utf8(x.path)<<"\","<<x.bytes<<','<<(x.directory?"pasta":"arquivo")<<','<<x.files<<'\n';if(!x.directory){largest.push_back(x);}} std::sort(largest.begin(),largest.end(),[](auto&a,auto&b){return a.bytes>b.bytes;});
 std::cout<<"Pasta: "<<utf8(r.root)<<"\nPastas visitadas: "<<r.directories<<"\nArquivos: "<<r.files<<"\nBytes: "<<r.bytes<<"\nIgnorados/erros: "<<r.skipped<<"\nMaiores arquivos:\n";for(std::size_t i=0;i<std::min(top,largest.size());++i)std::cout<<largest[i].bytes<<" | "<<utf8(largest[i].path)<<'\n';return 0;
}
}
