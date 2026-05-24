# tarsau - Arşivleme Programı (Sıkıştırmasız)

Bu proje, Sakarya Üniversitesi Bilgisayar Mühendisliği Bölümü **Sistem Programlama** dersi 2025-2026 Bahar Dönemi projesi kapsamında geliştirilmiştir. Linux/Unix ortamlarında C dili kullanılarak, POSIX standartlarına uygun, sıkıştırma yapmadan çalışan bir arşivleme (`.sau`) aracı simülasyonudur.

## 🚀 Özellikler
* **ASCII Doğrulaması:** Giriş dosyalarının tamamı saf ASCII metin formatında (bayt başına 1 karakter) olmak zorundadır. Binary dosyalar otomatik olarak reddedilir.
* **Metadata Koruma:** Arşivlenen dosyaların isim, Unix dosya izinleri (rwx) ve boyut bilgileri `.sau` dosyasının ilk bölümündeki organizasyon alanında saklanır.
* **Geri Yükleme:** Arşiv açıldığında, dosyalar orijinal erişim yetkileri (chmod modları) birebir korunarak hedef dizine çıkartılır.
* **Kısıt Denetimleri:** Maksimum 32 dosya ve toplamda 200 MB boyut sınırları katı bir şekilde kontrol edilir.

## 🛠️ Derleme ve Çalıştırma

Proje `Makefile` otomasyon aracı ile derlenmektedir.

### Derleme:
```bash
make clean && make
