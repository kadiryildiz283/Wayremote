# Wayremote - C++ ile Uzak Masaüstü Sistemi (Geliştirme Aşamasında)

Bu proje, AnyDesk veya TeamViewer benzeri bir deneyim sunmayı hedefleyen, merkezi bir sunucu üzerinden ID tabanlı bağlantı kurarak VNC protokolü ile uzak masaüstü erişimi sağlamayı amaçlayan bir C++ uygulamasıdır.

Proje şu anda aktif geliştirme aşamasındadır.

## 🎯 Amaç

* Kullanıcıların karmaşık ağ ayarları (port yönlendirme, IP adresi ezberleme) ile uğraşmadan, basit bir ID kullanarak birbirlerinin bilgisayarlarına bağlanabilmesi.
* Farklı ağlardaki bilgisayarlar arasında bağlantıyı merkezi bir sunucu (relay server) aracılığıyla kurmak.
* Platforma özel (Linux Wayland/X11, Windows) uygun VNC sunucusunu otomatik olarak tetiklemek.
* VNC trafiğini merkezi sunucu üzerinden güvenli bir şekilde tünellemek (Bu kısım henüz tamamlanmadı).

## ✨ Özellikler (Mevcut ve Planlanan)

**Mevcut:**

* Merkezi sunucu üzerinden istemci bağlantı yönetimi.
* Her istemciye benzersiz 6 haneli ID atanması.
* ID kullanarak istemciler arası bağlantı isteği (`connect`).
* Gelen bağlantı isteklerini kabul veya reddetme (`accept` / `reject`).
* Bağlı istemciler arasında metin tabanlı mesajlaşma (`msg`).
* Platform Algılama (Linux/Wayland/X11, Windows).
* Bağlantı kabul edildiğinde hedef makinede uygun VNC sunucusunu/servisini başlatma denemesi:
    * Linux (Wayland): `wayvnc` (fork/exec ile)
    * Linux (X11): `x11vnc` (fork/exec ile)
    * Windows: Önceden kurulmuş VNC servisini başlatma (`sc start` ile)
* Gerekli VNC sunucusu bulunamazsa kullanıcıyı bilgilendirme (Linux).

**Planlanan:**

* **VNC Trafiği Tünelleme:** Projenin bir sonraki ana adımı.
* Güvenlik İyileştirmeleri (İletişim şifreleme - TLS?).
* Kullanıcı Arayüzü (GUI)? (Şu an sadece CLI).
* Daha Gelişmiş Platform Desteği ve Algılama (macOS, farklı Linux ortamları).
* Yapılandırılabilir Ayarlar (Port, VNC şifresi vb.).
* Dosya Transferi?

## 🏗️ Mimari

Proje iki ana bileşenden oluşur:

1.  **Relay Sunucusu (`server`):** Herkese açık bir IP adresine sahip bir makinede (örn. VPS) çalışır. İstemcilerin bağlanmasını, ID atamasını, istemci listesini tutmayı ve bağlantı isteklerini yönetmeyi sağlar. Gelecekte VNC trafiğini de aktaracaktır.
2.  **İstemci Ajanı (`client`):** Kullanıcıların kendi bilgisayarlarında çalışır. Sunucuya bağlanır, ID alır, diğer istemcilere bağlanma isteği gönderir/alır ve bağlantı kurulduğunda yerel VNC sunucusunu tetikler. Gelecekte VNC trafiğini tünelleyecektir.

İletişim, istemciler ve sunucu arasında basit, metin tabanlı bir protokol üzerinden yapılır. Uzak masaüstü için temel protokol VNC (RFB)'dir.

## 💻 Teknoloji Stack

* **Dil:** C++17
* **Kütüphaneler:** Standart Kütüphane (STL), POSIX Sockets (Linux/macOS) / Winsock (Windows), POSIX Threads / `std::thread`
* **Protokoller:** Özel Metin Protokolü (İstemci-Sunucu), VNC/RFB (Temel)
* **Derleme:** g++, Make (Makefile)

##  durumu

* **Aşama:** Alfa / Geliştirme / Kavram Kanıtlama (Proof of Concept)
* **Çalışan Kısımlar:** Sunucu bağlantısı, ID alma, bağlantı kurma (connect/accept/reject), VNC sunucusu tetikleme (Linux/Windows servis).
* **Eksik Ana Kısım:** **VNC trafiğinin tünellenmesi henüz eklenmemiştir.** Bu nedenle şu an sadece bağlantı kurulabilir ancak uzak masaüstü görüntülenemez/kontrol edilemez.

## 🚀 Kurulum ve Çalıştırma

**Ön Gereksinimler:**

* C++17 destekli bir g++ derleyicisi.
* `make` aracı.
* `pthread` kütüphanesi (genellikle Linux/macOS'ta standarttır).
* **Kontrol edilecek Linux makinelerinde:** Oturum türüne göre `wayvnc` veya `x11vnc` kurulu olmalıdır.
* **Kontrol edilecek Windows makinelerinde:** Proje için seçilen ve `setup.exe` ile kurulacak olan VNC sunucusunun (örn. TightVNC) servisi kurulu olmalıdır. (Servis adının koddaki `YourVNCServiceName` ile eşleşmesi gerekir!)

**Sunucu (`server`):**

1.  Derleme: `g++ server.cpp -o server -std=c++17 -pthread`
2.  Çalıştırma (Herkese açık IP'li bir makinede):
    * Varsayılan (0.0.0.0:12345): `./server`
    * Belirli IP/Port: `./server <ip_adresi> <port>`

**İstemci (`client`):**

1.  Derleme (Proje ana dizinindeyken): `make` (Eğer sağlanan Makefile kullanılıyorsa)
    * Veya manuel: `g++ src/main.cpp src/client_utils.cpp -o client -Iinclude -std=c++17 -pthread` (Dosya yollarını kendi yapınıza göre ayarlayın)
2.  Çalıştırma: `./client <sunucu_ip_adresi> <sunucu_port>`
    * Örnek: `./client 123.45.67.89 12345`

## ⌨️ Kullanım

1.  Sunucuyu çalıştırın.
2.  Kontrol etmek istediğiniz ve kontrol edeceğiniz makinelerde istemciyi sunucu adresini vererek çalıştırın.
3.  Her istemci bağlandığında sunucudan benzersiz bir ID alacaktır (`[Bilgi] Size atanan ID: ...`).
4.  Bir istemciden diğerine bağlanmak için `connect <hedef_ID>` komutunu kullanın.
5.  Hedef istemci `[Bağlantı İsteği]` mesajını aldığında `accept <kaynak_ID>` veya `reject <kaynak_ID>` komutunu kullanabilir.
6.  Bağlantı kurulursa (`[Bilgi] ... BAĞLANTI KURULDU.`), kabul eden istemci otomatik olarak yerel VNC sunucusunu başlatmayı dener.
7.  Diğer kullanılabilir komutlar:
    * `list`: Sunucuya bağlı diğer istemcileri listeler.
    * `msg <mesaj>`: Bağlı olduğunuz istemciye metin mesajı gönderir (tünelleme öncesi test için).
    * `disconnect`: Mevcut bağlantıyı sonlandırır.
    * `Ctrl+C`: İstemciyi kapatır.

**Not:** Şu anda VNC tünelleme olmadığı için, bağlantı kurulduktan sonra uzak masaüstünü göremezsiniz. Sadece VNC sunucusunun başlatıldığını doğrulayabilirsiniz.

## 🤝 Katkıda Bulunma


## 📜 Lisans

Gnu Public lisansı.
---

Bu taslağı kendi projenizin detaylarına göre düzenleyebilir, eksik kısımları tamamlayabilir veya istemediğiniz bölümleri çıkarabilirsiniz. Özellikle **Lisans** ve **Katkıda Bulunma** kısımlarını kendi tercihinize göre doldurmanız/düzenlemeniz önemlidir. Windows için seçtiğiniz VNC sunucusunun adını da README'de ve kodda belirtmeyi unutmayın!
