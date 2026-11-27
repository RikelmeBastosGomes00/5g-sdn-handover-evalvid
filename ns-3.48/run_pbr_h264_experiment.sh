#!/usr/bin/env bash
set -e

NS3_DIR="${NS3_DIR:-$(cd "$(dirname "$(readlink -f "$0")")" && pwd)}"
cd "$NS3_DIR"

BASE_OUT="results/experimento_pbr_h264"
REF_DIR="$BASE_OUT/referencia_h264"
mkdir -p "$BASE_OUT" "$REF_DIR"

CSV_FILE="$BASE_OUT/resultados_pbr_h264.csv"

echo "=================================================================="
echo "1. Preparando Video H.264 (QP=24, sem B-frames, GOP=30) e Referencia"
echo "=================================================================="
if [[ ! -f "akiyo_cif.y4m" ]]; then
    echo "[ERRO] Arquivo akiyo_cif.y4m nao encontrado em $(pwd)"
    exit 1
fi

rm -f akiyo_cif.mp4 akiyo_cif.yuv st_akiyo_cif.st psnr_ref.txt mos_ref.txt

# Codec exato solicitado
ffmpeg -y -v panic -i "akiyo_cif.y4m" -c:v libx264 -qp 24 -bf 0 -g 30 -an "akiyo_cif.mp4"

# Gera YUV de referencia (352x288 YUV420p) e Hint Track RTP
ffmpeg -y -v panic -i "akiyo_cif.mp4" -pix_fmt yuv420p "akiyo_cif.yuv"
MP4Box -hint -mtu 1024 -fps 30 "akiyo_cif.mp4" >/dev/null
mp4trace -f -s 127.0.0.1 12346 "akiyo_cif.mp4" > "st_akiyo_cif.st"

# Calcula e salva PSNR e MOS de referencia (sem perdas na rede)
psnr 352 288 420 "akiyo_cif.yuv" "akiyo_cif.yuv" > "psnr_ref.txt"
mos . "psnr_ref.txt" > "mos_ref.txt" 2>/dev/null || true

cp -f akiyo_cif.mp4 akiyo_cif.yuv st_akiyo_cif.st psnr_ref.txt mos_ref.txt "$REF_DIR/"

REF_PSNR=$(awk '{sum+=$1; n++} END {if(n>0) printf "%.2f", sum/n; else print "0.00"}' psnr_ref.txt)
echo "   [OK] H.264 salvo em $REF_DIR | PSNR Referencia = ${REF_PSNR} dB"

echo "=================================================================="
echo "2. Verificando binário de simulação"
echo "=================================================================="
PBR_BIN=$(find "$NS3_DIR/build/scratch" -maxdepth 2 -type f -executable -name "*sdn-5g-handover-pbr-h264*" 2>/dev/null | head -n 1)
if [[ -z "$PBR_BIN" ]]; then
    ./ns3 build scratch/sdn-5g-handover-pbr-h264
    PBR_BIN=$(find "$NS3_DIR/build/scratch" -maxdepth 2 -type f -executable -name "*sdn-5g-handover-pbr-h264*" 2>/dev/null | head -n 1)
fi

echo "modo,nUes,handovers,mos,psnr_db,atraso_ms,jitter_ms,banda_mbps,perda_pct" > "$CSV_FILE"

for MODO in 0 1; do
    NOME_MODO=$([[ "$MODO" -eq 1 ]] && echo "SDN-PBR" || echo "Roteamento_Padrao")
    for N in 1 5 10 15 20; do
        RUN_DIR="$BASE_OUT/rodada_${NOME_MODO}_ue${N}"
        mkdir -p "$RUN_DIR"

        echo "------------------------------------------------------------------"
        echo "Executando: Modo=${NOME_MODO} | UEs=${N} | Multi-Antena + Handover X2"
        echo "------------------------------------------------------------------"
        rm -f sd_dl_video rd_dl_video rec_dl* psnr_dl.txt mos_dl.txt mos_resumo.txt

        if [[ -n "$PBR_BIN" ]]; then
            "$PBR_BIN" --nUes="$N" --enablePbr="$MODO" > "$RUN_DIR/simulacao_ns3.log" 2>&1
        else
            ./ns3 run "sdn-5g-handover-pbr-h264 --nUes=$N --enablePbr=$MODO" > "$RUN_DIR/simulacao_ns3.log" 2>&1
        fi

        METRIC_LINE=$(grep "^METRICAS_PBR," "$RUN_DIR/simulacao_ns3.log" | tail -n 1)
        HANDOVERS=$(echo "$METRIC_LINE" | cut -d',' -f4)
        BANDA_MBPS=$(echo "$METRIC_LINE" | cut -d',' -f5)
        ATRASO_MS=$(echo "$METRIC_LINE" | cut -d',' -f6)
        JITTER_MS=$(echo "$METRIC_LINE" | cut -d',' -f7)
        PERDA_PCT=$(echo "$METRIC_LINE" | cut -d',' -f8)

        # Reconstroi video H.264 recebido no Downlink e converte para YUV
        ( etmp4 -p -0 sd_dl_video rd_dl_video st_akiyo_cif.st akiyo_cif.mp4 rec_dl ) >/dev/null 2>&1 || true
        if [[ -f "rec_dl.mp4" ]]; then
            ffmpeg -y -v panic -i "rec_dl.mp4" -pix_fmt yuv420p "rec_dl.yuv" || true
        fi

        PSNR_DB="0.00"
        MOS_VAL="1.00"

        if [[ -f "rec_dl.yuv" ]]; then
            # 1. Calcula e salva PSNR quadro a quadro
            psnr 352 288 420 "akiyo_cif.yuv" "rec_dl.yuv" > "psnr_dl.txt"
            PSNR_DB=$(awk '{sum+=$1; n++} END {if(n>0) printf "%.2f", sum/n; else print "0.00"}' psnr_dl.txt)

            # 2. Executa ferramenta mos do EvalVid e salva arquivo completo de MOS quadro a quadro
            mos . "psnr_dl.txt" > "mos_dl_raw.txt" 2>/dev/null || true

            # Converte e salva tabela completa de MOS por frame (escala ITU 1.0 a 5.0) em mos_dl.txt
            python3 - << 'PYEOF'
psnr_vals = []
with open("psnr_dl.txt", "r") as f:
    for line in f:
        parts = line.strip().split()
        if parts:
            try:
                psnr_vals.append(float(parts[0]))
            except ValueError:
                pass

def psnr_to_mos(p):
    # Mapeamento padrao EvalVid / ITU-T J.144 (PSNR -> MOS 1..5)
    if p >= 37.0:
        return 5.0
    elif p >= 31.0:
        return 4.0 + (p - 31.0) / 6.0
    elif p >= 25.0:
        return 3.0 + (p - 25.0) / 6.0
    elif p >= 20.0:
        return 2.0 + (p - 20.0) / 5.0
    else:
        return 1.0

mos_vals = [psnr_to_mos(p) for p in psnr_vals]
mean_mos = sum(mos_vals) / len(mos_vals) if mos_vals else 1.0

with open("mos_dl.txt", "w") as f:
    f.write("frame\tpsnr_db\tmos\n")
    for idx, (p, m) in enumerate(zip(psnr_vals, mos_vals)):
        f.write(f"{idx}\t{p:.4f}\t{m:.4f}\n")

with open("mos_resumo.txt", "w") as f:
    f.write(f"{mean_mos:.2f}\n")
PYEOF
            MOS_VAL=$(cat mos_resumo.txt)
        fi

        # Salva todos os artefatos da rodada na pasta dedicada
        cp -f sd_dl_video rd_dl_video "$RUN_DIR/" 2>/dev/null || true
        cp -f rec_dl.mp4 rec_dl.yuv psnr_dl.txt mos_dl.txt mos_dl_raw.txt mos_resumo.txt "$RUN_DIR/" 2>/dev/null || true

        echo "${NOME_MODO},${N},${HANDOVERS},${MOS_VAL},${PSNR_DB},${ATRASO_MS},${JITTER_MS},${BANDA_MBPS},${PERDA_PCT}" >> "$CSV_FILE"
        echo "   [OK] Salvo em $RUN_DIR | Handovers=${HANDOVERS} | MOS=${MOS_VAL} | PSNR=${PSNR_DB} dB | Atraso=${ATRASO_MS} ms | Banda=${BANDA_MBPS} Mbps"
    done
done

echo "=================================================================="
echo "3. Gerando Graficos de MOS, PSNR, Atraso e Banda"
echo "=================================================================="
python3 - << 'PYEOF'
import pandas as pd
import matplotlib.pyplot as plt

csv_path = "results/experimento_pbr_h264/resultados_pbr_h264.csv"
df = pd.read_csv(csv_path)

df_std = df[df["modo"] == "Roteamento_Padrao"]
df_pbr = df[df["modo"] == "SDN-PBR"]

fig, axes = plt.subplots(2, 2, figsize=(13.5, 9.2), dpi=150)
fig.suptitle(
    "Desempenho 5G SA + SDN-PBR (H.264 QP24, MIMO 2x4/2x2, Handover X2 - akiyo_cif)",
    fontsize=13, fontweight="bold", y=0.98
)

# 1. Grafico de MOS
ax = axes[0, 0]
ax.plot(df_std["nUes"], df_std["mos"], marker="o", linewidth=2.2, color="#d62728", label="Roteamento Padrao (Sem PBR)")
ax.plot(df_pbr["nUes"], df_pbr["mos"], marker="s", linewidth=2.2, color="#1f77b4", label="SDN Policy-Based Routing (PBR)")
ax.axhline(4.0, color="green", linestyle=":", linewidth=1.2, label="Limiar Boa QoE (MOS = 4.0)")
ax.set_title("Qualidade Subjetiva de Video (MOS)")
ax.set_xlabel("Numero de Usuarios (UEs)")
ax.set_ylabel("MOS Medio (Escala ITU 1 - 5)")
ax.set_ylim(1.0, 5.2)
ax.set_xticks(df_pbr["nUes"])
ax.grid(True, linestyle="--", alpha=0.5)
ax.legend(fontsize=8.5)

# 2. Grafico de PSNR
ax = axes[0, 1]
ax.plot(df_std["nUes"], df_std["psnr_db"], marker="o", linewidth=2.2, color="#d62728", label="Roteamento Padrao (Sem PBR)")
ax.plot(df_pbr["nUes"], df_pbr["psnr_db"], marker="s", linewidth=2.2, color="#1f77b4", label="SDN Policy-Based Routing (PBR)")
ax.axhline(37.0, color="green", linestyle=":", linewidth=1.2, label="Limiar Boa Qualidade (37 dB)")
ax.set_title("Qualidade Objetiva de Video (PSNR)")
ax.set_xlabel("Numero de Usuarios (UEs)")
ax.set_ylabel("PSNR Medio (dB)")
ax.set_xticks(df_pbr["nUes"])
ax.grid(True, linestyle="--", alpha=0.5)
ax.legend(fontsize=8.5)

# 3. Grafico de Atraso
ax = axes[1, 0]
ax.plot(df_std["nUes"], df_std["atraso_ms"], marker="o", linewidth=2.2, color="#d62728", label="Roteamento Padrao (Sem PBR)")
ax.plot(df_pbr["nUes"], df_pbr["atraso_ms"], marker="s", linewidth=2.2, color="#1f77b4", label="SDN Policy-Based Routing (PBR)")
ax.axhline(100.0, color="orange", linestyle="--", linewidth=1.2, label="Limiar PBR do Artigo (100 ms)")
ax.set_title("Atraso Medio Fim-a-Fim (Downlink)")
ax.set_xlabel("Numero de Usuarios (UEs)")
ax.set_ylabel("Atraso Medio (ms)")
ax.set_xticks(df_pbr["nUes"])
ax.grid(True, linestyle="--", alpha=0.5)
ax.legend(fontsize=8.5)

# 4. Grafico de Banda / Vazao
ax = axes[1, 1]
ax.plot(df_std["nUes"], df_std["banda_mbps"], marker="o", linewidth=2.2, color="#d62728", label="Roteamento Padrao (Sem PBR)")
ax.plot(df_pbr["nUes"], df_pbr["banda_mbps"], marker="s", linewidth=2.2, color="#1f77b4", label="SDN Policy-Based Routing (PBR)")
ax.set_title("Largura de Banda Efetiva Entregue (Vazao)")
ax.set_xlabel("Numero de Usuarios (UEs)")
ax.set_ylabel("Vazao Media (Mbps)")
ax.set_xticks(df_pbr["nUes"])
ax.grid(True, linestyle="--", alpha=0.5)
ax.legend(fontsize=8.5)

plt.tight_layout()
plt.savefig("results/experimento_pbr_h264/graficos_mos_psnr_atraso_banda.png", bbox_inches="tight")
plt.savefig("results/experimento_pbr_h264/graficos_mos_psnr_atraso_banda.pdf", bbox_inches="tight")
print("[OK] Todos os arquivos, MOS e graficos salvos em results/experimento_pbr_h264/")
PYEOF
