# Disk Inspector v0.3

Analisador local de armazenamento com scanner em C++17, API HTTP em loopback e interface React + TypeScript + Vite. O scanner também continua disponível pela CLI. A aplicação é somente de leitura e não envia dados para serviços externos.

## Arquitetura

- `src/scanner.cpp` e `src/scanner.h`: percorrem pastas recursivamente, acumulam tamanho e contagem, pulam links simbólicos e reparse points do Windows e preservam erros parciais.
- `src/server.cpp`: API HTTP nativa com sockets, vinculada exclusivamente a `127.0.0.1`; porta padrão 8765. A API mantém o resultado em memória e fornece listagem paginada de itens.
- `frontend/`: React, TypeScript, Vite, Tailwind CSS e Lucide. A tabela e os gráficos usam os resultados da API.
- `src/main.cpp`: seleciona a CLI ou o modo servidor.

## Dependências de desenvolvimento

- Windows 11, Visual Studio 2022 ou mais recente com componente de compilação C++, Windows SDK e CMake.
- Node.js 20 ou mais recente e npm para instalar e compilar a interface.
- O backend não baixa bibliotecas C++: usa somente a biblioteca padrão e Winsock no Windows.

## Compilar e executar no Windows

Compile primeiro a interface e os assets locais:

```powershell
cd frontend
npm install
npm run build
cd ..
```

Em um terminal com CMake e compilador C++ no `PATH`:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

No modo de produção, o CMake copia os assets compilados para `build/web`. Inicie o aplicativo:

```powershell
.\build\Release\disk-inspector.exe --server
```

O aplicativo abre o navegador padrão em `http://127.0.0.1:8765` e serve o frontend e a API pela mesma origem. O backend também aceita uma porta alternativa:

```powershell
.\build\Release\disk-inspector.exe --server 8766
```

### Desenvolvimento separado

Inicie o backend sem abrir a página de produção:

```powershell
.\build\Release\disk-inspector.exe --server --no-browser
```

Em outro terminal, inicie o frontend:

```powershell
cd frontend
npm run dev
```

Abra `http://127.0.0.1:5173`. O proxy Vite direciona `/api` para `127.0.0.1:8765`.

## CLI

```powershell
.\build\Release\disk-inspector.exe --path "C:\Users" --top 30 --csv "relatorio.csv"
```

Opções: `--help`, `--path`, `--top` e `--csv`.

## API local

| Método e rota | Uso |
|---|---|
| `GET /api/drives` | Unidades montadas, capacidade e espaço livre (Windows) |
| `POST /api/scan` | Inicia uma análise; corpo JSON: `{"path":"C:\\\\Users"}` |
| `GET /api/status` | Estado e contadores da análise |
| `GET /api/items?path=...&offset=0&limit=200` | Itens diretos da pasta, paginados |
| `POST /api/cancel` | Solicita cancelamento |
| `GET /api/export.csv` | Exporta os resultados em memória |

A API rejeita cabeçalhos `Origin` que não sejam os endereços de desenvolvimento locais e valida `Host`; ela não habilita CORS geral. Não use um proxy remoto para expor a porta.

## Limitações conhecidas

- Um instalador que dispense Node.js e CMake no computador final ainda não está preparado; compile os assets e o executável antes da distribuição.
- Para escolher uma pasta, a interface solicita o caminho local completo; o navegador não pode abrir um seletor nativo de pastas arbitrárias sem uma ponte local dedicada.
- A listagem HTTP é paginada em lotes de até 500 itens. A interface mostra paginação. A consulta ainda filtra os itens em memória por pasta e pode usar memória proporcional à quantidade de entradas da análise.
- A análise mede arquivos acessíveis, não o uso reportado pelo sistema operacional. Arquivos protegidos, metadados e dados em nuvem podem não ser contabilizados.
- CSV e resultados podem conter caminhos pessoais. Os CSVs são ignorados pelo Git.

## Licença

MIT. Consulte [LICENSE](LICENSE).
