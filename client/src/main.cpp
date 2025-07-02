#include "../includes/client_utils.h" // Kendi başlık dosyanızın yolu doğru varsayıldı
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <csignal>
#include <stdexcept> // std::invalid_argument, std::out_of_range için
#include <chrono>    // std::chrono::milliseconds

// Global Değişkenler
int sock = 0;
std::string my_id = "UNKNOWN";
std::atomic<bool> running(true);
std::mutex cout_mutex;
std::atomic<bool> client_a_waiting_for_tunnel_activation(false);
std::atomic<bool> client_a_vnc_data_mode_active(false); //// Sinyal işleyici

void signal_handler(int signum) {
   {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << "\n[Bilgi] Çıkış yapılıyor (Sinyal " << signum << " alındı)..." << std::endl;
   }
   running = false;
   if (sock > 0) {
       // Shutdown soketi kapatmadan önce diğer ucu bilgilendirmek için önemlidir
       // ve okuma/yazma işlemlerinin sonlanmasına yardımcı olur.
       shutdown(sock, SHUT_RDWR);
       close(sock);
       sock = -1; // Soketin kapalı olduğunu belirtmek için
   }
}

// Sunucudan mesajları dinleyen thread fonksiyonu
void receive_messages_thread_func() {
    char buffer[4096];
    std::string partial_message;
    std::string current_line;

    // DÖNGÜ KOŞULU GÜNCELLENDİ
    while (running && !client_a_vnc_data_mode_active) {
        if (sock <= 0) { running = false; break; }

        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = ::read(sock, buffer, sizeof(buffer) - 1);

        if (bytes_read <= 0) {
            // ... (hata ve bağlantı kopma logları) ...
            running = false;
            break;
        }

        partial_message += buffer;
        size_t newline_pos;
        
        // İkinci while koşulu da güncellendi
        while (running && !client_a_vnc_data_mode_active && (newline_pos = partial_message.find('\n')) != std::string::npos) {
            current_line = partial_message.substr(0, newline_pos);
            partial_message.erase(0, newline_pos + 1);
            if (!current_line.empty()) {
                process_server_message(current_line, my_id, cout_mutex, sock);
            }
        }
    } // Ana while döngüsü sonu

    { // Thread sonlanma logu
        std::lock_guard<std::mutex> lock(cout_mutex);
        if (client_a_vnc_data_mode_active) {
            std::cout << "\n[Bilgi] Ana mesaj alıcı thread VNC veri modu nedeniyle durduruldu. Soket libVNCclient'e devredildi." << std::endl;
        } else {
            std::cout << "\n[Bilgi] Sunucu dinleme thread'i sonlandırıldı." << std::endl;
        }
    }
}


int main(int argc, char *argv[]) {
    // Argüman sayısını kontrol et
    if (argc != 3) {
        // std::cerr standart hata akışına yazar, genellikle hatalar için tercih edilir
        std::cerr << "Hata: Yanlış argüman sayısı." << std::endl;
        std::cerr << "Kullanım: " << argv[0] << " <sunucu_ip> <sunucu_port>" << std::endl;
        return 1; // Hata kodu ile çık
    }

    const char* SERVER_IP = argv[1];
    int SERVER_PORT = 0;

    // Port numarasını güvenli bir şekilde çevir
    try {
        SERVER_PORT = std::stoi(argv[2]); // std::string'i int'e çevirir
        if (SERVER_PORT <= 0 || SERVER_PORT > 65535) {
             std::cerr << "Hata: Geçersiz port numarası (1-65535 arası olmalı): " << argv[2] << std::endl;
             return 1;
        }
    } catch (const std::invalid_argument& e) {
        // Eğer çevirme başarısız olursa (sayı değilse)
        std::cerr << "Hata: Port numarası sayısal olmalı: '" << argv[2] << "'" << std::endl;
        return 1;
    } catch (const std::out_of_range& e) {
        // Eğer sayı geçerli aralığın dışındaysa
        std::cerr << "Hata: Port numarası çok büyük veya çok küçük: " << argv[2] << std::endl;
        return 1;
    }

    struct sockaddr_in serv_addr;

    // Sinyalleri ayarla (programın başında yapmak iyi bir pratik)
    signal(SIGINT, signal_handler);  // Ctrl+C
    signal(SIGTERM, signal_handler); // Sistemden gelen kapatma sinyali

    // Soket oluştur
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[HATA] Soket oluşturma başarısız");
        return 1; // Hata kodu ile çık
    }

    // Sunucu adresi yapısını ayarla
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT); // Portu network byte order'a çevir

    // IP adresini network formatına çevir ve ayarla
    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror(("[HATA] Geçersiz adres/Desteklenmeyen format: " + std::string(SERVER_IP)).c_str());
        close(sock); // Oluşturulan soketi kapat
        return 1;
    }

    // Sunucuya bağlanmayı dene
    std::cout << "[Bilgi] Sunucuya (" << SERVER_IP << ":" << SERVER_PORT << ") bağlanılıyor..." << std::endl;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror(("[HATA] Bağlantı Başarısız (" + std::string(SERVER_IP) + ":" + std::to_string(SERVER_PORT) + ")").c_str());
        close(sock);
        return 1;
    }

    // Bağlantı başarılıysa devam et
    std::cout << "[Bilgi] Sunucuya bağlanıldı. ID bekleniyor..." << std::endl;

    // Mesaj alma thread'ini başlat
    std::thread receiver_thread(receive_messages_thread_func);

    // Kullanıcıdan komut alma döngüsü
    std::string line;
    while (running) {
        {
             std::lock_guard<std::mutex> lock(cout_mutex);
             std::cout << "> ";
             std::cout.flush(); // Prompt'un hemen görünmesini sağla
        }

        // std::getline başarısız olursa (örn. Ctrl+D veya pipe kapandıysa)
        if (!std::getline(std::cin, line)) {
            if (running) {
                std::cout << "\n[Bilgi] Giriş sonlandı (EOF). Çıkılıyor..." << std::endl;
                // Sinyal göndererek temiz çıkış yapmayı dene
                // kill(getpid(), SIGTERM); // Kendine sinyal gönder (alternatif)
                signal_handler(SIGTERM); // Doğrudan çağırarak temizliği başlat
            }
            break; // Döngüden çık
        }

        // Eğer sinyal handler running'i false yaptıysa döngüden çık
        if (!running) break;

        // Boş satırları gönderme
        if (!line.empty()) {
             // Komutu sunucuya gönder
             if (!send_server_message(sock, line)) {
                 if (running) {
                     std::lock_guard<std::mutex> lock(cout_mutex);
                     std::cerr << "\n[Hata] Sunucuya mesaj gönderilemedi! (Bağlantı kopmuş olabilir)" << std::endl;
                 }
                 running = false; // Gönderim başarısızsa çık
                 break;
             }
        }
         // CPU kullanımını azaltmak için kısa bir bekleme
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Temizlik Aşaması
    std::cout << "[Bilgi] Ana döngü bitti, temizlik yapılıyor..." << std::endl;
    running = false; // Diğer thread'in de durduğundan emin ol

    // Soketi kapat (signal_handler kapatmadıysa)
    if (sock > 0) {
        std::cout << "[Bilgi] Soket kapatılıyor..." << std::endl;
        // shutdown(sock, SHUT_RDWR); // Signal handler zaten yapıyor olmalı
        close(sock);
        sock = -1;
    }

    // Alıcı thread'in tamamen bitmesini bekle
    if (receiver_thread.joinable()) {
        std::cout << "[Bilgi] Alıcı thread'in bitmesi bekleniyor..." << std::endl;
        receiver_thread.join();
        std::cout << "[Bilgi] Alıcı thread bitti." << std::endl;
    }

    std::cout << "[Bilgi] İstemci programı sonlandı." << std::endl;
    return 0; // Başarılı çıkış kodu
}
