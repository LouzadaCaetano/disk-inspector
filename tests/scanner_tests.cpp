#include "scanner.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs=std::filesystem;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool hasReason(const disk::Result& result,disk::SkipReason reason,const fs::path& path){for(const auto& entry:result.skippedEntries)if(entry.reason==reason&&entry.path==path)return true;return false;}
}

int main(){
 try {
  const auto root=fs::temp_directory_path()/fs::u8path(u8"Disk Inspector - teste çã & espaços");
  std::error_code ec;fs::remove_all(root,ec);fs::create_directories(root/"pasta vazia");
  const auto file=root/fs::u8path(u8"arquivo com ç e espaços.txt");{std::ofstream out(file,std::ios::binary);out<<"12345";}

  auto empty=disk::scan(root/"pasta vazia");
  require(empty.skipped==0&&empty.skippedEntries.empty(),"pasta sem ocorrências deve permanecer vazia");

  auto regular=disk::scan(root);
  require(regular.files==1&&regular.bytes==5,"arquivos válidos e tamanhos devem permanecer inalterados");
  bool unicodePath=false;for(const auto& item:regular.items)if(item.path==file)unicodePath=true;
  require(unicodePath,"caminhos com espaços e Unicode devem ser preservados");
  require(regular.skipped==regular.skippedErrors+regular.skippedIntentional,"contador agregado deve corresponder às categorias");
  require(regular.skippedEntries.size()+regular.skippedNotStored==regular.skipped,"limite deve contabilizar detalhes não armazenados");

  const auto missing=root/"pasta que não existe";auto missingResult=disk::scan(missing);
  require(missingResult.skipped==1&&missingResult.skippedErrors==1,"diretório inexistente deve gerar um erro registrado");
  require(hasReason(missingResult,disk::SkipReason::FileNotFound,missingResult.root),"erro de diretório inexistente deve manter caminho e motivo");
  require(missingResult.skippedEntries.front().errorCode!=0,"erro do sistema deve preservar o código nativo");

#ifndef _WIN32
  const auto denied=root/"pasta sem permissão";fs::create_directories(denied);fs::permissions(denied,fs::perms::none,ec);
  if(!ec){auto deniedResult=disk::scan(root);fs::permissions(denied,fs::perms::owner_all,ec);if(deniedResult.skipped){require(deniedResult.skippedErrors>0&&hasReason(deniedResult,disk::SkipReason::PermissionDenied,denied),"diretório negado deve registrar permissão e caminho");}else std::cout<<"AVISO: teste de permissão ignorado pelo sistema (processo privilegiado)\n";}
#endif

  const auto link=root/"link para pasta vazia";std::error_code linkError;fs::create_directory_symlink(root/"pasta vazia",link,linkError);
  if(!linkError){auto linked=disk::scan(root);require(linked.skippedIntentional==1,"link simbólico deve ser contado como exclusão intencional");require(hasReason(linked,disk::SkipReason::ReparsePoint,link),"link simbólico deve registrar caminho e motivo");require(linked.files==1&&linked.bytes==5,"link ignorado não deve alterar arquivos nem tamanhos");}
  else std::cout<<"AVISO: teste de link simbólico não executado: "<<linkError.message()<<'\n';

  std::atomic_bool cancel{true};auto cancelled=disk::scan(root,&cancel);
  require(cancelled.cancelled&&cancelled.skipped==0,"cancelamento não deve criar ocorrências artificiais");
  fs::remove_all(root,ec);
  std::cout<<"scanner-tests: aprovado\n";return 0;
 } catch(const std::exception& e){std::cerr<<"scanner-tests: falhou: "<<e.what()<<'\n';return 1;}
}
