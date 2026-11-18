# AlptugDUB Studio 3.0

**Seslendirme ve dublaj için ses mühendisliği ilkeleriyle yeniden yapılandırılmış hibrit Web Audio DSP konsolu.**

## v3.0 Mühendislik Yeniden Yapılandırması

AlptugDUB 2.0 kod tabanında tespit edilen 6 temel DSP kusuru giderilerek gerçek zamanlı 48 kHz Web Audio / AudioWorklet stüdyo motoruna taşındı:

1. **Sıfır Dolgulu (2048-pt) Doğrusal Konvolüsyon & F0-Uyarlamalı Gerçek Zarf (True-Envelope):** Dairesel konvolüsyon örtüşmesi ve düşük perdede formant erimesi giderildi.
2. **Tek Türevli DC-Dengeli LF Glottal Tablosu & Uyumlu PSOLA Enerji Normalizasyonu:** Çift türev kaynaklı tiz cızırtı ve yukarı/aşağı perde kaymalarındaki seviye dengesizliği düzeltildi.
3. **Giriş Ön İşleme Zinciri:** 2. derece Butterworth HPF (40–160 Hz), yumuşak gürültü kapısı ve dinamik De-Esser ile perde izleyicinin alt frekans uğultularına kilitlenmesi engellendi.
4. **Ünsüz Koruma (Consonant Guard):** HF/LF enerji oranı + sıfır-geçiş hızı (ZCR) ile `s, ş, f, ç, t, k, p` ünsüzleri perde kaydırma hattından pürüzsüz çapraz geçişle ayrıldı.
5. **Uyarlanabilir Stüdyo Auto-Tune:** 90 ms melisma gecikmesi giderildi; bağımsız Retune Hızı (ms), Vibrato Koruma ve Manuel Gam Kilidi (Majör / Minör / Kromatik / Hicaz / Pentatonik) eklendi.
6. **3 ms Lookahead −0,3 dBFS True-Peak Limiter & K-Ağırlıklı Seviye Eşleme:** Ani 0 ms kırpma distorsiyonu ve nefes aralarındaki seviye pompalaması kaldırıldı.

## Özellikler

- **Gerçek Zamanlı Stüdyo Konsolu:** 13 karakter bankası, senkronize Perde/Gırtlak kilidi, canlı osiloskop, A/B karşılaştırma, Bypass ve Giriş/Çıkış/Limiter GR ölçümü.
- **Canlı Mikrofon, Dosya Yükleme, Yerleşik Test Sesleri ve WAV Dışa Aktarım.**
- **Spektrum & Formant Analizörü:** Kuru/işlenmiş FFT eğrisi, F1/F2/F3 anatomik bükme vektörleri ve canlı perde izi.
- **DSP Mühendislik Test Tezgâhı:** 48 kHz deterministik sinyal doğrulama testleri.

## Çalıştırma

```bash
npm install
npm run dev
npm test
```

## Lisans

GNU AGPL 3.0 (`LICENSE`). Marka, fotoğraf ve imza Alptuğ Dağ'a aittir.
