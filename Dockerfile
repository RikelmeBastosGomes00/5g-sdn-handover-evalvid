# ============================================================================
# Multi-stage Dockerfile for ns3-5g-sdn-video
# ns-3.48 + 5G-LENA NR + EvalVid + OFSwitch13
# ============================================================================

# ---------------------------------------------------------------------------
# Stage 1: Base de dependências
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS deps

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=America/Sao_Paulo \
    LC_ALL=C.UTF-8 \
    LANG=C.UTF-8

# Habilitar arquitetura i386 para os binários de 32 bits do EvalVid
RUN dpkg --add-architecture i386

RUN apt-get update && apt-get install -y --no-install-recommends \
    # ── Build tools ──
    build-essential cmake ninja-build ccache g++ \
    pkg-config autoconf automake libtool flex bison dos2unix \
    # ── Python (ns-3 build system + scripts de plot) ──
    python3 python3-dev python3-pip python3-setuptools \
    python3-pandas python3-matplotlib \
    # ── ns-3 core dependencies ──
    libxml2-dev libsqlite3-dev libgsl-dev \
    libeigen3-dev libboost-all-dev \
    libgtk-3-dev \
    # ── OFSwitch13 / bofuss ──
    libpcap-dev libfl-dev nettle-dev \
    # ── EvalVid tools (binários i386) ──
    libc6:i386 libstdc++6:i386 zlib1g:i386 \
    # ── Video pipeline ──
    ffmpeg gpac \
    # ── Extras úteis ──
    git wget curl \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Stage 2: Compilar bofuss / ofsoftswitch13
# ---------------------------------------------------------------------------
FROM deps AS bofuss-build

WORKDIR /build

COPY bofuss/ ./bofuss/
COPY ofsoftswitch13/ ./ofsoftswitch13/

# Compilar ofsoftswitch13 (gera libofsoftswitch13 instalada em /usr/local)
WORKDIR /build/ofsoftswitch13
RUN find /build -type f \( -name "configure" -o -name "*.sh" -o -name "configure.ac" -o -name "*.m4" \) -exec sed -i 's/\r$//' {} + 2>/dev/null || true \
    && chmod -R +x /build/ofsoftswitch13 2>/dev/null || true \
    && if [ -f configure ]; then \
    sh ./configure; \
    elif [ -f configure.ac ]; then \
    autoreconf -i && sh ./configure; \
    fi \
    && make -j"$(nproc)" \
    && make install \
    && cp -rf /build/bofuss/include/* /usr/local/include/ \
    && ldconfig

# ---------------------------------------------------------------------------
# Stage 3: Compilar ns-3.48
# ---------------------------------------------------------------------------
FROM bofuss-build AS ns3-build

WORKDIR /sim
COPY ns-3.48/ ./ns-3.48/

WORKDIR /sim/ns-3.48

# Permissões de execução para scripts e binários, e remoção de CRLF (Windows)
RUN find /sim/ns-3.48 -type f \( -name "*.sh" -o -name "ns3" -o -name "*.py" \) -exec sed -i 's/\r$//' {} + 2>/dev/null || true \
    && chmod +x ns3 \
    && chmod +x etmp4 mp4trace psnr mos 2>/dev/null || true

# Configurar e compilar (optimized profile, compila src, contrib e scratch)
RUN ./ns3 configure --build-profile=optimized \
    && ./ns3 build -j"$(nproc)"

# ---------------------------------------------------------------------------
# Stage 4: Runtime (imagem menor, só o necessário para rodar)
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=America/Sao_Paulo \
    LC_ALL=C.UTF-8 \
    LANG=C.UTF-8

# Habilitar i386 para binários EvalVid
RUN dpkg --add-architecture i386

RUN apt-get update && apt-get install -y --no-install-recommends \
    # ── Runtime libs para ns-3 ──
    python3 \
    python3-pandas python3-matplotlib \
    libxml2 libsqlite3-0 \
    libgsl27 libgslcblas0 \
    libboost-system1.83.0 libboost-filesystem1.83.0 \
    libpcap0.8t64 libnettle8t64 \
    libgtk-3-0t64 \
    # ── EvalVid (i386) ──
    libc6:i386 libstdc++6:i386 zlib1g:i386 \
    # ── Video pipeline ──
    ffmpeg gpac \
    # ── Runner ns3 dependencies ──
    cmake ninja-build g++ \
    # ── Úteis ──
    bash coreutils findutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /sim/ns-3.48
ENV PATH="/sim/ns-3.48:${PATH}"

# ── Copiar binários e libs compilados ──
COPY --from=ns3-build /sim/ns-3.48/build/        ./build/
COPY --from=ns3-build /sim/ns-3.48/cmake-cache/  ./cmake-cache/
COPY --from=ns3-build /sim/ns-3.48/.lock-ns3_*   ./
COPY --from=ns3-build /sim/ns-3.48/ns3            ./ns3
COPY --from=ns3-build /sim/ns-3.48/VERSION        ./VERSION
COPY --from=ns3-build /sim/ns-3.48/CMakeLists.txt ./CMakeLists.txt
COPY --from=ns3-build /sim/ns-3.48/build-support/ ./build-support/

# ── Scratch (cenários de simulação) ──
COPY --from=ns3-build /sim/ns-3.48/scratch/       ./scratch/

# ── Contrib modules e source (necessários pelo runner ns3) ──
COPY --from=ns3-build /sim/ns-3.48/contrib/       ./contrib/
COPY --from=ns3-build /sim/ns-3.48/src/           ./src/

# ── Libs do ofsoftswitch13 instaladas em /usr/local ──
COPY --from=ns3-build /usr/local/lib/             /usr/local/lib/
COPY --from=ns3-build /usr/local/include/         /usr/local/include/

# ── Ferramentas EvalVid (binários i386 pré-compilados) ──
COPY --from=ns3-build /sim/ns-3.48/etmp4          ./etmp4
COPY --from=ns3-build /sim/ns-3.48/mp4trace       ./mp4trace
COPY --from=ns3-build /sim/ns-3.48/psnr           ./psnr
COPY --from=ns3-build /sim/ns-3.48/mos            ./mos

# ── Vídeos de teste ──
COPY --from=ns3-build /sim/ns-3.48/akiyo_cif.y4m   ./akiyo_cif.y4m
COPY --from=ns3-build /sim/ns-3.48/akiyo_cif.mp4   ./akiyo_cif.mp4
COPY --from=ns3-build /sim/ns-3.48/st_akiyo_cif.st ./st_akiyo_cif.st

# ── Scripts de pipeline ──
COPY --from=ns3-build /sim/ns-3.48/run_pipeline.sh              ./run_pipeline.sh
COPY --from=ns3-build /sim/ns-3.48/run_pbr_h264_experiment.sh   ./run_pbr_h264_experiment.sh

# ── Entrypoint ──
COPY entrypoint.sh /entrypoint.sh

RUN ldconfig \
    && sed -i 's/\r$//' /entrypoint.sh 2>/dev/null || true \
    && chmod +x /entrypoint.sh \
    && find /sim/ns-3.48 -type f -name "*.sh" -exec sed -i 's/\r$//' {} + 2>/dev/null || true \
    && chmod +x etmp4 mp4trace psnr mos \
    run_pipeline.sh run_pbr_h264_experiment.sh \
    && cp -f etmp4 mp4trace psnr mos /usr/local/bin/ \
    && touch cmake-cache/build.ninja cmake-cache/CMakeCache.txt 2>/dev/null || true \
    && mkdir -p /sim/ns-3.48/results

VOLUME ["/sim/ns-3.48/results"]

ENTRYPOINT ["/entrypoint.sh"]
CMD ["bash"]
