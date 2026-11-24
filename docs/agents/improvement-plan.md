# Taskbar Vitals: Geliştirme Planı (ajan için sıralı görev listesi)

Bu dosya, görevleri **sırayla** yapacak bir kodlama ajanı için yazıldı. Her görev tek başına ele alınabilir,
derlenebilir, test edilebilir ve commit'lenebilir. Bir görevi bitirmeden sonrakine geçme.

---

## 0. Başlamadan önce: Genel kurallar (her görevde geçerli)

1. **Önce oku:** `CONTRIBUTING.md`, `README.md` ("Live System Telemetry" ve "Settings Panel" bölümleri), `src/panel.h`,
   `src/alert_rules.h`, `src/sysmon.h`, `src/runtime_estimate.h`, `src/history_model.h`.
2. **Bir görev = bir commit.** Commit mesajı mevcut stili izlesin: kısa, İngilizce ve emir kipinde
   (örn. `Show the laptop battery on the system strip`). **`Co-authored-by` satırı ekleme**, çünkü PR safety bunu reddediyor.
3. **Her görevden sonra:** MSYS2 MINGW64 kabuğunda `make -j12` ve ardından `make test VERSION=2.0.0 CHANNEL=test` çalıştır.
   İkisi de geçmeden commit atma. (PowerShell'deki `make` çalışmaz; MSYS2 yoksa `build.bat` ile derle ve bunu raporda belirt.)
4. **Saf mantığı ayır:** Hesap ve politika kodu (eşikler, oran hesabı, zamanlama kararları) Windows'tan bağımsız bir
   header'a gitsin (`alert_rules.h` ve `refresh_policy.h` gibi). Bu header için `tests/` altına test yaz ve testi
   **`Makefile` içindeki `test:` hedefine elle kaydet** (testler otomatik bulunmuyor).
5. **Çeviri:** Kullanıcıya görünen her yeni metin `T(L"...")` veya `TF(...)` ile yazılsın. Her yeni ifadeyi
   `lang/_template.txt` dosyasına ekle. `lang/tr.txt` için Türkçe çeviriyi de ekle. Diğer diller boş kalabilir
   (boş kalan satırlar İngilizce görünür). `--list` ve `--dump` çıktıları İngilizce kalmalı.
6. **Varsayılanlar:** Mevcut kullanıcıların şeridini değiştiren yeni göstergeler ve özellikler **varsayılan olarak kapalı** olsun.
   Ayarlar kullanıcı tarafında `HKCU\Software\WirelessBattery` altında, mevcut `kSettingsKey` ile tutulur. Anahtar adını değiştirme.
7. **Sürücüler yalnızca okur.** Hiçbir cihaza yapılandırma yazılmaz. Ana exe'ye yeni bir yetki yükseltme yolu eklenmez.
   İsteğe bağlı DLL'ler yalnızca ilgili gösterge açıldığında yüklenir ve kapatılınca serbest bırakılır (`gpumon` ve `sysmon` gibi).
8. **README:** Her özellik için README'deki ilgili tabloyu güncelle. Yeni bölüm açma, mevcut tablolara satır ekle.
9. **🛑 DUR işareti:** Bir görev yeni bir binary veya üçüncü parti bağımlılık, yeni bir sürücü, yetki yükseltme ya da dış hesap
   gerektiriyorsa (veya yanında 🛑 varsa) o noktada dur ve kullanıcıya sor.
10. **İlerleme:** Bitirdiğin görevin kutusunu bu dosyada `[x]` yap ve yanına commit hash'ini yaz.
    Bir şeyi atladıysan veya sapma yaptıysan, görevin altına `> Not:` ile kısaca açıkla.

### Önemli teknik not: Sistem göstergesi sıralaması

`main.cpp` içinde `g_systemOrder{0,1,2,3}` (CPU, RAM, GPU, GPU memory) değeri, registry'deki `SystemOrder` DWORD'üne
**4 bitlik dilimler halinde ve en fazla 4 öğe** olarak yazılıyor (`LoadSystemOrder` / `SaveSystemOrder`). Yeni bir sistem
göstergesi eklendiğinde (Görev 1 ve 4):

- Format en az 8 öğeye genişletilmeli (DWORD'de 8 × 4 bit yer var).
- **Geriye uyumluluk** korunmalı: Eski bir DWORD okunduğunda eksik öğeler sona eklenmeli ve geçersiz değerler atılmalı.
- Bu okuma/yazma mantığını saf bir fonksiyona çıkar ve test et. Bunu ilk gösterge görevinde (Görev 1) yap.

---

## Aşama 1: Hızlı kazanımlar (düşük efor, yüksek etki)

### [x] Görev 1: Laptop'un kendi pilini sistem şeridinde göster (e41317c)
- **Amaç:** Dizüstü bilgisayar kullanıcıları, bilgisayarın kendi pilini de şeritte görsün.
- **Nasıl:** `GetSystemPowerStatus` kullan. `BatteryFlag == 128` (pil yok) veya `255` (bilinmiyor) durumunda göstergeyi
  System sekmesinde gri ve devre dışı göster.
  Gösterilecekler: yüzde, şarj durumu (`ACLineStatus` + `BatteryFlag & 8`) ve tooltip'te kalan süre (`BatteryLifeTime`, -1 değilse).
- **Ayar:** System sekmesinde "Laptop battery" kartı, açma/kapama anahtarı ve ↑/↓ sıralaması. Varsayılan: kapalı.
- **Görünüm:** Renk kuralları mevcut pil göstergeleriyle aynı (≤%15 kırmızı, şarjda ⚡).
- **Test:** Sıralama formatı göçü (yukarıdaki not) ve `SYSTEM_POWER_STATUS` → görüntü durumu dönüşümü için saf fonksiyon testleri.
- **Kabul:** Masaüstü PC'de kart gri görünür ve şeritte bir şey çıkmaz. Laptop'ta yüzde ve şarj durumu doğru görünür.
  Eski `SystemOrder` değeri bozulmadan yüklenir.

### [x] Görev 2: Sıcaklık renk eşiklerini ayarlanabilir yap (7c1213f)
- **Bugün:** `sysmon.h:23`, `HeatLevel()` → 80 °C turuncu, 90 °C kırmızı (sabit).
- **Yapılacak:** System sekmesine "Temperature colours" seçeneği ekle. Ön ayarlar: **Standard (80/90)** (varsayılan),
  **Laptop (85/95)**, **Strict (70/85)**. `HeatLevel` eşikleri parametre olarak alsın. Varsa yüksek sıcaklık uyarısı
  (commit `fb331f5`) da aynı eşiği kullansın.
- **Test:** `tests/sysmon_test.cpp` dosyasına eşik varyantları için test ekle.
- **Kabul:** Seçim registry'ye kaydedilir, yeniden başlatmadan sonra korunur ve şerit rengi anında değişir.

### [x] Görev 3: Ağ hızı göstergesi (↓ / ↑) (353bbf7)
- **Nasıl:** `iphlpapi.dll` içindeki `GetIfTable2`'yi delay-load ile yükle. Loopback, tünel ve bağlantısı kopuk arayüzleri
  hariç tut ve `InOctets` / `OutOctets` toplamını al. Mevcut 2 saniyelik örnekleme döngüsünde (`TickSystemReadings`) iki ölçüm
  arasındaki farktan hızı hesapla.
- **Biçim:** Şeritte kısa gösterim (`↓1.2M ↑85K`, birim B/s), tooltip'te tam değer. 32/64-bit sayaç taşması ve ilk örnek
  (önceki değer yok) güvenli şekilde ele alınmalı.
- **Ayar:** System sekmesinde "Network" kartı, anahtar ve ↑/↓ sıralama. Varsayılan: kapalı. Kapatılınca DLL serbest bırakılır.
- **Test:** Hız hesaplama ve biçimlendirme için saf header + test (`tests/netmon_test.cpp`).
- **Not:** Bu iş ağ çağrısı yapmaz, yalnızca sayaç okur. PR safety yine de uyarı verirse PR açıklamasında belirt.

### [x] Görev 4: Cihaz kartı menüsüne "Yoksay" ekle (registry düzenlemeden) (a13b099)
- **Bugün:** `IgnoreDevices` yalnızca elle registry'den düzenlenebiliyor (VID:PID listesi, virgülle ayrılmış).
- **Yapılacak:** Cihaz kartının `⋯` menüsüne "Ignore this device" ekle. Tıklanınca onay iste, ardından `DeviceStatus.vid` ile
  birlikte PID'yi `IgnoreDevices` listesine yaz. PID bugün `DeviceStatus` içinde tutulmuyorsa `pid` alanını ekle ve
  `PollContext::Add` içinde doldur.
- **Geri alma:** Batteries sekmesinin altına "Ignored devices…" eylemi ekle. Bu eylem listeyi gösterir ve tek tıkla geri almayı sağlar.
- **Kabul:** Yoksayılan cihaz bir sonraki yoklamada kaybolur. Geri alınca yeniden görünür. Elle yazılmış eski değerler bozulmaz.

### [x] Görev 5: Ayarları ve geçmişi dışa/içe aktar (64692e9)
- **Ayarlar:** General sekmesine "Export settings…" ve "Import settings…" ekle. Settings anahtarındaki değerler okunabilir bir
  JSON dosyasına yazılsın (mevcut `json_output.h` yardımcılarını kullan). İçe aktarırken yalnızca bilinen değer adlarını ve
  geçerli aralıkları kabul et. Bilinmeyen anahtarları yok say. Başlangıçla çalıştırma (Run key) ayarı **içe aktarılmasın**.
- **Geçmiş:** Pil geçmişi penceresine "Export CSV…" ekle. Sütunlar: `device_key, device_name, utc_iso8601, percent, charging`.
- **Test:** JSON doğrulama/filtreleme ve CSV satır biçimi için saf fonksiyon testleri.

---

## Aşama 2: Oyuncu deneyimi ve akıllı uyarılar

### [x] Görev 6: Tam ekran farkındalığı (yoklama sıklığını azaltma) (0cc20ec, 07afca6)
- **Nasıl:** `SHQueryUserNotificationState` sonucu `QUNS_RUNNING_D3D_FULL_SCREEN`, `QUNS_BUSY` veya `QUNS_PRESENTATION_MODE`
  iken pil yoklama aralığını uzat (örn. en az 5 dakika). Sistem metrik örneklemesi devam etsin.
- **Ayar:** General sekmesinde "Poll less during full-screen games". Varsayılan: açık.
- **Test:** "Durum + kullanıcı ayarı → yoklama aralığı" kararı saf bir fonksiyonda olsun ve test edilsin
  (`refresh_policy.h` dosyasına eklenebilir).
> Not: Kullanıcı onayıyla (`07afca6`) `PollLessFullscreen` varsayılan olarak açık (`true`) yapıldı ve ön plan tam ekran penceresi + çoklu monitör tespiti (`refresh_policy.h`, `osd_overlay.cpp`) iyileştirildi.

### [x] Görev 7: Oyun içi mini uyarı (OSD-lite) (fdc7632, 07afca6)
- **Sorun:** Windows tam ekranda balon ve toast bildirimlerini bastırıyor. Kulaklık maçın ortasında habersizce kapanıyor.
- **Yapılacak:** Düşük pil uyarısı tetiklendiğinde, Görev 6'daki durum "tam ekran/meşgul" ise birincil monitörün köşesinde
  3–4 saniyelik bir mini pencere göster.
  - Pencere stilleri: `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`.
  - Fokus çalmamalı, tıklamaları geçirmeli ve yarı saydam olmalı.
  - İçerik: cihaz simgesi, adı ve yüzdesi.
- **Ayar:** "In-game alerts": Off / Only in full screen (varsayılan) / Always. Köşe seçimi: sağ üst (varsayılan) veya diğer köşeler.
- **Sınırlama:** Exclusive fullscreen modunda pencere görünmeyebilir (borderless modda görünür). Bunu README'deki
  "Known Limitations" bölümüne yaz.
- **Kabul:** Oyun penceresinden fokus kaçmaz, OSD süresi dolunca kaybolur, DPI ölçeklemesi doğru çalışır.
> Not: Kullanıcı onayıyla (`07afca6`) `In-game alerts` varsayılanı `Only in full screen` (Akıllı Otomatik: 2+ ekranda oyun dışı serbest monitör, yoksa oyun monitörü) yapıldı ve `Full screen (game monitor)` seçeneği eklendi.

### [x] Görev 8: Uyarı sesi seçeneği (609a3bf)
- Pil uyarıları için Batteries sekmesindeki "Battery alerts" kartına "Alert sound" ekle: Off / Windows default
  (`PlaySoundW(L"SystemExclamation", nullptr, SND_ALIAS | SND_ASYNC)`).
- Güncelleme bildirim politikası değişmez (CONTRIBUTING: güncelleme durumu yalnızca panelde gösterilir).

### [x] Görev 9: Akşam şarj hatırlatıcısı (15ce44a)
- **Fikir:** Kullanıcının seçtiği saatte (varsayılan 22:00, özellik varsayılan olarak kapalı) şu iki koşuldan birini sağlayan
  cihazlar için tek bir bildirim göster:
  - yüzdesi bir eşiğin altında (varsayılan %30), veya
  - `runtime_estimate.h` tahminine göre önümüzdeki 24 saat içinde bitecek.

  Örnek metin: *"Charge tonight: Mouse 22% (about 9 h left), Headset 31%."*
- **Kurallar:** Günde en fazla bir kez gösterilir. PC o saatte uykudaysa uyandığında, aynı gün ve gece 03:00'ten önceyse bir kez
  gösterilir. Şarjda olan ve uykudaki cihazlar hariç tutulur.
- **Test:** "Saat + son gösterim zamanı + cihaz listesi → bildirim metni veya hiçbir şey" kararı saf bir header'da olsun
  ve test edilsin.
- **Ayar:** "Battery alerts" kartında "Evening charge reminder": Off / 20:00 / 21:00 / 22:00 / 23:00.

---

## Aşama 3: Platform eşitliği ve derin özellikler

### [x] Görev 10: AMD CPU sıcaklığı 🛑 (12d1b96, fca2a60)
- **Bugün:** Yardımcı program yalnızca Intel MSR'lerini (`0x1A2`, `0x1B1`) okuyor ve `res/pawnio/IntelMSR.bin` modülünü gömüyor.
- **Yapılacak:** PawnIO'nun AMD modülüyle (Family 17h ve sonrası, SMN üzerinden Tctl) Ryzen sıcaklığını oku. Tctl → Tdie
  ofsetlerini model bazında uygula (bkz. Linux `k10temp`). Sıcaklık aritmetiğini `cputemp.h` içinde saf ve test edilebilir tut.
- **🛑 Başlamadan önce kullanıcıya sor:** Bu görev yeni bir binary gömmeyi (PawnIO AMD modülü, LGPL), CONTRIBUTING'deki
  "yalnızca `0x1A2` / `0x1B1`" kuralını genişletmeyi ve `THIRD_PARTY_NOTICES` güncellemesini gerektiriyor.
  Onay gelmeden yalnızca saf aritmetiği ve testleri yaz.
> Not: İlk aşamada saf aritmetik (`12d1b96`) eklendi; kullanıcı onayıyla (`fca2a60`) resmi PawnIO `AMDFamily17.bin` modülü SHA-256 doğrulanarak `res/pawnio/AMDFamily17.bin` olarak eklendi ve `TaskbarVitalsTemp.exe` helper, `CONTRIBUTING.md`, `res/pawnio/README.md` ve `installer/THIRD_PARTY_NOTICES.txt` güncellendi.

### [x] Görev 11: Pil sağlığı (yıpranma) takibi (0dc0cc0)
- **Yapılacak:**
  - 30 günlük ham geçmişe ek olarak, cihaz başına **günlük özetler** tut ve bunları 365 gün sakla (ayrı bir dosya, boyutu sınırlı).
  - Tam şarjdan (≥%95) düşük seviyeye (≤%20) kadar süren kesintisiz deşarj döngülerini tespit et ve her döngünün saat
    cinsinden "%100'e normalize süre" değerini sakla.
  - Geçmiş penceresinde bunu göster: *"Full charge lasted about 38 h in the first month, about 29 h recently (−24%)."*
    En az 3 döngü yoksa "Not enough data yet" yaz.
- **Test:** Döngü tespiti (şarj kesintileri, uyku boşlukları) ve normalize süre hesabı için kapsamlı saf testler.
- **Not:** Geçmiş dosyası formatı değişecekse eski dosyayı okumaya devam et (göç kodu + test).

### [x] Görev 12: Yerel salt-okunur canlı API (named pipe) 🛑 (22eb4b5)
- **Fikir:** Stream Deck, Rainmeter ve Home Assistant gibi entegrasyonlar için `\\.\pipe\TaskbarVitals` adlı bir pipe açılsın.
  Bağlanan istemci, `--json` biçimindeki cihazlar ve sistem metriklerini içeren tek bir JSON anlık görüntüsü alsın.
  Hiçbir komut kabul edilmez.
- **Güvenlik:** Pipe yalnızca mevcut kullanıcının SID'ine açık olsun (`cputemp.h`'deki ACL yaklaşımına bak).
  `PIPE_REJECT_REMOTE_CLIENTS` kullan. Varsayılan: kapalı.
- **🛑 Kullanıcıya sor:** PR safety bunu IPC olarak işaretleyecek ve bu bir tasarım kararı. Onaydan sonra uygula.
  README'ye örnek bir PowerShell istemcisi ekle.

### [x] Görev 13: Uyumluluk listesini aranabilir site sayfasına taşı (e3d7c6c)
- README'deki "Full A–Z Hardware Matrix" tablosunu `site/` altında arama kutulu bir sayfaya taşı (model adı veya `VID:PID`
  ile arama). Veriyi `site/devices.json` dosyasında tut. Bu dosyayı README tablosundan üreten küçük bir script yazabilirsin
  (`scripts/`).
- README'de tam matris yerine bu sayfaya giden bir bağlantı ve öne çıkan markaların tablosu kalsın.
- `.github/workflows/pages.yml` ile uyumlu olmalı.

---

## Aşama 4: İsteğe bağlı ve büyük işler (kullanıcı onayıyla başla)

- [x] **Görev 14: AMD/Intel GPU watt değeri:** ADLX (AMD) ve IGCL (Intel) delay-load ile, `gpumon` yapısında. (083df09)
- [x] **Görev 15: Linux isim tutarlılığı:** `wireless-battery` → `taskbar-vitals` (binary, `.desktop`, udev kuralı).
  Eski udev dosyası ve config/autostart geçişi eklendi. (19c9ce6)
- [x] **Görev 16: `main.cpp` modülerleştirme** (`settings_port.h` + `command_routing.h` + `app_state.h` + `app_settings.*` + `notifications.*` + `app_commands.*` olarak 4 adımda taşındı). (d68e6ec, ed5f868, 3a795ab, caca6bc)
  > Not: 2.0.0 birleştirmesinde bu bölme main'e gelmemişti; güncel main.cpp üzerinden yeniden çıkarıldı (9aa7a52, 3009859, 1d16f90).

## Ajanın yapamayacağı işler (kullanıcı yapar, ajan yalnızca taslak hazırlar)

- **Kod imzalama:** SignPath Foundation başvurusu için README ve workflow taslağını ajan hazırlayabilir.
- **winget / Scoop:** Taslak manifestler ve doğrulama/hash betiği `installer/packaging/` ve `scripts/update-packaging-manifests.ps1` altına eklendi (`663679e`). Sürüm etiketinden sonra gönderimi kullanıcı yapar.
