# AlptugDUB

**Seslendirme ve dublaj için doğal ses karakterleri: iri bir adam, küçük bir kız, bir canavar, bir cadı… hepsi kendi sesinizden, yapaylık olmadan.**
*Natural voice characters for voice-over and dubbing, made from your own voice. English below.*

![AlptugDUB](docs/screenshot.png)

VST3 · Audio Unit · macOS 11+ · Apple Silicon ve Intel (universal) · Premiere Pro, Audition, DaVinci Resolve (Fairlight), Logic Pro, Reaper

---

## Ne yapar

AlptugDUB önce sesinizi dinler ve öğrenir (perde aralığı, ses tipi, formantlar, parlaklık, nefes), sonra perdeyi ve
gırtlağı (formantları) birbirinden bağımsız, faz uyumlu ve şeffaf biçimde değiştirir. Efekt sabit değildir: her ses
darbesi, o anki perdeye, şiddete ve tonlamaya göre yeniden şekillenir. Yankı ya da süsleme eklemez; yalnızca sesin
kendisini dönüştürür.

- **Yapay zekâ ile perde analizi.** [SwiftF0](https://github.com/lars76/swift-f0) sinirsel perde modeli eklentinin
  içinde, makinenizde çalışır (internet yok, ek kurulum yok). Fısıltıyı, nefesli sesi ve gırtlak cızırtısını (vocal fry)
  doğru izler; oktav hatası yapmaz. Orijinal modelle sayısal olarak doğrulandı: perde farkı < 0,01 sent.
- **Doğal ses motoru (TD-PSOLA).** Her gırtlak darbesi ayrı bir tane olarak yeniden dizilir: perde değişir, formantlar
  yerinde kalır ya da istenen kadar kayar. "s", "ş", "f" gibi sessiz harfler hiç perdelenmez, dokuları bozulmaz.
  Stereo ses tek bir analizle işlendiği için faz kayması olmaz.
- **Geniş motor.** Müzik altındaki ses, koro ya da gürültülü kayıtlar için spektral motor (Signalsmith Stretch).
- **13 karakter**, sesinize göre ayarlanır: Doğal, İri adam, Dev, Canavar, Erkek, Kadın, Küçük kız, Küçük oğlan,
  Yaşlı adam, Yaşlı kadın, Cadı, Cüce, Goblin.
- **Az düğme, çok yapay zekâ.** Perde ve Gırtlak büyük düğmelerdir; Tonlama, Yaş (−50 genç … +50 yaşlı), Hırıltı,
  Nefes, Karışım ve Çıkış küçük düğmelerdir. Geri kalanını anahtarlar yönetir:

  | Anahtar | Ne yapar |
  | --- | --- |
  | Yapay zekâ perde analizi | Sinirsel modeli açar (≈275 ms gecikme); kapalıyken hızlı analiz (≈110 ms) |
  | Karakteri sesime uyarla | Ses öğrenildikçe karakterin perdesini ve gırtlağını sürekli yeniden hesaplar |
  | Doğal gırtlak hareketi | Perde yükselip alçaldıkça gırtlak da gerçek bir ses gibi hareket eder |
  | Dinamik tepki | Hırıltı, titreme ve nefes sesin şiddetini izler: bağırınca sertleşir, fısıldayınca yumuşar |
  | Akıllı ton dengesi | Kalınlaşan sese göğüs, incelen sese hava ekler; ton donuklaşmaz |
  | Titreşimi yumuşat | Perdedeki titrekliği azaltır: daha kontrollü, oturaklı bir ses |
  | Seviyeyi koru | İşlenmiş ses, orijinali kadar yüksek kalır |
  | Gırtlak perdeyi izlesin | Formantlar perdeyle birlikte biraz kayar |

  Genizden, Netlik, Gövde, Hava, Tepki, Hareket ve Kararlılık ayarları DAW otomasyonunda da açıktır.
- **Güvenli çıkış.** Zincirin sonunda yalnızca aşımlarda çalışan −0,3 dBFS limitleyici; NaN ya da patlama asla çıkmaz.

## Kurulum

1. [Releases](https://github.com/AlptugDag/AlptugDUB/releases) sayfasından `AlptugDUB-macOS.zip` dosyasını indirin ve açın.
2. Terminal'de klasörün içinde çalıştırın:

   ```bash
   sh install.sh
   ```

   (Tüm kullanıcılar için: `sudo sh install.sh -a`.) Betik eklentileri `~/Library/Audio/Plug-Ins` altına kopyalar ve
   indirme karantinasını kaldırır.
3. DAW'ınızda eklentileri yeniden taratın. AlptugDUB, üretici **Alptuğ Dağ** altında görünür.

Gecikme (latency) DAW'a bildirilir; Premiere, Audition, Resolve, Logic ve Reaper bunu otomatik olarak dengeler.

## Kaynaktan derleme

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --target AlptugDUB_VST3 AlptugDUB_AU
```

JUCE 9.0.4 ve Signalsmith Stretch 1.4.0 derleme sırasında sabit sürümleriyle indirilir. Xcode Command Line Tools yeterlidir.

Test araçları:

- `build/dubtest giriş.wav çıkış.wav <perde> <formant>`: perde ve formant doğruluğu, kare-kare sapma, tıklama ve seviye ölçer.
- `AlptugDUBSnapshot --stress`: 44,1/48/96 kHz, mono/stereo, değişken blok boyutları, tüm karakterler ve motor değişimleri.
- `tools/swiftf0/check.py`: C++ sinirsel modeli orijinal ONNX modeliyle karşılaştırır.

## Doğrulama

- Steinberg VST3 validator: **47/47 test geçti**
- Apple `auval -strict`: **AU VALIDATION SUCCEEDED**
- Stres testi: 18 yapılandırmanın tamamı geçti; CPU yükü tek çekirdeğin ≈ %3'ü

---

## English

AlptugDUB first learns the voice (pitch range, voice type, formants, brightness, breathiness), then moves pitch and
formants independently, phase-coherently and transparently. Every glottal pulse is shaped from that moment's pitch,
effort and melody, so nothing is a fixed effect. No reverb, no gimmicks: only the voice itself is transformed.

- On-device neural pitch tracking (SwiftF0, MIT) compiled into the plug-in, verified against the original ONNX model
  (< 0.01 cent difference). Follows whispers, breathy voices and vocal fry without octave errors.
- Natural engine: real-time TD-PSOLA, pitch marks on the glottal pulses, unvoiced consonants never re-pitched, stereo
  driven by one mono analysis. Wide engine: Signalsmith Stretch for voices over music, choirs and noisy takes.
- 13 characters tuned to the learned voice; two big knobs (Pitch, Formant), six small ones, eight AI switches.
- Reported latency: about 275 ms with AI analysis, 110 ms without (compensated by every host listed above).
- Install: download the release zip, run `sh install.sh`, rescan plug-ins.

## Yapımcı / Author

<p>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/signature-light.svg">
    <img src="docs/signature-dark.svg" alt="Alptuğ Dağ" width="150">
  </picture>
</p>

**Alptuğ Dağ**: sinemacı, hikâye anlatıcı, seslendirme sanatçısı. / Filmmaker, storyteller, voice artist.

[Instagram @alptugdag](https://instagram.com/alptugdag) · [YouTube @AlptugDag](https://youtube.com/@AlptugDag) ·
[Letterboxd](https://letterboxd.com/AlptugDag/) · [LinkedIn](https://www.linkedin.com/in/AlptugDag/) ·
[GitHub](https://github.com/AlptugDag)

![Hakkında](docs/about.png)

## Lisans / License

Kod GNU AGPL 3.0 ile yayımlanır (`LICENSE`). Üçüncü taraf bileşenler ve görseller için `CREDITS.md`'ye bakın; fotoğraf,
imza ve "AlptugDUB" adı Alptuğ Dağ'a aittir.
