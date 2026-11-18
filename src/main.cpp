#include "scanner.h"
#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
 if(argc>1 && std::string(argv[1])=="--server") { unsigned long p=8765; bool open=true; for(int i=2;i<argc;++i){std::string arg=argv[i];if(arg=="--no-browser")open=false;else try{std::size_t used=0;p=std::stoul(arg,&used);if(used!=arg.size())throw std::invalid_argument("port");}catch(...){std::cerr<<"Porta invalida\n";return 2;}} if(p<1||p>65535) return 2; return disk::runServer(static_cast<unsigned short>(p),open); }
 return disk::runCli(argc,argv);
}
