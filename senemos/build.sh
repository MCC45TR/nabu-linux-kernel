#!/usr/bin/env bash
set -Eeuo pipefail

source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${SENEMOS_OUTPUT_DIR:-/workspace/kernel-builds/linux-nabu-senemos-7.2.2-test}
jobs=${SENEMOS_JOBS:-16}

# Keep the release deterministic. An explicitly set empty LOCALVERSION stops
# setlocalversion from appending "+" merely because HEAD is past upstream-v7.2.
export LOCALVERSION=${SENEMOS_EXTRA_LOCALVERSION:-}

mkdir -p "$output_dir"

make -C "$source_dir" O="$output_dir" ARCH=arm64 defconfig

KCONFIG_CONFIG="$output_dir/.config" \
  "$source_dir/scripts/kconfig/merge_config.sh" -m -r -O "$output_dir" \
  "$output_dir/.config" \
  "$source_dir/senemos/configs/nabu-minimal.config"

"$source_dir/senemos/configs/prune-nabu-config.sh" "$output_dir/.config"

make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 olddefconfig
make -C "$source_dir" O="$output_dir" ARCH=arm64 LLVM=1 -j"$jobs" \
  Image qcom/sm8150-xiaomi-nabu.dtb modules
