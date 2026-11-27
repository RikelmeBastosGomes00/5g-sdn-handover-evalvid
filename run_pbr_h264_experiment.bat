@echo off
REM Atalho para rodar o experimento PBR H.264 no Docker pelo Windows
docker compose run --rm ns3-sim ./run_pbr_h264_experiment.sh %*
