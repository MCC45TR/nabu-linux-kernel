#!/usr/bin/env bash
set -Eeuo pipefail

config_file=${1:-.config}
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
config_tool="$source_dir/scripts/config"

if [[ ! -s "$config_file" ]]; then
	printf 'missing kernel config: %s\n' "$config_file" >&2
	exit 1
fi

# This kernel boots one DT-described SM8150 tablet.  Keep generic Linux ABI,
# filesystem, security, networking and container features, but do not build
# hardware drivers for unrelated ARM64 boards.
platforms=(
	ARCH_ACTIONS ARCH_AIROHA ARCH_ALPINE ARCH_APPLE ARCH_ARTPEC
	ARCH_ASPEED ARCH_AXIADO ARCH_BCM ARCH_BCMBCA ARCH_BCM2835
	ARCH_BCM_IPROC ARCH_BERLIN ARCH_BLAIZE ARCH_BRCMSTB ARCH_BST
	ARCH_CIX ARCH_EXYNOS ARCH_HISI ARCH_INTEL_SOCFPGA ARCH_K3
	ARCH_KEEMBAY ARCH_LAYERSCAPE ARCH_LG1K ARCH_MA35 ARCH_MEDIATEK
	ARCH_MESON ARCH_MICROCHIP ARCH_MVEBU ARCH_MXC ARCH_NPCM ARCH_NXP
	ARCH_REALTEK ARCH_RENESAS ARCH_ROCKCHIP ARCH_S32 ARCH_SEATTLE
	ARCH_SOPHGO ARCH_SPARX5 ARCH_SPRD ARCH_STM32 ARCH_SUNXI
	ARCH_SYNQUACER ARCH_TEGRA ARCH_TESLA_FSD ARCH_THUNDER ARCH_THUNDER2
	ARCH_UNIPHIER ARCH_VEXPRESS ARCH_VISCONTI ARCH_XGENE ARCH_ZYNQMP
)

# Nabu is DT-only, boots from UFS, has no PCIe endpoint, SATA, NVMe, MMC,
# Ethernet, CAN, NFC or WWAN hardware, and uses only camera/media capture.
unused_buses=(
	ACPI PCI VIRTUALIZATION KVM XEN HYPERV ATA BLK_DEV_NVME MMC MTD
	ETHERNET CAN NFC WWAN MEDIA_ANALOG_TV_SUPPORT
	MEDIA_DIGITAL_TV_SUPPORT MEDIA_RADIO_SUPPORT MEDIA_SDR_SUPPORT
	MEDIA_CEC_SUPPORT MEDIA_PCI_SUPPORT MEDIA_USB_SUPPORT
	V4L_TEST_DRIVERS
)

args=()
for symbol in "${platforms[@]}" "${unused_buses[@]}"; do
	args+=(--disable "$symbol")
done

# Prune board-specific modules selected by the generic arm64 defconfig.  The
# allowlist contains Nabu silicon plus infrastructure required by those
# drivers.  Generic USB serial/network gadget classes remain available for
# OTG recovery and log collection.
module_deny='^(ARM_CCI|ARM_CCN|ARM_CMN|ARM_CORESIGHT|ARM_DSU|ARM_SMMU_V3_PMU|ARM_SPE|NVIDIA_CORESIGHT|CORESIGHT|ALTERA_|FPGA_|OF_FPGA|XILINX_|UACCE|CROS_|EC_|GREYBUS|GNSS|GOOGLE_|IPMI_|TCG_|MHI_|ATH11K|BRCMF|BRCMUTIL|MWIFIEX|RSI_|WCN36XX|WL18XX|WLCORE|RTC_DRV_|SENSORS_|IIO_CROS|IIO_ST_|TOUCHSCREEN_|BACKLIGHT_|BATTERY_|CHARGER_|DRM_|CLK_|SM_|SC_|SA_|SDM_|QCS_|QCM_|IPQ_|MSM_MMCC_|PHY_|TYPEC_|UCSI_|REGULATOR_|MFD_|GPIO_|PWM_|VIDEO_|SND_SOC_|BT_)'
module_allow='^(BACKLIGHT_KTZ8866|ATH10K|ATH10K_SNOC|ATH_COMMON|BT_HCIUART|BT_HIDP|BT_QCA|CHARGER_IDTP9418|CHARGER_LN8000|CHARGER_QCOM_SMB2|SM_CAMCC_8150|DRM_PANEL_NOVATEK_NT36523|GPIO_WCD934X|MFD_WCD934X|TOUCHSCREEN_NT36523_SPI|VIDEO_DEV|VIDEO_CN3927|VIDEO_OV13B10|VIDEO_OV8856|VIDEO_QCOM_CAMSS|VIDEO_QCOM_IRIS|SND_SOC_I2C_AND_SPI|SND_SOC_QCOM.*|SND_SOC_QDSP6.*|SND_SOC_SM8150|SND_SOC_CS35L41.*|SND_SOC_WCD934X|SND_SOC_WCD_COMMON|SND_SOC_WCD_CLASSH|SND_SOC_WCD_MBHC|SND_SOC_WSA881X)$'

while IFS= read -r symbol; do
	if [[ $symbol =~ $module_deny && ! $symbol =~ $module_allow ]]; then
		args+=(--disable "$symbol")
	fi
done < <(sed -n 's/^CONFIG_\([A-Z0-9_]*\)=m$/\1/p' "$config_file")

# ARCH_QCOM also selects support for many unrelated Qualcomm SoCs as built-in.
# Retain only the common frameworks and blocks referenced by the Nabu DT.
while IFS= read -r symbol; do
	case "$symbol" in
	PINCTRL_MSM|PINCTRL_QCOM_SPMI_PMIC|PINCTRL_SM8150) ;;
	*) args+=(--disable "$symbol") ;;
	esac
done < <(sed -n 's/^CONFIG_\(PINCTRL_[A-Z0-9_]*\)=[ym]$/\1/p' "$config_file")

while IFS= read -r symbol; do
	case "$symbol" in
	INTERCONNECT_QCOM_BCM_VOTER|INTERCONNECT_QCOM_RPMH|\
	INTERCONNECT_QCOM_RPMH_POSSIBLE|INTERCONNECT_QCOM_SM8150) ;;
	*) args+=(--disable "$symbol") ;;
	esac
done < <(sed -n 's/^CONFIG_\(INTERCONNECT_QCOM_[A-Z0-9_]*\)=[ym]$/\1/p' "$config_file")

while IFS= read -r symbol; do
	case "$symbol" in
	CC_IS_GCC|GCC10_NO_ARRAY_BOUNDS|GCC_NO_STRINGOP_OVERFLOW|\
	GCC_SUPPORTS_DYNAMIC_FTRACE_WITH_ARGS|HAVE_GCC_PLUGINS|\
	SM_GCC_8150|SM_GPUCC_8150|SM_DISPCC_8250|SM_CAMCC_8150) ;;
	*) args+=(--disable "$symbol") ;;
	esac
done < <(sed -n 's/^CONFIG_\([A-Z0-9_]*\(GCC\|GPUCC\|DISPCC\|VIDEOCC\|CAMCC\|TCSRCC\)[A-Z0-9_]*\)=[ym]$/\1/p' "$config_file")

"$config_tool" --file "$config_file" "${args[@]}" \
	--module SM_CAMCC_8150 \
	--enable SECURITY --enable SECURITY_NETWORK --enable SECURITY_SELINUX \
	--disable VIDEO_QCOM_VENUS \
	--disable DEBUG_INFO --enable DEBUG_INFO_NONE \
	--disable DEBUG_INFO_BTF --disable DEBUG_INFO_BTF_MODULES \
	--disable GDB_SCRIPTS
