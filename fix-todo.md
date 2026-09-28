# Xiaomi Pad 5 (Nabu) Linux 6.17 kusursuzlaştırma planı

Bu dosya Linux 6.17 Nabu hattını güvenilir ana/fallback temel olarak tamamlamak
için gereken çekirdek, DT, güvenlik, paketleme ve fiziksel kabul işlerini tek
sözleşmede toplar. Hedef yalnız hataları azaltmak değil; her hata yolunun bounded,
gözlemlenebilir ve geri döndürülebilir olduğu üretim kalitesinde bir sistemdir.

## 1. Belge sözleşmesi

- Kaynak ağacı: `publication/nabu-linux-kernel`
- İncelenen dal/HEAD: `v1.4.1.2-alpha.1`,
  `d41e5e84ffaeef40a27916470ecff8d621952e38`
- Canlı denetlenen kernel: `6.17.0-senemos-2608282308`; kaynak HEAD ile aynı
  olduğu varsayılmaz, manifest/hash ile eşleştirilmelidir.
- Bu dosyadaki kodlar örnek tasarım iskeletidir; API ve kilit sırası gözden
  geçirilmeden doğrudan uygulanmaz.
- Her checkbox ancak istenen kanıt üretildiğinde kapanır.
- Çalışan 6.17 veya bilinen sağlam fallback, yeni aday fiziksel kabul edilmeden
  kaldırılmaz. Android EFI girdisi byte-for-byte korunur.
- Paket/COPR/solver başarısı fiziksel açılış, suspend veya çevre birimi kabulü
  anlamına gelmez.
- KDE/Fedora uygulamaları fork edilmez veya yamalanmaz; stok API, upstream rapor
  ve bağımsız Nabu entegrasyonu kullanılır.

## 2. Canlı bulguların 6.17 sahipliği

### Kesin 6.17/pstore kanıtları

- Kernel `6.17.0-2608282012` pstore zinciri:
  - ath10k queue flush timeout;
  - key/peer kaldırmada `-108/ESHUTDOWN`;
  - mac80211 `sta_info.c:1539` WARNING;
  - NVT/GENI SPI timeout;
  - DMA TX/RX reset başarısızlığı;
  - IRQ 201 “nobody cared” ve disable;
  - `-16/EBUSY`, `-5/EIO` dokunmatik hataları;
  - CPU7 üzerinde RCU stall ve BPF text-poke zinciri.
- Diğer 6.17 pstore/journal kanıtları:
  - kullanıcı süreçleri freezer iptali ve bağımsız ADSP fatal crash;
  - CS35L41 `Enable(0) failed -110`;
  - SLPI `USER-PD DOG`;
  - UFS UIC hata/recovery;
  - EFI runtime reboot fault;
  - watchdog timeout `EINVAL`;
  - RTC/duvar saati sorunları;
  - kesintili kapanışlar ve journal corruption.

### Canlı güncel durum

- Sistem `nabu-kernel-maintenance.service` nedeniyle degraded.
- SELinux config/cmdline enforcing diyor; çalışan kernel LSM listesinde SELinux
  yok ve `getenforce` Disabled.
- FastRPC/HexagonRPC servisleri çalışsa da `/dev/iio:device*` bulunmuyor.
- Firewalld şu an çalışıyor; önceki açılışlarda nftables transaction’ı çökmüş.
- Üç Linux EFI girişi ve Android mevcut; kabul bitmeden silme yapılmamalı.

## 3. Öncelik ve bağımlılık sırası

1. G0: Kesin kaynak/UKI/boot eşleştirmesi ve kurtarma.
2. G1: Wi-Fi/modem/mac80211 kapanış zinciri.
3. G2: GENI SPI/NVT ve RCU stall kök nedeni.
4. G3: ADSP/SLPI/FastRPC suspend yaşam döngüsü.
5. G4: Ses, sensör, ekran, depolama, şarj ve RTC.
6. G5: SELinux, nftables ve kernel güvenlik profili.
7. G6: RPM/UKI bakım hattı ve iki-slot politikası.
8. G7: Uzun fiziksel HIL, performans ve hata bütçesi.
9. G8: Stable/fallback kabulü.

## 4. G0 — Temel, kanıt ve geri dönüş

- [ ] Canlı `6.17.0-senemos-2608282308` için source commit, `.config`, DTB,
  `Image`, modules tree, UKI ve RPM NEVRA manifesti üret.
- [ ] Pstore kayıtlarını ilgili boot-ID ve UKI hash ile eşleştir; RTC tarihini
  tek başına kimlik olarak kullanma.
- [ ] Geliştirme için konu başına worktree/dal kullan; çalışan yayın dalına
  birden fazla hipotezi tek commit olarak koyma.
- [ ] A/B temelini fiziksel olarak bilinen en sağlam 6.17 UKI’ye sabitle.
- [ ] Android, current ve fallback dosyalarının SHA-256 değerlerini reboot öncesi
  ve sonrası karşılaştır.
- [ ] Reboot/poweroff fiziksel test planında açıkça onaylanmadan çalıştırılmasın.
- [ ] Her test sonucu `source`, `package`, `COPR`, `solver`, `UKI preboot` ve
  `physical HIL` aşamalarına ayrı yazılsın.

Önerilen kanıt ağacı:

```text
evidence/6.17/<build-id>/
├── source-manifest.txt
├── config.gz
├── image-dtb-modules.sha256
├── rpm-nevra-and-signatures.txt
├── esp-before.sha256
├── esp-after.sha256
├── boot-<boot-id>/
│   ├── uname.txt
│   ├── kernel.log
│   ├── warnings.log
│   ├── pstore.sha256
│   └── health.json
└── acceptance.md
```

## 5. G1 — Ath10k, modem SSR ve mac80211

### 5.1 Kök neden

- [ ] Pstore’daki sıralamayı trace ile doğrula: deauth → modem/remoteproc stop →
  WMI shutdown → key/peer cleanup → mac80211 station destroy WARNING.
- [ ] `ATH10K_SNOC_FLAG_MODEM_STOPPED` bayrağının yalnız bilgi mi yoksa WMI
  komutlarını engelleyen gerçek durum kapısı mı olduğunu denetle.
- [ ] `QCOM_SSR_BEFORE_SHUTDOWN` callback’i geldiğinde veri yolu kapanmadan önce
  netdev queue, scan, remain-on-channel, peer ve key işlemleri quiesce edilsin.
- [ ] Notifier atomic bağlam gerektiriyorsa uyuyabilen teardown ayrı workqueue’ya
  taşınsın ve remoteproc shutdown bounded completion beklesin.
- [ ] Firmware crash ile kontrollü shutdown ayrılmalı; crash recovery için gerekli
  dump yolu korunmalı.
- [ ] WMI artık çalışmıyorsa peer/key silmeyi yeniden deneme; local mac80211 state
  idempotent biçimde temizlenmeli.
- [ ] `net/mac80211/sta_info.c` WARN koşulu kaldırılmamalı; ona ulaşan yanlış sıra
  düzeltilmeli.

İlgili dosyalar:

- `drivers/net/wireless/ath/ath10k/snoc.c`
- `drivers/net/wireless/ath/ath10k/core.c`
- `drivers/net/wireless/ath/ath10k/mac.c`
- `drivers/net/wireless/ath/ath10k/wmi.c`
- `net/mac80211/sta_info.c`
- `drivers/remoteproc/qcom_q6v5_pas.c`
- `arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu.dts`

### Örnek durum kapısı

Nihai patch değildir; kapalı transport üzerinde komut üretmeme sözleşmesini
gösterir.

```c
static bool ath10k_snoc_transport_available(struct ath10k *ar)
{
        struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

        return !test_bit(ATH10K_SNOC_FLAG_MODEM_STOPPED, &ar_snoc->flags) &&
               ar->state == ATH10K_STATE_ON;
}

/* Callers must still complete local state cleanup when this is false. */
```

### 5.2 Kabul

- [ ] 100 bağlantı kes/bağlan döngüsü.
- [ ] 50 uçak modu döngüsü.
- [ ] 50 modem SSR.
- [ ] 100 suspend/resume ve 30 reboot.
- [ ] Sıfır queue flush timeout, `-ESHUTDOWN` key/peer hatası ve sta_info WARN.
- [ ] Her döngüden sonra ağ tarama, DHCP, IPv4/IPv6 ve 30 saniyelik trafik çalışır.

## 6. G2 — GENI SPI18 ve NVT dokunmatik

### 6.1 Controller state machine

- [ ] `drivers/spi/spi-geni-qcom.c` için açık state tanımla:
  `IDLE`, `TRANSFER`, `CANCEL`, `ABORT`, `DMA_RESET`, `FAULTED`, `RECOVERING`.
- [ ] `cur_xfer`, `cur_xfer_mode` ve completion nesnelerinin sahipliği tek lock
  sözleşmesiyle belgelenmeli.
- [ ] Cancel/abort completion beklenirken spinlock tutulmamalı.
- [ ] Abort timeout sonrası IRQ status/enable ile DMA TX/RX status bounded trace’e
  alınmalı.
- [ ] Belgelenmiş pending bitler W1C ile temizlenmeden yeni transfer başlatılmamalı.
- [ ] DMA reset başarısızsa controller `FAULTED` olmalı; her NVT poll’unda yeni
  abort başlatmamalı.
- [ ] Runtime-PM suspend, system suspend ve NVT workqueue transferi serialize edilmeli.
- [ ] IRQ handler tüm okunan status bitlerini doğru temizlemeli; `IRQ_NONE`
  yalnız gerçekten hiçbir status yoksa dönmeli.

### 6.2 NVT istemci davranışı

- [ ] SPI `-ETIMEDOUT`, `-EBUSY`, `-EIO` için bounded retry matrisi tanımla.
- [ ] İlk hata: transferi iptal et; ikinci hata: controller/device reset; devam
  ederse aygıtı offline yap ve tek health event üret.
- [ ] Suspend sırasında poll/firmware workqueue’larını durdur; resume sonrası
  controller hazır olmadan okumaya başlama.
- [ ] Touch slot state resume’da sıfırlansın; MOVE-before-DOWN KWin’e ulaşmasın.
- [ ] Bilinmeyen pen format ID yalnız ilk olayda bounded hexdump üretsin.

İlgili dosyalar:

- `drivers/spi/spi-geni-qcom.c`
- `drivers/input/touchscreen/nt36523/nt36xxx.c`
- `drivers/input/touchscreen/nt36523/nt36xxx_fw_update.c`
- `drivers/gpu/drm/panel/panel-novatek-nt36523.c`
- `arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu.dts`

### Örnek hata matrisi

```text
ETIMEDOUT  -> cancel/abort -> one controller reset -> offline
EBUSY      -> wait for recovery completion -> one retry -> offline
EIO        -> device reset/reinit -> one retry -> offline
ESHUTDOWN  -> no retry; suspend/remove completion
```

### 6.3 Kabul

- [ ] 10.000 touch ve 1.000 pen olayı.
- [ ] 200 ekran kapat/aç.
- [ ] 100 suspend/resume.
- [ ] Hata enjeksiyonunda en fazla bir reset ve iki saniyelik bounded recovery.
- [ ] Sıfır SPI timeout, pending abort, DMA reset failure, disabled IRQ ve KWin
  touch sequence warning.

## 7. G2 — RCU/BPF stall

- [ ] CPU7 stall’un SPI IRQ deadlock’ının sonucu mu bağımsız BPF text-poke sorunu
  mu olduğunu SPI fault injection açık/kapalı A/B ile ayır.
- [ ] `rcu_trace`, IRQ latency, IPI, workqueue ve `bpf_prog_free_deferred`
  tracepointlerini bounded ring buffer’da topla.
- [ ] `smp_call_function_many_cond` hedef CPU maskesi ve CPU7 interrupt state’i
  stall anında kaydedilsin.
- [ ] BPF JIT kapatma yalnız teşhis build’i olsun; üretim çözümü sayılmasın.
- [ ] Lockdep/KCSAN debug build ile GENI SPI ve BPF free yolu test edilsin.
- [ ] 24 saat soak + 1.000 BPF program load/unload sırasında RCU stall sıfır.

## 8. G3 — ADSP, SLPI, FastRPC ve suspend

### 8.1 Freezer ve DSP’yi ayır

- [ ] `SYSTEMD_SLEEP_FREEZE_USER_SESSIONS=false` kullanıcı alanı workaround’ını
  kernel çözümü gibi göstermeden ayrı entegrasyon paketi olarak koru.
- [ ] Freezer başarılı iken görülen ADSP fatal crash’i ana kernel/DSP problemi
  kabul et.
- [ ] Suspend girişinde ADSP/SLPI/MPSS/CDSP durumunu, pending RPMSG ve FastRPC
  kullanıcı sayısını kaydet.
- [ ] Resume tamamlanmadan yeni suspend isteğini reddet veya sıraya al.
- [ ] Remoteproc recovery bitmeden iio/audio kullanıcı alanı servislerini başlatma.

### 8.2 FastRPC bellek sahipliği

Mevcut 6.17 kodu SDSP SCM assign sonucunu kontrol etmiyor. Bu, gerçek hatayı
saklayabilir.

- [ ] `qcom_scm_assign_mem()` dönüşünü mutlaka kontrol et.
- [ ] `vmcount`, resource alignment/boyut ve kaynak VMID maskesini çağrı öncesi
  doğrula.
- [ ] Firmware VMID’leri restart boyunca koruyorsa 7.2’deki yaklaşımı körlemesine
  kopyalama; Nabu firmware üzerinde A/B kanıt üret.
- [ ] `-EINVAL` yalnız doğrulanmış retained-VMID sözleşmesinde kontrollü kabul
  edilsin; diğer bütün durumlarda probe kesin başarısız olsun.
- [ ] Aynı boot’ta sınırsız RPMSG reprobe ve log fırtınası engellensin.

Örnek hata kontrolü:

```c
err = qcom_scm_assign_mem(res.start, resource_size(&res),
                          &src_perms, data->vmperms, data->vmcount);
if (err)
        return dev_err_probe(rdev, err,
                             "failed to assign SDSP reserved memory\n");
```

Bu örnek retained-VMID firmware durumunu kapsamaz; o durum kanıtlandıktan sonra
dar kapsamlı ve belgeli dal eklenmelidir.

### 8.3 Sensör kabulü

- [ ] `/dev/iio:device*` gerçekten oluşmadan sensor stack sağlıklı sayılmasın.
- [ ] DT mount matrix gerçek Nabu yönünü temsil etsin; sıfır matris yasaklansın.
- [ ] `hexagonrpcd-sdsp` restart sayısı ve SLPI crash health metriğine girsin.
- [ ] 100 suspend/resume sonrası ivmeölçer, gyro, ALS, SAR ve rotation çalışsın.

İlgili dosyalar:

- `drivers/misc/fastrpc.c`
- `drivers/remoteproc/qcom_q6v5_pas.c`
- `drivers/rpmsg/qcom_glink_native.c`
- `arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu.dts`
- bağımsız sistem entegrasyonundaki sensör/session-gate unit’leri

## 9. G4 — CS35L41, QDSP6 ve ses yönlendirme

- [ ] Dört CS35L41 için per-device ve shared-boost state açıkça tutulmalı.
- [ ] Zaten disabled amp üzerinde PDN_DONE beklenmemeli.
- [ ] `regmap_read()` sonuçları kontrol edilmeden register değeri kullanılmamalı.
- [ ] DAPM `PRE_PMU`/`POST_PMD` yarışları dört amp için serialize edilmeli.
- [ ] Timeout durumunda register snapshot bounded olmalı ve amp güvenli state’e
  dönmeli.
- [ ] Dummy VA/VP supply yerine gerçek regulator bağlantıları tanımlanmalı.
- [ ] `sound/soc/qcom/sm8150.c` FE/BE rotaları, WCD934x mux ve Q6ASM DAI’ları
  UCM ile birlikte doğrulanmalı.
- [ ] ALSA state erişim problemi kullanıcı alanı sahiplik/policy ile çözülmeli;
  kernel kontrol isimlerini gizleyerek aşılmamalı.

İlgili dosyalar:

- `sound/soc/codecs/cs35l41.c`
- `sound/soc/codecs/cs35l41-lib.c`
- `sound/soc/qcom/sm8150.c`
- `sound/soc/qcom/qdsp6/q6asm.c`
- `sound/soc/qcom/qdsp6/q6asm-dai.c`
- `arch/arm64/boot/dts/qcom/sm8150-xiaomi-nabu.dts`

Kabul:

- [ ] 100 playback/stop, 100 volume ramp, 100 suspend/resume.
- [ ] Dört hoparlör kanal/yön testi ve mikrofon kaydı.
- [ ] Sıfır `Enable(0) failed -110`, EOS timeout, underflow/overflow.
- [ ] Ses kapalıyken boşta güç tüketimi regresyonu yok.

## 10. G4 — GPU, ekran ve medya

- [ ] 6.17’de Adreno/SMMU fault bütçesi sıfır olarak korunmalı; 7.2 düzeltmeleri
  geriye taşınacaksa commit bazlı ve ayrı A/B yapılmalı.
- [ ] DSI-1 iç panel için deterministik mode listesi sağla; EDID/DDC beklenmiyorsa
  probe etme.
- [ ] 60/90 Hz geçişlerinde dual-DSI, TE/vblank ve touch FPS senkron olsun.
- [ ] 120 Hz yalnız fiziksel panel kabulü sonrası görünür olsun.
- [ ] qcom_iris ayrı entegrasyon dalındaki değişiklikler üç-yollu port ve modpost
  doğrulamasıyla alınmalı; dosya overlay’i körlemesine kopyalanmamalı.
- [ ] V4L2 → VA-API → FFmpeg/Chromium zinciri fiziksel olarak doğrulanmalı.
- [ ] KWin/KScreen hatası stock uygulama yamalanarak değil, connector/mode ve
  bağımsız ayar paketiyle çözülmeli.

Kabul:

- [ ] 8 saat Wayland/KWin soak.
- [ ] 30 dakika 4K60 VP9, fullscreen, seek ve scroll; dropped frames ≤ %0,1.
- [ ] 100 ekran aç/kapa ve 100 rotation.
- [ ] GPU reset, IOMMU fault ve output configuration failure sıfır.

## 11. G4 — Wi-Fi kalibrasyonu ve Bluetooth

- [ ] Ath10k `invalid frequency 0 idx 41` event parser sınır kontrolüyle çözülsün.
- [ ] Kalıcı donanım MAC’i NVMEM/firmware kaynağından gelsin.
- [ ] Wi-Fi 3,3 V supply gerçek regulator/fixed-regulator ile tanımlansın.
- [ ] Board/calibration dosyasının cihaz varyantıyla eşleştiği hash ile kanıtlansın.
- [ ] Bluetooth `Frame reassembly failed (-84/EILSEQ)` için transport baud,
  firmware ve suspend resume state’i incelensin.
- [ ] 8 saat Wi-Fi trafik + Bluetooth HID/ses birlikte test edilsin.

## 12. G4 — RTC, UFS, şarj ve watchdog

### RTC

- [ ] EFI RTC’nin çalışmadığı kabul edilmeden önce read/set davranışı doğrulansın.
- [ ] PMIC RTC doğru aygıtsa `CONFIG_RTC_HCTOSYS_DEVICE` ve DT alias buna göre
  ayarlansın.
- [ ] 20 cold boot’ta 1971/25 Temmuz başlangıç zamanı görülmesin.
- [ ] Boot sırası health raporlarında Boot-ID + monotonic timestamp kullanılsın.

### UFS

- [ ] `vccq2` regulator hazır olma sırası düzeltilsin; beklenen `-517` error
  seviyesinde yazılmasın.
- [ ] UIC `saved_err 0x4/saved_uic_err 0x2` için PHY, gear, lane ve power-mode
  snapshot’ı alınsın.
- [ ] 100 GiB checksum’lı I/O + 100 suspend/resume: recovery ve data error sıfır.
- [ ] UFS firmware güncellemesi yalnız üretici kanıtı ve rollback ile ayrı test.

### Şarj

- [ ] LN8000 NTC alarm/fault gerçek sıcaklıkla kalibre edilsin.
- [ ] Güvenlik nedeniyle şarj kesme korunmalı; yalnız yanlış eşik düzeltilmeli.
- [ ] USB-PD, normal/hızlı şarj, ekran açık/kapalı ve suspend matrisi uygulanmalı.

### Watchdog

- [ ] systemd’nin istediği final timeout QCOM WDT’nin desteklediği aralığa uysun.
- [ ] `Failed to set watchdog ... Invalid argument` sıfır olmalı.
- [ ] Kernel panic/hang testinde watchdog kurtarması ve pstore kaydı doğrulanmalı.

## 13. G4 — Pstore ve günlük güvenilirliği

- [ ] Ramoops reserved-memory boyutu ile kernel parametreleri birebir eşleşsin.
- [ ] ECC/header uyumu düzeltilsin; `uncorrectable error in header` sıfır.
- [ ] Ramoops veya EFI pstore’dan bir ana backend seçilsin; beklenen backend
  reddi her boot’ta warning üretmesin.
- [ ] Crash kaydı hashlenip arşivlendikten sonra kontrollü temizlensin.
- [ ] Başarılı NVT probe/update ve LN8000 IRQ girişi error yerine info/debug olsun.
- [ ] Journald rate limit yalnız kaynak fırtınalar düzeltildikten sonra ayarlansın.
- [ ] 14 sistem/10 kullanıcı journal corruption geçmişi nedeniyle clean shutdown
  marker testi yayın kapısına eklensin.

## 14. G5 — SELinux ve kernel güvenlik profili

Mevcut `arch/arm64/configs/sm8150.config` SELinux sözleşmesini açıkça taşımıyor;
canlı kernel yalnız `capability` LSM ile açılmış.

- [ ] Config birleştirme sonucu şu seçenekleri taşımalı:
  - `CONFIG_SECURITY=y`;
  - `CONFIG_SECURITYFS=y`;
  - `CONFIG_SECURITY_SELINUX=y`;
  - `CONFIG_SECURITY_SELINUX_BOOTPARAM=y`;
  - `CONFIG_DEFAULT_SECURITY_SELINUX=y`;
  - `CONFIG_LSM` içinde `lockdown,yama,integrity,selinux,bpf,landlock`;
  - Fedora firewalld/nftables için gereken netfilter seçenekleri.
- [ ] `/etc/selinux/config`, cmdline ve canlı `getenforce` aynı sonucu vermeli.
- [ ] Rootfs tam `setfiles` ile etiketlenmeli; `/etc/ld.so.cache` kesinlikle
  `unlabeled_t` olmamalı.
- [ ] FastRPC/HexagonRPC için dar SELinux domain/device policy hazırlanmalı.
- [ ] Enforcing 24 saat soak’ta beklenmeyen AVC sıfır.
- [ ] Modül imzaları ve keyring doğrulansın; test için SELinux kapatma kalıcı
  çözüm sayılmasın.

Önerilen config parçası örneği:

```text
CONFIG_SECURITY=y
CONFIG_SECURITYFS=y
CONFIG_SECURITY_SELINUX=y
CONFIG_SECURITY_SELINUX_BOOTPARAM=y
CONFIG_DEFAULT_SECURITY_SELINUX=y
CONFIG_LSM="lockdown,yama,integrity,selinux,bpf,landlock"
```

Final değer Fedora Rawhide taban config’iyle merge edilip `olddefconfig` sonrası
yeniden doğrulanmalıdır.

## 15. G5 — nftables/firewalld ve modül yükleme

- [ ] Firewalld’ın başarısız JSON transaction’ındaki her expression için gerekli
  `nf_tables`, conntrack, NAT, reject, fib ve IPv6 modülleri final payload’da
  doğrulansın.
- [ ] Initramfs/boot sırasındaki module autoload, firewalld başlamadan önce hazır
  olmalı.
- [ ] `firewall-cmd --check-config`, temiz namespace ruleset yükleme ve reboot
  sonrası public zone testi CI/HIL’e eklenmeli.
- [ ] `Failed to find module xhci_plat_hcd` için doğru modül adı/config/payload
  eşleştirilsin; olmayan modül modules-load’dan kaldırılmalı.
- [ ] UFS tablette gereksiz multipath unit’i donanım profilinde kapatılmalı; kernel
  hatası gibi ele alınmamalı.

## 16. G6 — RPM, installonly ve UKI bakım servisi

İlgili çekirdek-dışı dosyalar (`Fedora_Rawhide_Builder` köküne göre):

- `rpmbuild-nabu-suspend-v1.4.1.2/SPECS/senemos-nabu-kernel-alpha.spec`
- canonical `senemos-nabu-kernel-alpha` kaynak paketi
- `Nabu-Fedora-Rawhide-Builder-two-meta/packaging/copr/nabu-unified-meta/nabu-kernel-maintenance`
- `Nabu-Fedora-Rawhide-Builder-two-meta/packaging/copr/nabu-kernel-maintenance/nabu-kernel-maintenance`
- canonical `nabu-regenerate-uki`

Yapılacaklar:

- [ ] Core/modules `installonlypkg(kernel)` sağlamalı; kendi eski sürümlerini
  Obsoletes etmemeli.
- [ ] Çalışan ve fallback kernel DNF transaction’dan korunmalı.
- [ ] DNF scriptlet UKI üretmemeli; exact ABI/hash pending kaydı yazmalı.
- [ ] Bakım preflight’ı bütün ESP mutasyonlarından önce çalışmalı.
- [ ] `loader.conf default` canonical current olduğunda status 9 üretmemeli.
- [ ] UKI temp dosya → doğrulama → fsync → atomik rename sırasıyla yazılmalı.
- [ ] Aynı ABI/image/DTB hash’i daha önce hazırlanmışsa dracut atlanmalı.
- [ ] Timer kendi başına `dnf --refresh upgrade` çalıştırmamalı.
- [ ] Başarıda pending temizlenmeli; prepared kaydı UKI hash’i taşımalı.
- [ ] Normal logda initramfs dosya listesinin tamamı basılmamalı.
- [ ] Transaction sonrası yalnız current + bir known-good fallback + Android
  hedeflenmeli; üçüncü Linux girişi ancak fiziksel kabulden sonra kaldırılmalı.

### Örnek bakım preflight sözleşmesi

```bash
preflight() {
    test -s "$image"
    test -s "$dtb"
    test -d "$module_dir"
    test -f "$esp/EFI/BOOT/BOOTAA64.EFI"
    test -f "$esp/EFI/Android/Reboot2Android.efi"
    verify_free_space
    verify_non_target_hashes
    verify_loader_entries
}

# preflight succeeds before dracut or any ESP write.
preflight
build_uki_to_temp
verify_uki
atomic_install_uki
commit_prepared_state
```

## 17. G7 — Örnek test dosyaları

Önerilen kaynak ağacı:

```text
tools/testing/selftests/nabu/
├── Makefile
├── config
├── fastrpc-restart.sh
├── spi-fault-contract.sh
└── README.md

senemos/tests/
├── build-manifest.sh
├── check-config.sh
├── check-dtb.sh
├── check-module-payload.sh
├── collect-health.sh
├── compare-error-signatures.sh
├── error-signatures.txt
└── physical-acceptance.md
```

Donanıma özel reboot/suspend komutları kernel selftest içinde otomatik
çalışmamalı; dış HIL sürücüsü ve açık kullanıcı onayı gerektirmelidir.

Örnek `compare-error-signatures.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

log=${1:?journal export required}
patterns=${2:?signature list required}
failed=0

while IFS= read -r pattern; do
    test -n "$pattern" || continue
    if grep -Eiq "$pattern" "$log"; then
        printf 'FAIL: %s\n' "$pattern" >&2
        failed=1
    fi
done <"$patterns"

exit "$failed"
```

Örnek `error-signatures.txt`:

```text
sta_info.c:1539
failed to flush transmit queue
failed to install key.*-108
failed to delete peer.*-108
SPI transfer timed out
Interrupts pending after abort
DMA (TX|RX) RESET failed
nobody cared
RCU.*stall
USER-PD DOG
fatal error received.*adsp
Enable\(0\) failed: -110
saved_uic_err
Unable to handle paging request in EFI runtime service
```

## 18. Derleme, DT ve paket kabulü

- [ ] `make ARCH=arm64 defconfig` + `merge_config.sh` + `olddefconfig`
  deterministik.
- [ ] `make ARCH=arm64 W=1 Image dtbs modules` hatasız.
- [ ] `CHECK_DTBS=y` Nabu DTB için temiz.
- [ ] Değişen binding’lerde `dt_binding_check` temiz.
- [ ] Checkpatch/sparse/Smatch değişen sürücülerde çalıştırılmış.
- [ ] Debug worktree’de lockdep/KASAN/KCSAN hedefli test yapılmış.
- [ ] `Image`, DTB, `Module.symvers`, modül sayısı ve vermagic doğrulanmış.
- [ ] Modüller `strip → sign → Zstd` sırasıyla işlenmiş.
- [ ] RPM’de `build/source` symlinkleri yok.
- [ ] SRPM ve RPM izole keyring ile imzalı; AArch64 solver temiz.
- [ ] `dnf5 check` temiz ve çalışan/fallback kernel korunuyor.

## 19. Fiziksel kabul matrisi

### Boot/kapanış

- [ ] 20 cold boot.
- [ ] 30 warm reboot.
- [ ] 20 clean poweroff.
- [ ] Her boot’ta doğru `uname -r`, UKI hash ve boot-ID.
- [ ] EFI runtime fault, watchdog error, pstore corruption ve journal corruption 0.

### Suspend

- [ ] 100 s2idle suspend/resume.
- [ ] 25 Wi-Fi + trafik sırasında.
- [ ] 25 ses çalarken.
- [ ] 25 Bluetooth bağlıyken.
- [ ] 25 şarj sırasında.
- [ ] ADSP/SLPI crash, touch kaybı, SPI timeout ve RCU stall 0.

### Çevre birimleri

- [ ] Wi-Fi scan/connect/roam ve Bluetooth HID/ses.
- [ ] Dört hoparlör ve mikrofon.
- [ ] Touch, pen, DT2W, pogo keyboard/tablet-mode.
- [ ] Rotation, ALS, SAR ve diğer IIO sensörleri.
- [ ] USB-C data/charging ve harici depolama.
- [ ] UFS uzun I/O ve şarj/termal matrisi.

### Masaüstü ve medya

- [ ] 8 saat Plasma Wayland soak.
- [ ] 30 dakika 4K60 VP9; seek, fullscreen ve scroll.
- [ ] 60/90 Hz geçişleri, Night Light ve color profile.
- [ ] Stock KDE/Fedora uygulamalarında yerel fork/patch yok.

### Hata bütçesi

- [ ] Kernel panic/oops/BUG/WARN/RCU stall/hung task: **0**.
- [ ] SPI, DMA, IRQ, GPU/IOMMU, FastRPC/SCM, remoteproc fatal: **0**.
- [ ] Beklenmeyen journal priority 0–3: **0**.
- [ ] Priority 4 allowlist dışı warning: **0**.
- [ ] `systemctl --failed`: **0**.
- [ ] Beklenmeyen SELinux AVC: **0**.
- [ ] Yeni pstore crash kaydı: **0**.

## 20. Performans ve güç hedefleri

- [ ] Açılış süresi firmware, loader, kernel, initrd ve userspace olarak ayrı
  ölçülsün; kernel iyileştirmesi firmware kazanımı gibi gösterilmesin.
- [ ] Boşta CPU residency ve wakeup kaynakları 30 dakika ölçülsün.
- [ ] Ekran kapalı suspend tüketimi tekrarlanabilir koşulda ölçülsün.
- [ ] 4K60 sırasında CPU/GPU/Iris frekansı, sıcaklık ve dropped frames kaydedilsin.
- [ ] Ses açık/kapalı, Wi-Fi aktif/boşta ve Bluetooth senaryoları ayrı ölçülsün.
- [ ] Bir düzeltme işlevsel kabulü geçse bile güç veya termal regresyon getiriyorsa
  tamamlanmış sayılmasın.

## 21. Yayın ve rollback

- [ ] Her P0/P1 düzeltmesi tek hipotez/commit olsun.
- [ ] Commit mesajı gözlenen hata imzası, kök neden ve testleri içersin.
- [ ] Önce local build, payload, RPM; sonra terminal COPR sonucu, imza, solver;
  en son fiziksel HIL.
- [ ] Yeni aday test/alpha kanalda kalmalı; kullanıcı onayı olmadan stable olmamalı.
- [ ] Reboot öncesi current/fallback/Android hashleri doğrulanmalı.
- [ ] SSH geri dönmez veya P0 imzası görülürse fallback seçilip kanıt toplanmalı;
  test sonucu başarılı gösterilmemeli.
- [ ] Stable adaylığı için iki ardışık, bağımsız build bütün kabul matrisini geçsin.

## 22. Tamamlanma tanımı

Bu TODO ancak aşağıdaki maddelerin tamamı kapalıysa bitmiş sayılır:

- [ ] G0–G8 kapılarının tamamı geçti.
- [ ] Ath10k/modem/mac80211 kapanış zinciri hatasız.
- [ ] GENI SPI/NVT timeout sonrası bounded recovery sağlıyor; IRQ disable yok.
- [ ] RCU/BPF stall yeniden üretilemiyor.
- [ ] ADSP/SLPI/FastRPC ve bütün IIO sensörleri 100 suspend döngüsünü geçiyor.
- [ ] Dört hoparlör, mikrofon ve QDSP6 rotaları hata günlüğü olmadan çalışıyor.
- [ ] Wi-Fi/Bluetooth, GPU/display/media, UFS/RTC/şarj kabulü tamam.
- [ ] SELinux enforcing gerçekten etkin, etiketler doğru ve AVC temiz.
- [ ] Firewalld/nftables her boot’ta atomik ruleset yükleyebiliyor.
- [ ] Pstore güvenilir; journal/pstore hata kanıtı kaybolmuyor.
- [ ] Kernel bakım servisi atomik/idempotent ve sistem degraded değil.
- [ ] Tam olarak current 6.17, bir fiziksel kabul edilmiş fallback ve Android
  politikası uygulanmış; kaldırma fiziksel kabulden sonra yapılmış.
- [ ] Bütün error/warning imzalarının sahibi, kök nedeni ve kabul testi var.
- [ ] Paket/COPR/solver kanıtı fiziksel HIL’den ayrı raporlanıyor.
