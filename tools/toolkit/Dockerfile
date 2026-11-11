# neilrackett/atarist-toolkit-docker-x86_64:mintelf, the image the release
# workflow builds with: upstream's published toolkit image (v1.4.0, gcc
# 4.6.4) plus gcc 13 (m68k-atari-mintelf), installed as the toolkit's
# mintelf branch does it on amd64 (sidecartridge/atarist-toolkit-docker
# PR #17). Until upstream publishes an image with gcc 13.
#
# On the published image rather than from the toolkit's own Dockerfile:
# that one no longer builds, as vasm and vlink have left
# sun.hasenbraten.de. This way the old toolchain is upstream's own layers.
#
#   docker build --platform linux/amd64 -t neilrackett/atarist-toolkit-docker-x86_64:mintelf tools/toolkit
#   docker push neilrackett/atarist-toolkit-docker-x86_64:mintelf
FROM logronoide/atarist-toolkit-docker-x86_64:latest
RUN \
  export DEBIAN_FRONTEND=noninteractive && \
  add-apt-repository -y ppa:vriviere/mintelf && \
  apt -y update && \
  apt -y install cross-mintelf-essential cflib-m68k-atari-mintelf gemma-m68k-atari-mintelf gemlib-m68k-atari-mintelf ldg-m68k-atari-mintelf sdl-m68k-atari-mintelf ncurses-m68k-atari-mintelf zlib-m68k-atari-mintelf readline-m68k-atari-mintelf openssl-m68k-atari-mintelf fdlibm-m68k-atari-mintelf && \
  m68k-atari-mintelf-gcc --version
