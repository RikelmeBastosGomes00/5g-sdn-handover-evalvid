.PHONY: build run dev clean pipeline experiment help

help: ## Mostra esta ajuda
	@echo ""
	@echo "  ns3-5g-sdn-video — Comandos disponíveis"
	@echo "  ════════════════════════════════════════════"
	@echo ""
	@echo "  make build        Constrói a imagem Docker (runtime)"
	@echo "  make build-dev    Constrói a imagem Docker (desenvolvimento)"
	@echo "  make run          Abre shell interativo no container"
	@echo "  make dev          Abre shell no container de dev (com compilador)"
	@echo "  make pipeline     Executa pipeline completo EvalVid"
	@echo "  make experiment   Executa experimento PBR H.264"
	@echo "  make clean        Remove containers e imagens locais"
	@echo ""

build: ## Constrói imagem runtime
	docker compose build ns3-sim

build-dev: ## Constrói imagem de desenvolvimento
	docker compose build ns3-dev

run: build ## Shell interativo (runtime)
	docker compose run --rm ns3-sim bash

dev: build-dev ## Shell interativo (dev, com compilador)
	docker compose run --rm ns3-dev bash

pipeline: build ## Executa pipeline EvalVid completo
	docker compose run --rm ns3-sim ./run_pipeline.sh

experiment: build ## Executa experimento PBR H.264
	docker compose run --rm ns3-sim ./run_pbr_h264_experiment.sh

clean: ## Remove containers e imagens
	docker compose down --rmi local -v
