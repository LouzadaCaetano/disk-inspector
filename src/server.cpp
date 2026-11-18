#include "scanner.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <objbase.h>
using socket_t=SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t=int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif
namespace disk {
namespace fs = std::filesystem;
namespace {
std::mutex m; Result result; std::atomic_bool busy{false},cancelled{false}; std::atomic<std::uint64_t> progress{0},progressBytes{0},progressDirectories{0}; std::string state="idle",currentPath,localOrigin;
std::atomic_bool folderDialogOpen{false};
void closeSocket(socket_t s){
#ifdef _WIN32
 closesocket(s);
#else
 close(s);
#endif
}
std::string esc(const std::string& s){std::string o;for(unsigned char c:s){switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:if(c>=32)o+=char(c);}}return o;}
std::string jsonItems(const std::string& parent,std::size_t offset,std::size_t limit){std::ostringstream o;std::vector<Item> rows;std::uint64_t total=0;{std::lock_guard<std::mutex> l(m);for(const auto& x:result.items)if(x.path.parent_path()==fs::u8path(parent))rows.push_back(x);}
 std::sort(rows.begin(),rows.end(),[](const Item&a,const Item&b){if(a.directory!=b.directory)return a.directory>b.directory;return a.bytes>b.bytes;});for(const auto& x:rows)total+=x.bytes;const auto end=std::min(rows.size(),offset+limit);o<<"{\"items\":[";for(std::size_t i=std::min(offset,rows.size());i<end;++i){const auto& x=rows[i];if(i>offset)o<<',';o<<"{\"name\":\""<<esc(utf8(x.path.filename()))<<"\",\"path\":\""<<esc(utf8(x.path))<<"\",\"directory\":"<<(x.directory?"true":"false")<<",\"bytes\":"<<x.bytes<<",\"files\":"<<x.files<<'}';}o<<"],\"totalBytes\":"<<total<<",\"totalItems\":"<<rows.size()<<",\"offset\":"<<offset<<'}';return o.str();}
std::string decode(std::string s){std::string o;for(size_t i=0;i<s.size();++i){if(s[i]=='%'&&i+2<s.size()){try{o+=char(std::stoi(s.substr(i+1,2),nullptr,16));i+=2;}catch(...){o+=s[i];}}else if(s[i]=='+')o+=' ';else o+=s[i];}return o;}
std::string bodyPath(const std::string& b){auto p=b.find("\"path\"");if(p==std::string::npos)return {};p=b.find(':',p);p=b.find('"',p);if(p==std::string::npos)return {};std::string out;for(++p;p<b.size();++p){if(b[p]=='"')return out;if(b[p]=='\\'&&p+1<b.size()){char c=b[++p];switch(c){case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;default:out+=c;} }else out+=b[p];}return {};}
std::string jsonString(const std::string& value){return "\""+esc(value)+"\"";}
#ifdef _WIN32
std::string selectNativeFolder(){
 DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)||session==0)return "{\"error\":\"A seleção de pasta exige uma sessão interativa do Windows\"}";
 HWINSTA station=GetProcessWindowStation();USEROBJECTFLAGS stationFlags{};DWORD needed=0;if(!station||!GetUserObjectInformationW(station,UOI_FLAGS,&stationFlags,sizeof(stationFlags),&needed)||(stationFlags.dwFlags&WSF_VISIBLE)==0)return "{\"error\":\"A seleção de pasta exige uma sessão interativa do Windows\"}";
 const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);if(FAILED(initialized))return "{\"error\":\"Não foi possível inicializar o seletor de pastas do Windows\"}";
 IFileOpenDialog* dialog=nullptr;HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
 if(FAILED(hr)){CoUninitialize();return "{\"error\":\"Não foi possível abrir o seletor nativo de pastas\"}";}
 FILEOPENDIALOGOPTIONS options{};hr=dialog->GetOptions(&options);if(SUCCEEDED(hr))hr=dialog->SetOptions(static_cast<FILEOPENDIALOGOPTIONS>(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST));
 if(SUCCEEDED(hr)){HWND owner=GetForegroundWindow();if(owner&&!IsWindow(owner))owner=nullptr;hr=dialog->Show(owner);}
 if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED)){dialog->Release();CoUninitialize();return "{\"cancelled\":true}";}
 if(FAILED(hr)){dialog->Release();CoUninitialize();return "{\"error\":\"O seletor nativo de pastas falhou\"}";}
 IShellItem* item=nullptr;hr=dialog->GetResult(&item);dialog->Release();if(FAILED(hr)||!item){if(item)item->Release();CoUninitialize();return "{\"error\":\"Não foi possível obter o caminho da pasta selecionada\"}";}
 PWSTR widePath=nullptr;hr=item->GetDisplayName(SIGDN_FILESYSPATH,&widePath);item->Release();if(FAILED(hr)||!widePath){if(widePath)CoTaskMemFree(widePath);CoUninitialize();return "{\"error\":\"Não foi possível obter o caminho da pasta selecionada\"}";}
 const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,widePath,-1,nullptr,0,nullptr,nullptr);std::string path;if(bytes>1){path.resize(static_cast<std::size_t>(bytes));if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,widePath,-1,path.data(),bytes,nullptr,nullptr)>0)path.resize(static_cast<std::size_t>(bytes-1));else path.clear();}CoTaskMemFree(widePath);CoUninitialize();if(path.empty())return "{\"error\":\"Não foi possível converter o caminho da pasta selecionada\"}";return "{\"cancelled\":false,\"path\":"+jsonString(path)+"}";
}
#endif
std::string response(const std::string& body,int code=200,const char* type="application/json"){std::ostringstream o;o<<"HTTP/1.1 "<<code<<(code==200?" OK":" Bad Request")<<"\r\nContent-Type: "<<type<<"; charset=utf-8\r\nAccess-Control-Allow-Origin: http://127.0.0.1:5173\r\nVary: Origin\r\nX-Content-Type-Options: nosniff\r\nContent-Length: "<<body.size()<<"\r\nConnection: close\r\n\r\n"<<body;return o.str();}
std::string mime(const fs::path& p){auto e=p.extension().string();if(e==".html")return "text/html";if(e==".js")return "text/javascript";if(e==".css")return "text/css";if(e==".svg")return "image/svg+xml";if(e==".png")return "image/png";if(e==".ico")return "image/x-icon";return "application/octet-stream";}
}
int runServer(unsigned short port,bool openBrowser){
#ifdef _WIN32
 WSADATA wd{};if(WSAStartup(MAKEWORD(2,2),&wd))return 1;
#endif
 socket_t server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(server==INVALID_SOCKET)return 1;int reuse=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&reuse),sizeof(reuse));sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==SOCKET_ERROR||listen(server,16)==SOCKET_ERROR){closeSocket(server);return 1;}std::string site="http://127.0.0.1:"+std::to_string(port);localOrigin=site;std::cout<<"Disk Inspector em "<<site<<"\n";
#ifdef _WIN32
 if(openBrowser)ShellExecuteA(nullptr,"open",site.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
#endif
 for(;;){socket_t client=accept(server,nullptr,nullptr);if(client==INVALID_SOCKET)continue;std::thread([client,port]{char buf[8192];std::string req;std::size_t expected=std::string::npos;for(;;){int n=recv(client,buf,sizeof(buf),0);if(n<=0){closeSocket(client);return;}req.append(buf,n);auto split=req.find("\r\n\r\n");if(split!=std::string::npos){if(expected==std::string::npos){auto cl=req.find("\r\nContent-Length:");if(cl!=std::string::npos){auto end=req.find("\r\n",cl+2);try{expected=std::stoull(req.substr(cl+17,end-cl-17));}catch(...){expected=0;}}else expected=0;}if(req.size()>=split+4+expected)break;}if(req.size()>65535){closeSocket(client);return;}}auto h=req.find("\r\n\r\n");std::string head=req.substr(0,h),body=h==std::string::npos?"":req.substr(h+4);if(h==std::string::npos||req.size()>65535){auto packet=response("{\"error\":\"requisição muito grande ou inválida\"}",400);send(client,packet.data(),static_cast<int>(packet.size()),0);closeSocket(client);return;}auto firstSpace=head.find(' ');auto secondSpace=head.find(' ',firstSpace+1);std::string method=head.substr(0,firstSpace),url=head.substr(firstSpace+1,secondSpace-firstSpace-1);auto q=url.find('?');std::string path=url.substr(0,q),query=q==std::string::npos?"":url.substr(q+1);auto host=head.find("\r\nHost:");if(host!=std::string::npos){auto val=head.substr(host+7);val=val.substr(0,val.find("\r\n"));if(val!=" 127.0.0.1:"+std::to_string(port)&&val!=" localhost:"+std::to_string(port)){auto packet=response("{\"error\":\"host não permitido\"}",403);send(client,packet.data(),static_cast<int>(packet.size()),0);closeSocket(client);return;}}auto origin=head.find("\r\nOrigin:");if(origin!=std::string::npos){auto val=head.substr(origin+9);val=val.substr(0,val.find("\r\n"));if(val!=" http://127.0.0.1:5173"&&val!=" http://localhost:5173"&&val!=" "+localOrigin){auto packet=response("{\"error\":\"origem não permitida\"}",403);send(client,packet.data(),static_cast<int>(packet.size()),0);closeSocket(client);return;}}
 std::string out="{}";int code=200;
 if(method=="GET"&&path=="/api/status"){std::lock_guard<std::mutex> l(m);std::ostringstream o;o<<"{\"state\":\""<<state<<"\",\"files\":"<<progress.load()<<",\"directories\":"<<(busy.load()?progressDirectories.load():result.directories)<<",\"bytes\":"<<(busy.load()?progressBytes.load():result.bytes)<<",\"skipped\":"<<result.skipped<<",\"root\":\""<<esc(busy.load()?currentPath:utf8(result.root))<<"\"}";out=o.str();}
 else if(method=="GET"&&path=="/api/drives"){std::ostringstream o;o<<"{\"drives\":[";
#ifdef _WIN32
 DWORD mask=GetLogicalDrives();bool comma=false;for(int i=0;i<26;++i)if(mask&(1u<<i)){char d[4]={char('A'+i),':','\\',0};ULARGE_INTEGER free{},total{};GetDiskFreeSpaceExW(fs::path(d).c_str(),nullptr,&total,&free);{if(comma)o<<',';comma=true;o<<"{\"path\":\""<<esc(d)<<"\",\"total\":"<<total.QuadPart<<",\"free\":"<<free.QuadPart<<'}';}}
#endif
 o<<"]}";out=o.str();}
 else if(method=="POST"&&path=="/api/select-folder"){
  std::string hostValue=host==std::string::npos?"":head.substr(host+7),originValue=origin==std::string::npos?"":head.substr(origin+9);hostValue=hostValue.substr(0,hostValue.find("\r\n"));originValue=originValue.substr(0,originValue.find("\r\n"));
  const bool trustedHost=hostValue==" 127.0.0.1:"+std::to_string(port)||hostValue==" localhost:"+std::to_string(port);
  const bool trustedOrigin=originValue==" http://127.0.0.1:5173"||originValue==" http://localhost:5173"||originValue==" "+localOrigin;
  if(!trustedHost||!trustedOrigin){code=403;out="{\"error\":\"A seleção de pasta só pode ser solicitada pela interface local\"}";}
  else if(folderDialogOpen.exchange(true)){code=409;out="{\"error\":\"Já existe um seletor de pasta aberto\"}";}
  else{struct DialogLock{~DialogLock(){folderDialogOpen=false;}} guard;
#ifdef _WIN32
   try{out=selectNativeFolder();if(out.find("\"error\"")!=std::string::npos)code=500;}catch(...){code=500;out="{\"error\":\"O seletor nativo de pastas falhou inesperadamente\"}";}
#else
   code=501;out="{\"error\":\"O seletor nativo de pastas está disponível somente no Windows\"}";
#endif
  }
 }
 else if(method=="POST"&&path=="/api/scan"){auto root=bodyPath(body);std::error_code e;if(root.empty()||!fs::is_directory(fs::u8path(root),e)){code=400;out="{\"error\":\"Selecione uma pasta acessível\"}";}else if(busy.exchange(true)){code=409;out="{\"error\":\"Já existe uma análise em andamento\"}";}else{cancelled=false;progress=0;progressBytes=0;progressDirectories=0;{std::lock_guard<std::mutex> l(m);state="scanning";result={};currentPath=root;}std::thread([root]{auto r=scan(fs::u8path(root),&cancelled,&progress,&progressBytes,&progressDirectories);{std::lock_guard<std::mutex> l(m);result=std::move(r);state=result.cancelled?"cancelled":"complete";}busy=false;}).detach();out="{\"accepted\":true}";}}
 else if(method=="POST"&&path=="/api/cancel"){cancelled=true;out="{\"cancelling\":true}";}
 else if(method=="GET"&&path=="/api/items"){auto k=query.find("path=");auto param=[&](const std::string& name,std::size_t def){auto i=query.find(name+"=");if(i==std::string::npos)return def;try{return static_cast<std::size_t>(std::stoull(query.substr(i+name.size()+1)));}catch(...){return def;}};out=k==std::string::npos?"{\"items\":[]}":jsonItems(decode(query.substr(k+5,query.find('&',k)==std::string::npos?std::string::npos:query.find('&',k)-k-5)),param("offset",0),std::min<std::size_t>(param("limit",200),500));}
 else if(method=="GET"&&path=="/api/export.csv"){std::ostringstream c;c<<"path,bytes,type,files\r\n";std::lock_guard<std::mutex> l(m);for(const auto& x:result.items)c<<'"'<<esc(utf8(x.path))<<"\","<<x.bytes<<','<<(x.directory?"directory":"file")<<','<<x.files<<"\r\n";out=response(c.str(),200,"text/csv");send(client,out.data(),static_cast<int>(out.size()),0);closeSocket(client);return;}
 else if(method=="GET"&&path.rfind("/api/",0)!=0){std::string relative=path=="/"?"index.html":decode(path.substr(1));fs::path asset=fs::u8path(DISK_INSPECTOR_WEB_DIR)/fs::u8path(relative);bool safe=true;for(const auto& part:fs::u8path(relative))if(part=="..")safe=false;std::ifstream file(asset,std::ios::binary);if(safe&&file){std::ostringstream data;data<<file.rdbuf();out=response(data.str(),200,mime(asset).c_str());send(client,out.data(),static_cast<int>(out.size()),0);closeSocket(client);return;}code=404;out="Not found";}
 else {code=404;out="{\"error\":\"não encontrado\"}";}auto packet=response(out,code);send(client,packet.data(),static_cast<int>(packet.size()),0);closeSocket(client);}).detach();}
}
}







