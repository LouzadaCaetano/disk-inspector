# Disk Inspector

Analisador de espaço em disco em **C++17**, sem dependências externas. Projeto inicial para evoluir para uma interface com Qt e visualização gráfica.

## Funcionalidades (v0.1)

- Analisa recursivamente uma pasta ou unidade informada.
- Mostra o tamanho agregado das subpastas imediatamente abaixo da raiz.
- Mostra os N maiores arquivos encontrados.
- Exporta **todos os arquivos acessíveis** para CSV (opcional).
- Opera somente em modo leitura nos diretórios analisados: **não exclui arquivos**.
- Não segue links simbólicos/junções, evitando ciclos e boa parte da dupla contagem.
- Ignora permissões negadas e relata contagem aproximada de entradas ignoradas/erros.

## Requisitos

- Windows 11 com Visual Studio Build Tools 2022+ (compilador C++) e CMake; ou Linux/macOS com compilador C++17 e CMake.
- Não exige Qt nesta versão.

## Compilar no PowerShell (Windows)

Abra um terminal com CMake e compilador C++ disponíveis:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

O executável com Visual Studio geralmente estará em `build\Release\disk-inspector.exe`.

## Executar

```powershell
.\build\Release\disk-inspector.exe --path "C:\Users" --top 30 --csv "relatorio.csv"
```

Para analisar a unidade inteira, rode o PowerShell como administrador e use:

```powershell
.\build\Release\disk-inspector.exe --path "C:\" --top 50 --csv "relatorio.csv"
```

**Nota:** dependendo do gerador CMake, o executável pode estar diretamente em `build\`.

Também funciona em Linux:

```bash
cmake -S . -B build && cmake --build build
./build/disk-inspector --path "$HOME" --top 20 --csv relatorio.csv
```

## Como ler o resultado

- `GiB` = gibibytes (1 GiB = 1.073.741.824 bytes); difere ligeiramente do GB decimal.
- As pastas listadas são as **subpastas imediatas da raiz**; o tamanho inclui os arquivos em subpastas internas.
- Arquivos diretamente na raiz estão no total geral, mas não são listados como subpasta.
- Arquivos de sistema, protegidos, bloqueados, dados de nuvem e metadados podem não ser contabilizados. Portanto a soma **não substitui** a leitura de espaço usado pelo Windows.
- Não apague arquivos do Windows, Program Files ou AppData sem entender sua função.
- O CSV pode conter nomes de usuário e caminhos sensíveis: ele é ignorado pelo Git por padrão. Revise antes de publicá-lo.

## Próximas etapas

1. Exibir todas as pastas em árvore e permitir navegação interativa.
2. Adicionar barra de progresso e cancelamento de análise.
3. Criar uma GUI com Qt Widgets.
4. Gerar treemap por tamanho.
5. Melhorar tratamento de erros, testes e desempenho em unidades NTFS grandes.

## Licença

MIT.
