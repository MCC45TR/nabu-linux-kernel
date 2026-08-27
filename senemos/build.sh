#!/usr/bin/env bash
set -Eeuo pipefail

source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${SENEMOS_OUTPUT_DIR:-/workspace/kernel-builds/linux-nabu-senemos-mainline-alpha-v7.2.0}
jobs=${SENEMOS_JOBS:-16}

# Keep the release deterministic. An explicitly set empty LOCALVERSION stops
# setlocalversion from appending "+" merely because HEAD is past upstream-v7.2.
export LOCALVERSION=${SENEMOS_EXTRA_LOCALVERSION:-}

mkdir -p "$output_dir"

cp "$source_dir/senemos/configs/fedora-rawhide-aarch64.config" \
  "$output_dir/.config"

KCONFIG_CONFIG="$output_dir/.config" \
  "$source_dir/scripts/kconfig/merge_config.sh" -m -r -O "$output_dir" \
  "$output_dir/.config" \
  "$source_dir/senemos/configs/nabu-minimal.config"

make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 olddefconfig
make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 -j"$jobs" \
  Image qcom/sm8150-xiaomi-nabu.dtb modules
