#!/bin/bash
set -e

echo "======================================================"
echo "  ns3-5g-sdn-video Container"
echo "  ns-3.48 + 5G-LENA NR + EvalVid + OFSwitch13"
echo "======================================================"
echo ""
echo "Comandos disponíveis:"
echo "  ./ns3 run sdn-5g-handover-evalvid       # Simulação EvalVid básica"
echo "  ./ns3 run sdn-5g-handover-pbr-h264      # Simulação PBR H.264"
echo "  ./run_pipeline.sh                        # Pipeline completo EvalVid"
echo "  ./run_pbr_h264_experiment.sh             # Experimento PBR completo"
echo ""
echo "Resultados salvos em: /sim/ns-3.48/results/"
echo "  (montado como volume em ./results/ no host)"
echo "======================================================"
echo ""

# Se o usuário tentar rodar ./ns3 run <cenario>, executa diretamente o binário compilado
if [ "$1" = "./ns3" ] || [ "$1" = "ns3" ]; then
    if [ "$2" = "run" ]; then
        TARGET="$3"
        shift 3 || shift 2
        # Extrai o nome do binário e os argumentos
        BIN_NAME=$(echo "$TARGET" | awk '{print $1}')
        EXTRA_ARGS=$(echo "$TARGET" | cut -s -d' ' -f2-)
        FOUND_BIN=$(find /sim/ns-3.48/build/scratch -name "*${BIN_NAME}*" -type f -executable 2>/dev/null | head -1)
        if [ -n "$FOUND_BIN" ]; then
            echo "[Container] Executando binário pré-compilado: $FOUND_BIN $EXTRA_ARGS $@"
            exec "$FOUND_BIN" $EXTRA_ARGS "$@"
        fi
    fi
fi

exec "$@"
