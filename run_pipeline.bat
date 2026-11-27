@echo off
REM Atalho para rodar o pipeline EvalVid no Docker pelo Windows
docker compose run --rm ns3-sim ./run_pipeline.sh %*
