# AlptugDUB 3.0

**Seslendirme ve dublaj için hibrit ses yeniden üretim motoru, uyarlanabilir stüdyo Auto-Tune ve Liquid Glass analog konsol: iri bir adam, küçük bir kız, canavar, cadı… hepsi kendi sesinizden.**
*Hyper-realistic hybrid voice regeneration & studio vocal Auto-Tune for voice-over and dubbing. English below.*

![AlptugDUB 3.0](docs/screenshot.png)

VST3 · Audio Unit · Standalone · macOS 11+ · Apple Silicon ve Intel (Universal) · Web Studio (`web/`)

---

## v3.0.0 Mühendislik Yeniden Yapılandırması

AlptugDUB 3.0.0, v2.0.0 C++ JUCE eklenti motorunda tespit edilen **6 temel DSP kusurunu** gidererek sıfır gecikme-örtüşmeli doğrusal konvolüsyon, stüdyo ön işleme zinciri ve genişletilmiş Auto-Tune denetimleriyle yerel VST3/AU/Standalone eklentisini yeniden yapılandırır:

1. **Sıfır Dolgulu (2048-pt) Doğrusal Konvolüsyon & F0-Uyarlamalı Gerçek Zarf (`VocalTractWarper`):** 1024 örnekli Hann penceresi + 2x sıfır dolgulu 2048-pt FFT ile dairesel konvolüsyon örtüşmesi ortadan kaldırıldı; 3 iterasyonlu Imai/Röbel True-Envelope ve F0-uyarlamalı kepstral kesim (`0.72 × T0`) ile bas seslerdeki F1/F2 formant erimesi giderildi.
2. **Tek Türevli DC-Dengeli LF Glottal Tablosu & Uyumlu PSOLA Enerji Normalizasyonu (`HybridRegenerator`):** v2.0'daki çift türev kaynaklı tiz metalik cızırtı kaldırılarak bant-sınırlı DC-dengeli LF glottal akış türevi doğrudan örneklendi; yukarı/aşağı perde kaymalarında `√(T_out / T_in)` PSOLA enerji dengelemesi uygulandı.
3. **Giriş Ön İşleme Zinciri (`StudioDynamics` + `PitchTracker`):** 2. derece Butterworth HPF (`40–160 Hz`, varsayılan `75 Hz`), yumuşak gürültü kapısı (`-72..-24 dB`) ve dinamik `De-Esser` (`5.2–10.5 kHz`) eklendi; YIN perde izleyicisine oktav sürekliliği koruması eklenerek alt frekans uğultularına kilitlenme engellendi.
4. **Çok İpuçlu Ünsüz Koruma (`Consonant Guard`):** Sıfır-geçiş hızı (`ZCR`), yüksek/alçak bant enerji oranı (`3.2–12 kHz` vs `100–1200 Hz`) ve seslilik güveni birlikte değerlendirilerek `s, ş, f, ç, t, k, p` ünsüzleri perde/formant hattından faz-uyumlu biçimde korunur.
5. **Uyarlanabilir Stüdyo Auto-Tune (`AutoTune`):** Hızlı melisma atlamalarındaki 90 ms gecikme giderildi (`>0.68 st` sıçramalarda anında kilit); bağımsız **Tepki Hızı (`5–120 ms`)**, **Vibrato Koruma (`0–100%`)** ve **Manuel Gam/Kök Ton Kilidi** (`Otomatik`, `Majör`, `Minör`, `Kromatik`, `Hicaz`, `Pentatonik`) eklendi.
6. **3 ms Lookahead `−0,3 dBFS` True-Peak Limiter & ITU-R BS.1770 K-Ağırlıklı Seviye Eşleme (`StudioDynamics`):** Ani 0 ms kırpma yerine stereo-bağlı 3 ms lookahead yumuşak limitleyici (canlı `LIM GR dB` göstergeli) ve `-48 dBFS` kapılı K-ağırlıklı seviye eşleme ile nefes aralarındaki seviye pompalaması giderildi.

## Kurulum (macOS VST3 & Audio Unit)

1. [Releases](https://github.com/AlptugDag/AlptugDUB/releases/latest) sayfasından `AlptugDUB-3.0.0-macOS.zip` dosyasını indirin ve açın.
2. Terminal'de klasörün içinde çalıştırın:

   ```bash
   sh install.sh
   ```

   (Tüm kullanıcılar için: `sudo sh install.sh -a`.) Betik eklentileri `~/Library/Audio/Plug-Ins` altına kopyalar, indirme karantinasını kaldırır ve `AudioComponentRegistrar`'ı yeniler.
3. DAW'ınızda eklentileri yeniden taratın. AlptugDUB, üretici **Alptuğ Dağ** altında görünür.

## Kaynaktan Derleme

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --target AlptugDUB_VST3 AlptugDUB_AU AlptugDUB_Standalone AlptugDUBSnapshot AlptugDUBTests -j
```

JUCE 9.0.4 ve Signalsmith Stretch 1.4.0 derleme sırasında sabit sürümleriyle indirilir. Xcode Command Line Tools yeterlidir.

Doğrulama araçları:

- `./build/AlptugDUBTests`: 6 mühendislik düzeltmesi ve 2x sıfır dolgulu doğrusal OLA deterministik C++20 doğrulama paketi.
- `./build/AlptugDUBSnapshot_artefacts/Release/AlptugDUBSnapshot --stress`: 44,1/48/96 kHz, mono/stereo, değişken blok boyutları, tüm 13 karakter stres testi.
- `auval -v aufx Adub Alpt`: Apple Audio Unit doğrulaması.

## Web Studio Konsolu (`web/`)

Tarayıcı içi Web Audio konsolu ve görsel mühendislik test tezgâhı `web/` dizininde yer alır:

```bash
cd web
npm install
npm test
npm run dev
```

---

## English

AlptugDUB 3.0.0 is a **native macOS Universal (Apple Silicon + Intel) VST3, Audio Unit & Standalone** voice regeneration plugin (plus a companion browser studio in `web/`) built around 6 major DSP engineering upgrades over v2.0.0:

- **2x Zero-Padded Linear Convolution (2048-pt FFT) & F0-Adaptive True-Envelope (`VocalTractWarper`):** Eliminates circular wrap-around aliasing and bass formant smearing via 3-iteration Imai/Röbel peak-hugging cepstral extraction.
- **Single-Derivative DC-Balanced LF Glottal Resynthesis & Coherent PSOLA Gain Normalization (`HybridRegenerator`):** Fixes v2.0 double-differentiation buzz and balances grain energy (`√(T_out / T_in)`) across ±18 st pitch shifts.
- **Studio Input Pre-Conditioning (`StudioDynamics` + `PitchTracker`):** 2nd-order Butterworth HPF (40–160 Hz), smooth noise gate (-72..-24 dB), dynamic De-Esser (5.2–10.5 kHz), and octave-continuous YIN pitch tracking.
- **Multi-Cue Consonant Guard:** Combines zero-crossing rate, HF/LF energy ratio, and voicing confidence to pass sibilants and plosives with phase-coherent clarity.
- **Studio Auto-Tune 3.0 (`AutoTune`):** Fast melisma onset lock, independent **Retune Speed (5–120 ms)**, **Vibrato Preserve (0–100%)**, and **Scale/Key Lock** (Auto, Major, Minor, Chromatic, Hijaz, Pentatonic) alongside Krumhansl-Schmuckler auto key detection.
- **3 ms Lookahead −0.3 dBFS True-Peak Limiter & ITU-R BS.1770 K-Weighted Gated Loudness Auto-Match:** Strict peak protection with live GR readout and zero breath-pause pumping.
- **Install:** Download `AlptugDUB-3.0.0-macOS.zip` from [Releases](https://github.com/AlptugDag/AlptugDUB/releases/latest), run `sh install.sh`, and rescan plug-ins in your DAW.

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

Kod GNU AGPL 3.0 ile yayımlanır (`LICENSE`). Üçüncü taraf bileşenler ve görseller için `CREDITS.md`'ye bakın; fotoğraf, imza ve "AlptugDUB" adı Alptuğ Dağ'a aittir.
