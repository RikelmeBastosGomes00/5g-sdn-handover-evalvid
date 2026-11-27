# 🌐 ns3-5g-sdn-video

Plataforma de simulação e avaliação de qualidade de vídeo sobre rede **5G Standalone (5G SA)** integrada a redes definidas por software (**SDN - OpenFlow 1.3**), com suporte a **Policy-Based Routing (PBR)** e **Handover X2**.

O projeto utiliza **ns-3.48** + **5G-LENA NR (v5.1)** + **EvalVid** + **OFSwitch13 / BOFUSS**, totalmente configurado e containerizado em Docker para execução sem burocracia.

---

## 🏗️ Arquitetura e Componentes

| Componente | Versão / Detalhes | Função |
| :--- | :--- | :--- |
| **ns-3** | 3.48 (CMake/Ninja) | Simulador de eventos discretos de redes |
| **5G-LENA (NR)** | v5.1 (`contrib/nr`) | Simulação de 5G NR SA (gNBs, UEs, canais 3GPP UMa/UMi, beamforming, Handover X2) |
| **EvalVid** | Módulo (`contrib/evalvid`) + binários i386 | Avaliação de QoE/QoS de vídeo (PSNR quadro a quadro, MOS, perdas e atrasos) |
| **OFSwitch13** | Módulo (`contrib/ofswitch13`) | Switch OpenFlow 1.3 e controlador SDN integrados ao ns-3 |
| **BOFUSS / ofsoftswitch13** | Datapath OpenFlow 1.3 | Biblioteca de encaminhamento OpenFlow em user-space |
| **Codecs & Ferramentas** | FFmpeg, GPAC (MP4Box) | Codificação MPEG-4 / H.264, geração de Hint Tracks RTP |

---

## 🎬 Cenários de Simulação Disponíveis

### 1. `sdn-5g-handover-evalvid` (Pipeline Completo MPEG-4)
* **Objetivo:** Avaliar o impacto da mobilidade e múltiplos handovers X2 na qualidade de vídeo do usuário móvel e no servidor.
* **Topologia:** 2 gNBs com cobertura celular adjacente, 1 UE em movimento contínuo cruzando as células, Switches SDN OpenFlow e Servidor Remoto.
* **Métricas:** PSNR quadro a quadro, MOS (escala ITU 1 a 5), atraso fim-a-fim, jitter e vazão via FlowMonitor.
* **Parâmetros configuráveis:**
  ```text
  --simTime=<segundos>          (padrão: 12.0)
  --ueSpeed=<m/s>               (padrão: 25.0)
  --centralFrequency=<Hz>       (padrão: 3.5e9 -> Banda n78)
  --bandwidth=<Hz>              (padrão: 20e6 -> 20 MHz)
  --numerology=<0,1,2>          (padrão: 1 -> SCS 30 kHz)
  --traceFile=<arquivo.st>      (padrão: st_akiyo_cif.st)
  ```

### 2. `sdn-5g-handover-pbr-h264` (Experimento SDN Policy-Based Routing)
* **Objetivo:** Comparar o roteamento tradicional vs. roteamento SDN baseado em políticas (PBR), desviando tráfego sensível a atraso quando filas/latência ultrapassam limiares pré-definidos (ex: 100 ms).
* **Topologia:** 3 gNBs, múltiplos UEs concorrentes (1 a 20 UEs com tráfego de fundo em paralelo), vídeo H.264 QP24, MIMO 2x4 e 2x2.
* **Saída:** Tabela CSV comparativa e gráficos automáticos de MOS, PSNR, Atraso e Vazão gerados via Matplotlib/Pandas.
* **Parâmetros configuráveis:**
  ```text
  --nUes=<quantidade>           (padrão: 10)
  --enablePbr=<0 ou 1>          (0: Roteamento Padrão, 1: SDN-PBR Ativo)
  --simTime=<segundos>          (padrão: 12.0)
  --traceFile=<arquivo.st>      (padrão: st_akiyo_cif.st)
  ```

---

## 🚀 Guia Rápido: Como Rodar com Docker (Zero Burocracia)

### 📋 Pré-requisitos
* [Docker Desktop](https://www.docker.com/products/docker-desktop/) (Windows ou macOS) ou **Docker Engine** (Linux).
* Docker Compose habilitado (já vem embutido no Docker Desktop).
* Cerca de 5 GB de espaço livre em disco.

---

### Passo 1: Clonar o repositório
```bash
git clone <URL_DO_REPOSITORIO>
cd ns3-5g-sdn-video
```

### Passo 2: Construir a imagem (Apenas na primeira vez)
```bash
make build
# ou: docker compose build ns3-sim
```
> ⏱️ **Nota:** O primeiro build compila o ns-3 e seus módulos C++ (leva ~15 a 25 minutos dependendo do seu processador). Os builds seguintes são instantâneos via cache.

---

### Passo 3: Executar as Simulações

Você pode rodar diretamente da raiz do projeto usando qualquer um dos métodos abaixo:

#### Método A: Atalhos Diretos na Raiz (Mais Fácil)
```bash
# Executa o pipeline completo do EvalVid (prepara vídeo, simula, calcula PSNR e MOS):
./run_pipeline.sh

# Executa o experimento PBR completo com gráficos:
./run_pbr_h264_experiment.sh
```

#### Método B: Via Makefile
```bash
make pipeline      # Executa o pipeline do EvalVid
make experiment    # Executa o experimento PBR H.264
make run           # Abre um terminal bash interativo dentro do container
make dev           # Abre o container de desenvolvimento (com compilador)
make help          # Exibe todos os comandos disponíveis
```

#### Método C: Via Docker Compose com Parâmetros Customizados
```bash
# Rodar simulando 5 segundos a 30 m/s:
docker compose run --rm ns3-sim ./ns3 run "sdn-5g-handover-evalvid --simTime=5.0 --ueSpeed=30.0"

# Rodar experimento com 5 UEs e SDN-PBR ativado:
docker compose run --rm ns3-sim ./ns3 run "sdn-5g-handover-pbr-h264 --nUes=5 --enablePbr=1"
```

---

## 📂 Onde Ficam os Resultados?

Todos os resultados gerados são salvos automaticamente na pasta `./results/` na raiz do seu projeto (ela é montada como um volume Docker permanente, então nada é perdido quando o container fecha).

Exemplo de estrutura gerada em `./results/`:
```text
results/
├── sdn-5g-handover-evalvid_akiyo_<timestamp>/
│   ├── rec_dl.mp4               # Vídeo reconstruído recebido pelo UE móvel
│   ├── rec_ul.mp4               # Vídeo reconstruído recebido pelo Servidor
│   ├── psnr_dl.txt              # PSNR quadro a quadro (Downlink)
│   ├── psnr_ul.txt              # PSNR quadro a quadro (Uplink)
│   ├── delay_rec_dl.txt         # Atraso por quadro recebido
│   ├── loss_rec_dl.txt          # Relatório de perdas de pacotes
│   ├── simulacao_ns3.log        # Log completo de conexões, handovers e FlowMonitor
│   └── st_akiyo_cif.st          # Rastreamento de transmissão de quadros
│
└── experimento_pbr_h264/
    ├── resultados_pbr_h264.csv                  # Tabela consolidada de métricas
    ├── graficos_mos_psnr_atraso_banda.png       # Gráfico comparativo de desempenho
    ├── graficos_mos_psnr_atraso_banda.pdf       # Gráfico em alta resolução para artigo/relatório
    └── rodada_SDN-PBR_ue*/                      # Dados detalhados por rodada
```

---

## 🛠️ Como Adicionar Novos Arquivos ou Modificar Cenários

O projeto conta com dois ambientes configurados no `docker-compose.yml`:
* **`ns3-sim` (Runtime):** Imagem leve (apenas 2.4 GB / 600 MB compactada), com os binários pré-compilados e pronta para rodar simulações em segundos.
* **`ns3-dev` (Desenvolvimento):** Imagem completa com `g++`, `cmake`, `ninja` e todo o código-fonte montado em tempo real.

### Criando um novo cenário C++ (`.cc`):
1. Crie seu arquivo em `ns-3.48/scratch/meu-cenario.cc`.
2. Abra o container de desenvolvimento:
   ```bash
   make dev
   # ou: docker compose run --rm ns3-dev bash
   ```
3. Dentro do container, compile e execute:
   ```bash
   ./ns3 build meu-cenario
   ./ns3 run meu-cenario
   ```
   > Como a pasta `scratch/` é montada como volume, qualquer alteração feita no seu VS Code/editor no Windows é refletida imediatamente no container!

---

## ☁️ Dica de Ouro: Compartilhar Imagem Pré-Compilada com o Grupo

Para que seus amigos **não precisem esperar 20 minutos compilando** no primeiro uso, você pode subir a imagem já construída para o **GitHub Container Registry (GHCR)** ou Docker Hub:

```bash
# 1. Faça login no GHCR (usando um Personal Access Token do GitHub):
echo $CR_PAT | docker login ghcr.io -u SEU_USUARIO --password-stdin

# 2. Marque a imagem com a tag do repositório:
docker tag ns3-5g-sdn-video:latest ghcr.io/SEU_USUARIO/ns3-5g-sdn-video:latest

# 3. Envie a imagem para a nuvem:
docker push ghcr.io/SEU_USUARIO/ns3-5g-sdn-video:latest
```

Depois disso, seus amigos só precisarão clonar o repositório e rodar:
```bash
docker pull ghcr.io/SEU_USUARIO/ns3-5g-sdn-video:latest
./run_pipeline.sh
```
A simulação iniciará em segundos, sem compilação local!

---

## 📁 Estrutura Completa de Diretórios

```text
ns3-5g-sdn-video/
├── Dockerfile                      # Receita multi-stage (deps -> build -> runtime)
├── docker-compose.yml              # Definição dos containers de simulação e dev
├── Makefile                        # Comandos simplificados (make build, run, pipeline)
├── entrypoint.sh                   # Script de inicialização e roteador de comandos
├── run_pipeline.sh                 # Atalho na raiz para o pipeline EvalVid
├── run_pbr_h264_experiment.sh      # Atalho na raiz para o experimento SDN-PBR
├── .dockerignore                   # Otimização do contexto de build do Docker
├── results/                        # Diretório onde os vídeos e métricas são salvos
├── bofuss/                         # Biblioteca de datapath OpenFlow 1.3
├── ofsoftswitch13/                 # Implementação de switch OpenFlow 1.3
└── ns-3.48/
    ├── scratch/                    # Cenários de simulação em C++
    │   ├── sdn-5g-handover-evalvid.cc
    │   └── sdn-5g-handover-pbr-h264.cc
    ├── contrib/
    │   ├── nr/                     # Módulo 5G-LENA NR v5.1
    │   ├── evalvid/                # Módulo EvalVid Client/Server
    │   └── ofswitch13/             # Módulo de integração SDN OpenFlow 1.3
    ├── etmp4, mp4trace, psnr, mos  # Ferramentas nativas do EvalVid (i386)
    ├── akiyo_cif.y4m, .mp4         # Vídeos de teste para transmissão
    └── ns3                         # Script de controle do ns-3
```

---

## 📜 Licença e Créditos

* **ns-3:** Licenciado sob a [GNU GPLv2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
* **5G-LENA:** Desenvolvido pelo CTTC sob licença GPLv2.
* **OFSwitch13 & BOFUSS:** Licenciados sob BSD / GPL.
* **EvalVid:** Desenvolvido pela Universidade de Berlim (TKN).
