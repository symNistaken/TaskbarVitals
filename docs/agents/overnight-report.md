# Gece Boyu ve Gündüz Tamamlama Raporu (`improvements/overnight`)

- **Dal:** `improvements/overnight` (`main` @ `39f6932` temel alınarak `C:\Users\saymin\orca\workspaces\WirelessBattery\toadfish` worktree'sinde çalışıldı; `main` dalına dokunulmadı, hiçbir değişiklik push edilmedi.)
- **Derleme ve Test Ortamı Notu:** Sistemde MSYS2 (`C:\msys64`) kurulu olmadığı için her aşamada ve final doğrulamasında `build.bat 2.0.0 test` ile birlikte Git Bash (`C:\Program Files\Git\usr\bin`) + WinLibs MinGW-w64 (`g++ 16.2.0`) üzerinden `mingw32-make SHELL=sh.exe -j12 VERSION=2.0.0 CHANNEL=test` ve `mingw32-make SHELL=sh.exe test VERSION=2.0.0 CHANNEL=test` çalıştırıldı.
- **Varsayılan Davranış Politikası:** Gece oturumunda `KISITLAR` gereği kapalı tutulan Görev 6 ve Görev 7, gündüz oturumunda kullanıcı onayıyla (`07afca6`) planlandığı gibi varsayılan açık (`PollLessFullscreen = true`, `InGameAlerts = 1 / FullscreenOnly`) ve çoklu monitör destekli hâle getirildi; diğer yeni göstergeler/IPC özellikleri (`ShowLaptopBattery`, `ShowNetwork`, `ShowCpuTemp`, `LocalApiPipe`, vb.) varsayılan olarak kapalıdır.

---

## 1. Görev Durumları

| Görev | Durum | Commit Hash (İş / Plan Güncellemesi) | Değişen Dosyalar | Özet |
| :--- | :--- | :--- | :--- | :--- |
| **Görev 1:** Laptop'un kendi pilini sistem şeridinde göster | **tamam** | `e41317c` / `bfcfb39` | `Makefile`, `README.md`, `docs/agents/improvement-plan.md`, `lang/_template.txt`, `lang/tr.txt`, `src/laptopbat.cpp`, `src/laptopbat.h`, `src/main.cpp`, `src/system_order.h`, `tests/laptopbat_test.cpp` | `GetSystemPowerStatus` ile laptop pilini varsayılan kapalı bir sistem şeridi öğesi olarak ekler ve `SystemOrder` formatını geriye uyumlu 8 yuvalı (4-bit) yapıya genişletir. |
| **Görev 2:** Sıcaklık renk eşiklerini ayarlanabilir yap | **tamam** | `7c1213f` / `755696d` | `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/main.cpp`, `src/sysmon.h`, `tests/sysmon_test.cpp`, `docs/agents/improvement-plan.md` | System sekmesine `Standard (80/90)`, `Laptop (85/95)` ve `Strict (70/85)` sıcaklık renk ön ayarlarını ekler ve yüksek sıcaklık uyarısının da seçili ön ayara uymasını sağlar. |
| **Görev 3:** Ağ hızı göstergesi (↓ / ↑) | **tamam** | `353bbf7` / `c6cd46e` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/main.cpp`, `src/netmon.cpp`, `src/netmon.h`, `src/netmon_math.h`, `tests/netmon_test.cpp`, `docs/agents/improvement-plan.md` | Delay-load `iphlpapi.dll` (`GetIfTable2`) sayaç farklarıyla indirme/yükleme hızını varsayılan kapalı bir şerit öğesi olarak hesaplayıp gösterir ve kapatılınca DLL'i serbest bırakır. |
| **Görev 4:** Cihaz kartı menüsüne "Yoksay" ekle | **tamam** | `a13b099` / `a6ed83f` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/devices.cpp`, `src/devices.h`, `src/drivers/drivers.h`, `src/drivers/{aula,corsair,logitech,nzxt,playstation,rog,rongyuan,semico,vaxee}.cpp`, `src/ignore_devices.h`, `src/main.cpp`, `tests/ignore_devices_test.cpp`, `docs/agents/improvement-plan.md` | `DeviceStatus::pid` alanını ekleyerek cihaz kartı menüsünden onaylı yoksayma ve Batteries sekmesindeki `Ignored devices...` üzerinden tek tıkla geri alma sunar. |
| **Görev 5:** Ayarları ve geçmişi dışa/içe aktar | **tamam** | `64692e9` / `9a9c168` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/battery_history.cpp`, `src/file_dialog.h`, `src/main.cpp`, `src/settings_port.h`, `src/update_win.cpp`, `src/update_win.h`, `tests/settings_port_test.cpp`, `docs/agents/improvement-plan.md` | `Run` anahtarını hariç tutarak doğrulamalı JSON ayar dışa/içe aktarma ve pil geçmişi penceresi için 5 sütunlu CSV dışa aktarma özellikleri ekler. |
| **Görev 6:** Tam ekran farkındalığı | **tamam** | `0cc20ec`, `07afca6` | `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/main.cpp`, `src/refresh_policy.h`, `tests/refresh_policy_test.cpp` | `SHQueryUserNotificationState` ve ön plan tam ekran pencere tespitiyle oyun sırasında pil yoklamasını yavaşlatır; gündüz onayıyla varsayılan açık (`true`) yapıldı. |
| **Görev 7:** Oyun içi mini uyarı (OSD-lite) | **tamam** | `fdc7632`, `07afca6` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/main.cpp`, `src/osd_overlay.cpp`, `src/osd_overlay.h`, `src/osd_policy.h`, `tests/osd_policy_test.cpp` | Düşük pilde fokus çalmayan ve tıklamaları geçiren çoklu monitör akıllı köşe OSD penceresini (`FullscreenOnly` varsayılan + `FullscreenGame` + `Always`) ekler. |
| **Görev 8:** Uyarı sesi seçeneği | **tamam** | `609a3bf` / `f5c2d96` | `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/alert_rules.h`, `src/main.cpp`, `tests/alert_rules_test.cpp`, `docs/agents/improvement-plan.md` | Pil uyarıları için delay-load `winmm.dll` ile `Windows default` ses seçeneği ekler; güncelleme bildirim politikasını değiştirmez. |
| **Görev 9:** Akşam şarj hatırlatıcısı | **tamam** | `15ce44a` / `60a3477` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/battery_history.cpp`, `src/battery_history.h`, `src/evening_reminder.h`, `src/main.cpp`, `tests/evening_reminder_test.cpp`, `docs/agents/improvement-plan.md` | Seçilen saatte (`Off` / `20:00`–`23:00`, varsayılan kapalı) günde en fazla bir kez düşük pilli veya 24 saat içinde bitecek aktif cihazları bildiren kuralı ekler. |
| **Görev 10:** AMD CPU sıcaklığı | **tamam** | `12d1b96`, `fca2a60` | `CONTRIBUTING.md`, `Makefile`, `README.md`, `helper/cputemp.rc`, `helper/cputemp_helper.cpp`, `installer/THIRD_PARTY_NOTICES.txt`, `lang/_template.txt`, `lang/tr.txt`, `res/pawnio/AMDFamily17.bin`, `res/pawnio/README.md`, `src/cputemp.h`, `src/main.cpp`, `tests/cputemp_test.cpp` | Saf Tctl/Tdie aritmetiğine ek olarak SHA-256 doğrulamalı resmi PawnIO `AMDFamily17.bin` modülünü `TaskbarVitalsTemp.exe` içine gömer ve AMD Ryzen SMN okumasını etkinleştirir. |
| **Görev 11:** Pil sağlığı (yıpranma) takibi | **tamam** | `0dc0cc0` / `d6084f6` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/battery_health.h`, `src/battery_history.cpp`, `tests/battery_health_test.cpp`, `docs/agents/improvement-plan.md` | Cihaz başına 365 günlük özet ve ≥%95→≤%20 deşarj döngülerini ayrı dosyada saklayıp geçmiş penceresinde pil sağlığı değişimini raporlar. |
| **Görev 12:** Yerel salt-okunur canlı API (named pipe) | **tamam** | `22eb4b5` | `Makefile`, `README.md`, `lang/_template.txt`, `lang/tr.txt`, `src/live_api_json.h`, `src/live_api_pipe.cpp`, `src/live_api_pipe.h`, `src/main.cpp`, `tests/live_api_test.cpp` | Varsayılan kapalı, yalnızca geçerli kullanıcı SID'ine okuma izni veren ve `PIPE_REJECT_REMOTE_CLIENTS` kullanan salt-okunur `\\.\pipe\TaskbarVitals` JSON akışını ekler. |
| **Görev 13:** Uyumluluk listesini aranabilir site sayfasına taşı | **tamam** | `e3d7c6c` / `094693a` | `Makefile`, `README.md`, `scripts/generate-devices-json.ps1`, `site/app.js`, `site/compatibility.html`, `site/compatibility.js`, `site/devices.json`, `site/index.html`, `site/styles.css`, `tests/devices_json_test.cpp`, `docs/agents/improvement-plan.md` | README matrisindeki 78 cihaz/aile satırını `scripts/generate-devices-json.ps1` ile `site/devices.json` ve aranabilir `site/compatibility.html` sayfasına taşır. |
| **Görev 14:** AMD/Intel GPU watt değeri | **tamam** | `083df09` | `CONTRIBUTING.md`, `README.md`, `src/gpumon.cpp`, `src/gpumon.h`, `tests/gpumon_test.cpp` | Delay-load AMD ADLX (`amdadlx64.dll`) ve Intel IGCL (`ControlLib.dll`) çağrılarını sıfır harici SDK başlığıyla `gpumon` yapısına ekler ve NVML ile birlikte tek arayüzde sunar. |
| **Görev 15:** Linux isim tutarlılığı (`wireless-battery` -> `taskbar-vitals`) | **tamam** | `19c9ce6` | `.gitignore`, `CONTRIBUTING.md`, `Makefile`, `README.md`, `build-linux.sh`, `linux/70-taskbar-vitals.rules`, `linux/Makefile.linux`, `linux/taskbar-vitals.desktop`, `linux/taskbar-vitals.svg`, `src/linux/main_linux.cpp`, `src/linux_paths.h`, `tests/linux_paths_test.cpp` | Linux ikili dosya, `.desktop`, ikon, udev kuralı ve XDG yapılandırma yollarını eski yapılandırma geriye uyumluluğuyla `taskbar-vitals` adına taşır. |
| **Görev 16:** `main.cpp` modülerleştirme | **tamam** | `d68e6ec`, `ed5f868`, `3a795ab`, `caca6bc` | `CONTRIBUTING.md`, `Makefile`, `src/app_commands.cpp`, `src/app_commands.h`, `src/app_settings.cpp`, `src/app_settings.h`, `src/app_state.h`, `src/command_routing.h`, `src/main.cpp`, `src/notifications.cpp`, `src/notifications.h`, `src/settings_port.h`, `tests/command_routing_test.cpp`, `tests/settings_port_test.cpp` | Önce ayar JSON/komut yönlendirme testlerini genişletip ardından kayıt defteri I/O (`app_settings`), bildirim/OSD (`notifications`) ve menü/panel komutlarını (`app_commands`) `main.cpp` içinden ayrı birimlere taşır. |
| **Ek:** `winget` / `Scoop` paketleme manifest taslakları | **tamam** | `663679e` | `Makefile`, `installer/packaging/README.md`, `installer/packaging/scoop/taskbar-vitals.json`, `installer/packaging/winget/manifests/s/symNistaken/TaskbarVitals/2.0.0/symNistaken.TaskbarVitals.{yaml,installer.yaml,locale.en-US.yaml}`, `scripts/update-packaging-manifests.ps1`, `tests/packaging_manifests_test.cpp` | `winget` 1.6.0 ve Scoop manifest taslaklarını, `SHA256SUMS.txt` özetlerini basan `scripts/update-packaging-manifests.ps1` betiğini ve doğrulama testini ekler. |

---

## 2. Elle Test Etmen Gerekenler (Kontrol Listesi)

Uygulamayı `TaskbarVitals.exe` üzerinden Windows masaüstünde çalıştırıp aşağıdaki adımları sırayla doğrulayabilirsin (yalnızca `Poll less during full-screen games` ve `In-game alerts: Only in full screen` varsayılan açıktır; diğer yeni özellikler test sırasında ayarlardan açılmalıdır):

### A. Laptop Pili (Görev 1)
1. **Masaüstü PC'de kontrol:** `Settings -> System` sekmesini aç. `Laptop battery` kartının gri ve devre dışı (`No battery`) göründüğünü, şeritte ekstra bir öğe çıkmadığını doğrula.
2. **Dizüstü bilgisayarda kontrol:** `Settings -> System -> Show laptop battery` anahtarını aç.
3. Görev çubuğundaki sistem şeridinde pil yüzdesinin ve şarj (`⚡`) / ≤%15 kırmızı renk kurallarının doğru çalıştığını, tooltip'te kalan sürenin (`{hours} h {minutes} min remaining`) göründüğünü kontrol et.
4. ↑ / ↓ düğmeleriyle şerit sırasını değiştirip yeniden başlattıktan sonra sıranın korunduğunu doğrula.

### B. Ağ Hızı (Görev 3)
1. `Settings -> System` sekmesinden `Show network speed` anahtarını aç.
2. Dosya indirirken/yüklerken 2 saniyelik aralıkla `↓... ↑...` değerlerinin güncellendiğini, tooltip'te tam hızların okunduğunu ve anahtar kapatılınca `iphlpapi.dll` kaynağının serbest kaldığını kontrol et.

### C. Çoklu Monitör Tam Ekran Farkındalığı & Oyun İçi Mini Uyarı (Görev 6 & 7)
1. `Settings -> General` altında `Poll less during full-screen games` ayarının varsayılan olarak açık olduğunu, `Settings -> Batteries` altında `In-game alerts` ayarının `Only in full screen` (Akıllı Otomatik) olduğunu doğrula.
2. **İki monitörlü kurulumda:** Oyun monitöründe tam ekran pencere açıkken düşük pil uyarısı tetiklendiğinde OSD uyarısının oyunun üzerini kapatmadan serbest olan diğer monitöre çıktığını doğrula.
3. **Oyun monitöründe veya tek ekranda:** `In-game alerts` seçeneğini `Full screen (game monitor)` veya `Always` yaparak borderless fullscreen üzerinde seçilen köşede (~3–4 sn) göründüğünü, klavye/fare odağını çalmadığını, tıklamaların arkadaki oyuna geçtiğini (`WS_EX_TRANSPARENT`) ve farklı DPI değerlerinde düzgün ölçeklendiğini doğrula.

### D. CPU / GPU Telemetry (Görev 2, 10 & 14)
1. **AMD Ryzen CPU Sıcaklığı (Görev 10):** PawnIO kurulu AMD Ryzen sistemde `Show CPU temperature` açıldığında `TaskbarVitalsTemp.exe`'nin `AMDFamily17.bin` modülünü yükleyip Tctl/Tdie sıcaklığını raporladığını doğrula.
2. **AMD / Intel GPU Gücü (Görev 14):** AMD Radeon (`amdadlx64.dll`) veya Intel Arc/iGPU (`ControlLib.dll`) sistemde `Show GPU power (watts)` açıldığında kart gücünün watt (`W`) olarak göründüğünü, kapatılınca DLL'in serbest bırakıldığını kontrol et.
3. **Sıcaklık Renkleri (Görev 2):** `Temperature colours` altında `Strict (70 / 85 °C)` seçip renk değişimi ile kalıcılığı kontrol et.

### E. Canlı Yerel JSON Pipe (Görev 12)
1. `Settings -> General` altından `Local JSON pipe (\\.\pipe\TaskbarVitals)` anahtarını aç.
2. README'deki PowerShell istemci örneğini çalıştırarak güncel cihazlar ve sistem metriklerini içeren tek satır geçerli JSON döndüğünü ve kapatıldığında pipe'ın kapandığını doğrula.

### F. Diğer Yeni Özelliklerin Hızlı Doğrulaması (Görev 4, 5, 8, 9, 11, 13 & 16)
- **Cihaz yoksayma (Görev 4):** Cihaz kartı `⋯ -> Ignore this device...` ve `Ignored devices... -> Unignore {id}`.
- **Ayar ve CSV dışa/içe aktarma (Görev 5 & 16):** `Export settings...` JSON çıktısında `Run` / `Autostart` olmadığını ve yeni ayarların (`PollLessFullscreen`, `LocalApiPipe`, `InGameAlerts`, `InGameAlertCorner`, `AlertSound`, `EveningReminderHour`) `Import settings...` ile geri yüklendiğini doğrula; pil geçmişi penceresinden `Export CSV...` çıktısını kontrol et.
- **Uyarı sesi & Akşam hatırlatıcısı (Görev 8 & 9):** `Alert sound` (`Windows default`) ve `Evening charge reminder` (`20:00`–`23:00`).
- **Pil sağlığı & Web sayfası (Görev 11 & 13):** Geçmiş penceresinde `Battery health` alanı ve `site/compatibility.html` araması.
- **Linux (Görev 15):** Linux ortamında `./build-linux.sh && ./build-linux.sh test`, eski `/etc/udev/rules.d/70-wireless-battery.rules` dosyasının kaldırılıp `linux/70-taskbar-vitals.rules` kopyalanması.

---

## 3. Kullanıcı Tarafındaki Harici Yayın Adımları

- **Kod İmzalama:** SignPath Foundation başvurusu kullanıcı tarafından yapılacak.
- **Sürüm Paketleme & winget / Scoop Gönderimi:** Sürüm etiketinden önce Windows ortamında Inno Setup ile `powershell -NoProfile -ExecutionPolicy Bypass -File installer\build-installer.ps1 -Version 2.0.0` çalıştırılmalı, ardından `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\update-packaging-manifests.ps1 -Version 2.0.0` ile gerçek SHA-256 özetleri manifestlere işlenip `winget validate` sonrasında `microsoft/winget-pkgs` ve Scoop depolarına PR açılmalıdır.

---

## 4. Kesinti Kaydı

- **Gece Kotası (Görev 7):** `2026-11-24T04:05:31+03:00` civarında Görev 7 alt ajanı `HTTP 429` kota limitine takıldı; yarım ajan sonlandırılıp `05:09:50+03:00` için kurulan tek seferlik zamanlayıcı sonrasında temiz alt ajanla tamamlandı (`fdc7632`).
- **Gündüz Devam (Görev 15):** `src/linux/main_linux.cpp` üzerindeki yanlışlıkla `main` çalışma dizinine yazılan değişiklik fark edilip `toadfish` dalına alındı ve `D:\saymin\Proje\WirelessBattery` (`main`) üzerindeki kopya `git checkout` ile sıfırlandı.

---

## 5. Son Derleme ve `make test` Çıktısının Özeti

Tüm görevlerin tamamlanmasının ardından temiz derleme ve test komutları (`build.bat 2.0.0 test` ve `mingw32-make SHELL=sh.exe clean && mingw32-make SHELL=sh.exe -j12 VERSION=2.0.0 CHANNEL=test && mingw32-make SHELL=sh.exe test VERSION=2.0.0 CHANNEL=test`) **0 çıkış koduyla** ve yeni eklenen modüllerde **0 uyarıyla** geçti:

- **İkili Dosyalar:** `TaskbarVitals.exe` ve `TaskbarVitalsTemp.exe` (hem `build.bat` hem temiz `mingw32-make` ile).
- **Toplam 62 Test Çalıştırılabilirinin Tamamı (62/62 Geçti):**
  - **Güncelleme Testleri (3/3):** `update_core_test.exe`, `update_win_test.exe`, `update_signature_test.exe`.
  - **Politika, Sistem, IPC, Linux, Komut Yönlendirme ve Paketleme Testleri (59 hedef içinde yeni/güncel öne çıkanlar):**
    - `cputemp_test.exe` (Intel MSR + AMD Tctl/Tdie + CPUID vendor tespiti + görev tanımı)
    - `gpumon_test.exe` (LUID, motor kullanımı + `RoundGpuWatts` + `IgclWattsFromEnergyDelta`)
    - `refresh_policy_test.exe` & `osd_policy_test.exe` (çoklu monitör Smart Auto ve tam ekran aralığı)
    - `live_api_test.exe` (JSON şeması + SID ACL / `PIPE_REJECT_REMOTE_CLIENTS` canlı pipe doğrulaması)
    - `linux_paths_test.exe` (`taskbar-vitals` XDG yolları, eski config göçü ve LF satır sonları)
    - `settings_port_test.exe` (46 ayar alanı gidiş-dönüş ve sınır doğrulaması)
    - `command_routing_test.exe` (`MenuId` çakışmasızlık, cihaz ve sistem sıra çözücüleri)
    - `packaging_manifests_test.exe` (Scoop JSON + winget 1.6.0 YAML şeması + `TaskbarVitals.iss` `ProductCode` eşleşmesi)
    - Ayrıca tüm mevcut 43 sürücü/protokol/etiket/marka/uyumluluk/pil geçmişi ve sağlığı testleri eksiksiz geçti.
