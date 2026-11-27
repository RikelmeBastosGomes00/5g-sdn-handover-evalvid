#!/usr/bin/env bash
# Atalho de conveniência para executar o experimento PBR dentro do container Docker
cd "$(dirname "$(readlink -f "$0")")"
docker compose run --rm ns3-sim ./run_pbr_h264_experiment.sh "$@"
