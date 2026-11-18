#include "scanner.h"
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <system_error>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace disk {
std::string utf8(const fs::path& p) { auto s=p.u8string(); return {s.begin(),s.end()}; }
namespace {
#ifndef DISK_INSPECTOR_MAX_SKIPPED_ENTRIES
#define DISK_INSPECTOR_MAX_SKIPPED_ENTRIES 10000
#endif
constexpr std::size_t maxSkippedEntries=DISK_INSPECTOR_MAX_SKIPPED_ENTRIES;
SkipReason classifyError(const std::error_code& error,SkipReason fallback) {
 if(error==std::errc::permission_denied)return SkipReason::PermissionDenied;
 if(error==std::errc::no_such_file_or_directory||error==std::errc::not_a_directory)return SkipReason::FileNotFound;
 return fallback;
}
void recordSkipped(Result& result,const fs::path& path,SkipCategory category,SkipReason reason,const std::error_code& error={}) noexcept {
 ++result.skipped;
 if(category==SkipCategory::Intentional)++result.skippedIntentional;else ++result.skippedErrors;
 if(result.skippedEntries.size()>=maxSkippedEntries){++result.skippedNotStored;return;}
 try {
  SkippedEntry entry;entry.path=path;entry.category=category;entry.reason=reason;entry.errorCode=error?error.value():0;entry.occurredAt=std::chrono::system_clock::now();
  entry.message=category==SkipCategory::Intentional?"Link simbólico ou ponto de nova análise ignorado pela política do scanner":error?error.message():"Falha sem código de erro disponível";
  result.skippedEntries.push_back(std::move(entry));
 } catch(...) { ++result.skippedNotStored; }
}
}
Result scan(const fs::path& input, const std::atomic_bool* cancel, std::atomic<std::uint64_t>* progress, std::atomic<std::uint64_t>* progressBytes, std::atomic<std::uint64_t>* progressDirectories) {
 Result r; std::error_code ec; r.root=fs::weakly_canonical(input,ec); if(ec) r.root=fs::absolute(input,ec).lexically_normal();
 std::vector<fs::path> pending{r.root}; std::unordered_map<std::string,std::size_t> directories;
 while(!pending.empty()) {
  if(cancel && cancel->load()) { r.cancelled=true; break; }
  auto dir=std::move(pending.back()); pending.pop_back(); ++r.directories; if(progressDirectories)progressDirectories->store(r.directories);
  std::error_code err; fs::directory_iterator it(dir,err), end;
  if(err) { recordSkipped(r,dir,SkipCategory::Error,classifyError(err,SkipReason::DirectoryReadError),err); continue; }
  while(it!=end) {
   if(cancel && cancel->load()) { r.cancelled=true; break; }
   const auto entry=*it; const auto entryPath=entry.path(); std::error_code e; bool reparse=entry.is_symlink(e);
   if(e) recordSkipped(r,entryPath,SkipCategory::Error,classifyError(e,SkipReason::MetadataError),e);
#ifdef _WIN32
   if(!e){auto attrs=GetFileAttributesW(entryPath.c_str()); if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) reparse=true;}
#endif
   if(!e&&reparse) recordSkipped(r,entryPath,SkipCategory::Intentional,SkipReason::ReparsePoint);
   else if(!e) {
    bool isDirectory=entry.is_directory(e);
    if(e) recordSkipped(r,entryPath,SkipCategory::Error,classifyError(e,SkipReason::MetadataError),e);
    else if(isDirectory) { pending.push_back(entryPath); directories.emplace(utf8(entryPath),r.items.size()); r.items.push_back({entryPath,true,0,0}); }
    else {
     bool isRegularFile=entry.is_regular_file(e);
     if(e) recordSkipped(r,entryPath,SkipCategory::Error,classifyError(e,SkipReason::MetadataError),e);
     else if(isRegularFile) {
      auto n=entry.file_size(e); if(e) recordSkipped(r,entryPath,SkipCategory::Error,classifyError(e,SkipReason::FileSizeError),e); else { r.bytes+=n; ++r.files; if(progress) progress->store(r.files); if(progressBytes)progressBytes->store(r.bytes); r.items.push_back({entryPath,false,n,1});
       for(auto p=entryPath.parent_path();;p=p.parent_path()) { auto found=directories.find(utf8(p)); if(found!=directories.end()){auto& d=r.items[found->second];d.bytes+=n;d.files+=1;} if(p==r.root||p==p.parent_path()) break; }
      }
     }
    }
   }
   it.increment(err); if(err) { recordSkipped(r,dir,SkipCategory::Error,classifyError(err,SkipReason::DirectoryReadError),err); break; }
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
