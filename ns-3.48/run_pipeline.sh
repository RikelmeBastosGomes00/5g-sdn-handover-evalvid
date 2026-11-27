#!/usr/bin/env bash
set -euo pipefail

# Auto-detecta o diretório do ns-3.48 (funciona tanto no host quanto no container)
NS3_DIR="${NS3_DIR:-$(cd "$(dirname "$(readlink -f "$0")")" && pwd)}"
SCENARIO="sdn-5g-handover-evalvid"

SIM_TIME="${SIM_TIME:-12.0}"
UE_SPEED="${UE_SPEED:-25.0}"
FREQ="${FREQ:-3.5e9}"
BW="${BW:-20e6}"
NUMEROLOGY="${NUMEROLOGY:-1}"
PACKET_SIZE="${PACKET_SIZE:-1024}"

VIDEO_Y4M="${VIDEO_Y4M:-$NS3_DIR/akiyo_cif.y4m}"
if [[ ! -f "$VIDEO_Y4M" ]]; then
    echo "[ERRO] Arquivo '$VIDEO_Y4M' não encontrado."
    exit 1
fi

VIDEO_WIDTH=352
VIDEO_HEIGHT=288
VIDEO_FPS=30

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUT_DIR="$NS3_DIR/results/${SCENARIO}_akiyo_${TIMESTAMP}"
mkdir -p "$OUT_DIR"

echo "=================================================================="
echo " 1. Preparando akiyo_cif.y4m (YUV -> MP4 -> Hint Track -> Trace)"
echo "=================================================================="
cp "$VIDEO_Y4M" "$OUT_DIR/akiyo_cif.y4m"
cd "$OUT_DIR"

# YUV bruto original e MP4 codificado sem perdas de rede
ffmpeg -y -v panic -i "akiyo_cif.y4m" -pix_fmt yuv420p "akiyo_cif.yuv"
ffmpeg -y -v panic -i "akiyo_cif.y4m" -c:v mpeg4 -q:v 4 -bf 0 -g 30 -an "akiyo_cif.mp4"
MP4Box -hint -mtu "$PACKET_SIZE" "akiyo_cif.mp4" >/dev/null 2>&1
mp4trace -f -s 10.1.1.2 4000 "$PACKET_SIZE" "akiyo_cif.mp4" > "st_akiyo_cif.st"

# Gera PSNR de referencia (video codificado sem perdas vs YUV bruto) para o calculo de MOS
ffmpeg -y -v panic -i "akiyo_cif.mp4" -pix_fmt yuv420p "akiyo_cif_enc.yuv"
psnr "$VIDEO_WIDTH" "$VIDEO_HEIGHT" 420 "akiyo_cif.yuv" "akiyo_cif_enc.yuv" > "psnr_ref.txt"
echo "   [OK] Trace gerado: st_akiyo_cif.st ($(wc -l < st_akiyo_cif.st) frames) + psnr_ref.txt"

echo ""
echo "=================================================================="
echo " 2. Executando simulação no ns-3.48"
echo "=================================================================="
# Localiza o binário pré-compilado; se não existir, tenta compilar
SIM_BIN=$(find "$NS3_DIR/build/scratch" -maxdepth 2 -type f -executable -name "*${SCENARIO}*" 2>/dev/null | head -n 1)
if [[ -z "$SIM_BIN" ]]; then
    cd "$NS3_DIR"
    ./ns3 build "scratch_${SCENARIO}" 2>/dev/null || ./ns3 build
    SIM_BIN=$(find "$NS3_DIR/build/scratch" -maxdepth 2 -type f -executable -name "*${SCENARIO}*" | head -n 1)
fi
cd "$OUT_DIR"

"$SIM_BIN" \
    --simTime="$SIM_TIME" \
    --ueSpeed="$UE_SPEED" \
    --centralFrequency="$FREQ" \
    --bandwidth="$BW" \
    --numerology="$NUMEROLOGY" \
    --traceFile="st_akiyo_cif.st" | tee "$OUT_DIR/simulacao_ns3.log"

echo ""
echo "=================================================================="
echo " 3. Processamento EvalVid (Reconstrução + PSNR + MOS)"
echo "=================================================================="

processar_fluxo_evalvid() {
    local DIR="$1"
    local LABEL="$2"
    local SD_FILE="sd_${DIR}_video"
    local RD_FILE="rd_${DIR}_video"
    local OUT_PREFIX="rec_${DIR}"

    echo "--- Processando $LABEL ---"

    if [[ ! -s "$SD_FILE" || ! -s "$RD_FILE" ]]; then
        echo "   [AVISO] $SD_FILE ou $RD_FILE vazio/ausente. Pulando $LABEL."
        return
    fi

    bash -c 'etmp4 -p -0 "$1" "$2" "st_akiyo_cif.st" "akiyo_cif.mp4" "$3" > "etmp4_${4}.log" 2>&1' _ \
        "$SD_FILE" "$RD_FILE" "$OUT_PREFIX" "$DIR" 2>/dev/null || true

    if [[ ! -f "${OUT_PREFIX}.mp4" ]]; then
        echo "   [AVISO] ${OUT_PREFIX}.mp4 não foi gerado. Verifique etmp4_${DIR}.log."
        return
    fi

    ffmpeg -y -v panic -i "${OUT_PREFIX}.mp4" -pix_fmt yuv420p "${OUT_PREFIX}.yuv"
    psnr "$VIDEO_WIDTH" "$VIDEO_HEIGHT" 420 "akiyo_cif.yuv" "${OUT_PREFIX}.yuv" > "psnr_${DIR}.txt"

    AVG_PSNR=$(awk '{sum+=$1; n++} END {if (n>0) printf "%.2f", sum/n; else print "N/A"}' "psnr_${DIR}.txt")
    echo "   [OK] $LABEL -> Vídeo: ${OUT_PREFIX}.mp4 | PSNR Médio: ${AVG_PSNR} dB"
}

processar_fluxo_evalvid "dl" "DOWNLINK (RemoteHost -> UE)"
processar_fluxo_evalvid "ul" "UPLINK (UE -> RemoteHost)"

# Calcula MOS comparando psnr_dl.txt e psnr_ul.txt contra psnr_ref.txt no diretorio atual
echo "--- Calculando MOS (Downlink e Uplink) ---"
mos . "psnr_ref.txt" || true

# Remove arquivos YUV brutos para economizar disco
rm -f "$OUT_DIR/akiyo_cif.yuv" "$OUT_DIR/akiyo_cif_enc.yuv" "$OUT_DIR/rec_dl.yuv" "$OUT_DIR/rec_ul.yuv"

echo ""
echo "=================================================================="
echo " 4. Concluído! Resultados em: $OUT_DIR"
echo "=================================================================="
ls -lh "$OUT_DIR"
