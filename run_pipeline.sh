#!/usr/bin/env bash
# Atalho de conveniência para executar o pipeline dentro do container Docker
cd "$(dirname "$(readlink -f "$0")")"
docker compose run --rm ns3-sim ./run_pipeline.sh "$@"
