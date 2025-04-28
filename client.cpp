#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic> // Programın çalışıp çalışmadığını kontrol etmek için
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <csignal>
#include <sstream>
int sock = 0;
std::string my_id = "UNKNOWN";
std::atomic<bool> running(true); // Programın çalışıp çalışmadığını kontrol etmek için atomik bool
std::mutex cout_mutex; // Konsola yazarken çakışmayı önlemek için

// Sokete güvenli mesaj gönderir (sonuna '\n' ekler)
bool send_server_message(const std::string& message) {
    if (sock <= 0) return false;
    std::string full_message = message + "\n";
    return send(sock, full_message.c_str(), full_message.length(), 0) >= 0;
}
// Sunucudan gelen mesajları dinleyen ve işleyen fonksiyon (thread üzerinde çalışacak)
void receive_messages() {
    char buffer[2048];
    std::string partial_message;

    while (running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);

        if (bytes_read <= 0) {
            if (running) {
                 std::lock_guard<std::mutex> lock(cout_mutex);
                 std::cerr << "\n[Hata] Sunucu bağlantısı koptu!" << std::endl;
                 std::cout << "Çıkmak için Enter'a basın..." << std::endl;
            }
            running = false;
            // close(sock); // shutdown signal_handler'da çağrılıyor genelde
            // sock = -1;
            break;
        }

        partial_message += buffer;
        size_t newline_pos;

        while ((newline_pos = partial_message.find('\n')) != std::string::npos) {
             std::string server_msg_line = partial_message.substr(0, newline_pos);
             partial_message.erase(0, newline_pos + 1);

             std::stringstream ss(server_msg_line);
             std::string msg_type;
             ss >> msg_type;

             // --- Gelen Mesajları İşleme ve Kullanıcıyı Bilgilendirme ---
             { // Mutex kilidini bu blok boyunca aktif tutalım
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "\n[Sunucu] " << server_msg_line << std::endl; // Ham mesajı yine de gösterelim (debug için)

                if (msg_type == "ID") {
                    ss >> my_id;
                    std::cout << "[Bilgi] Size atanan ID: " << my_id << std::endl;
                } else if (msg_type == "INCOMING") { // <<< DEĞİŞİKLİK BURADA BAŞLIYOR
                    std::string source_id;
                    ss >> source_id;
                    std::cout << "----------------------------------------" << std::endl;
                    std::cout << "[Bağlantı İsteği] ID '" << source_id << "' sizinle bağlanmak istiyor." << std::endl;
                    std::cout << "Kabul etmek için: accept " << source_id << std::endl;
                    std::cout << "Reddetmek için:   reject " << source_id << std::endl;
                    std::cout << "----------------------------------------" << std::endl;
                } else if (msg_type == "CONNECTING") {
                     std::string target_id;
                     ss >> target_id;
                     std::cout << "[Bilgi] ID '" << target_id << "' hedefine bağlantı isteği gönderildi, yanıt bekleniyor..." << std::endl;
                } else if (msg_type == "ACCEPTED") {
                    std::string peer_id;
                    ss >> peer_id;
                    std::cout << "[Bilgi] Bağlantı isteğiniz ID '" << peer_id << "' tarafından KABUL EDİLDİ." << std::endl;
                } else if (msg_type == "REJECTED") {
                    std::string peer_id;
                    ss >> peer_id;
                    std::cout << "[Bilgi] Bağlantı isteğiniz ID '" << peer_id << "' tarafından REDDEDİLDİ." << std::endl;
                } else if (msg_type == "CONNECTION_ESTABLISHED") {
                    std::string peer_id;
                    ss >> peer_id;
                    std::cout << "[Bilgi] ID '" << peer_id << "' ile BAĞLANTI KURULDU." << std::endl;
                    std::cout << "Artık 'msg <mesaj>' ile mesaj gönderebilirsiniz." << std::endl;
                    std::cout << "Bağlantıyı bitirmek için 'disconnect' komutunu kullanın." << std::endl;
                } else if (msg_type == "PEER_DISCONNECTED") {
                    std::string peer_id;
                    ss >> peer_id;
                    std::cout << "[Bilgi] ID '" << peer_id << "' bağlantısı KESİLDİ." << std::endl;
                } else if (msg_type == "DISCONNECTED_OK") {
                     std::cout << "[Bilgi] Mevcut bağlantınız başarıyla sonlandırıldı." << std::endl;
                } else if (msg_type == "MSG_FROM") {
                    std::string source_id;
                    ss >> source_id;
                    // Mesajın geri kalanını al
                    std::string message_content;
                    size_t first_space = server_msg_line.find(' ');
                    size_t second_space = (first_space != std::string::npos) ? server_msg_line.find(' ', first_space + 1) : std::string::npos;
                    if (second_space != std::string::npos) {
                        message_content = server_msg_line.substr(second_space + 1);
                    }
                    std::cout << "[Mesaj (" << source_id << ")] " << message_content << std::endl;
                } else if (msg_type == "ERROR") {
                     std::string error_message;
                     size_t first_space = server_msg_line.find(' ');
                     if (first_space != std::string::npos) {
                          error_message = server_msg_line.substr(first_space + 1);
                     }
                     std::cerr << "[Sunucu Hatası] " << error_message << std::endl;
                } else if (msg_type == "LIST_BEGIN") {
                     std::cout << "--- Bağlı İstemciler Listesi ---" << std::endl;
                } else if (msg_type == "LIST_END") {
                     std::cout << "--------------------------------" << std::endl;
                } else if (server_msg_line.rfind("ID: ", 0) == 0) { // LIST komutunun içeriği için basit kontrol
                     std::cout << "  " << server_msg_line << std::endl; // Liste elemanını yazdır
                }
                // Diğer sunucu mesajlarını burada işleyebilirsiniz...

                std::cout << "> "; // Yeni komut istemi
                std::cout.flush(); // Konsolu hemen güncelle
            } // Mutex kilidi bloğu sonu
        } // while (newline_pos)
    } // while (running)
     // Thread bittiğinde belirtelim (isteğe bağlı)
     {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\n[Bilgi] Sunucu dinleme thread'i sonlandı." << std::endl;
     }
}

// ... (main fonksiyonu ve signal_handler aynı kalabilir) ...// Sinyal işleyici
void signal_handler(int signum) {
   {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << "\n[Bilgi] Çıkış yapılıyor (Sinyal " << signum << " alındı)..." << std::endl;
   }
   running = false; // Döngüleri durdur

   if (sock > 0) {
       // Önce shutdown çağırarak diğer uçtaki ve bekleyen read() çağrılarını bilgilendir.
       // SHUT_RDWR: Hem okuma hem yazma yönünü kapatır.
       shutdown(sock, SHUT_RDWR);

       // Sonra soketi kapat.
       close(sock);
       sock = -1;
   }
   // exit(signum); // Doğrudan exit çağırmak yerine thread'in bitmesini beklemek daha temiz.
}int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Kullanım: " << argv[0] << " <sunucu_ip> <sunucu_port>" << std::endl;
        return 1;
    }
    const char* SERVER_IP = argv[1];
    int SERVER_PORT = std::stoi(argv[2]); // String'i integer'a çevir
    struct sockaddr_in serv_addr;

    signal(SIGINT, signal_handler);  // Ctrl+C
    signal(SIGTERM, signal_handler); // Terminate

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Soket oluşturma hatası"); return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if(inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Geçersiz adres"); close(sock); return -1;
    }

    std::cout << "[Bilgi] Sunucuya (" << SERVER_IP << ":" << SERVER_PORT << ") bağlanılıyor..." << std::endl;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bağlantı Başarısız"); close(sock); return -1;
    }

    std::cout << "[Bilgi] Sunucuya bağlanıldı. ID bekleniyor..." << std::endl;

    // Sunucudan mesajları dinlemek için ayrı bir thread başlat
    std::thread receiver_thread(receive_messages);

    // Ana thread kullanıcıdan komutları okur
    std::string line;
    while (running) {
        { // Yeni komut istemini yazdır
             std::lock_guard<std::mutex> lock(cout_mutex);
             std::cout << "> ";
             std::cout.flush();
        }

        if (!std::getline(std::cin, line)) {
            // Eğer std::cin okuması başarısız olursa (örn. pipe kapandıysa) veya EOF ise
            if (running) { // Sadece program hala çalışıyorsa çıkış yap
                 signal_handler(SIGTERM); // Temiz çıkış yapmayı dene
            }
            break; // Döngüden çık
        }

         if (!running) break; // Eğer receiver thread running'i false yaptıysa çık


        if (!line.empty()) {
             // Kullanıcıdan komut alındı, sunucuya gönder
             if (!send_server_message(line)) {
                 if (running) { // Sadece hala çalışıyorsak hata ver
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cerr << "\n[Hata] Sunucuya mesaj gönderilemedi!" << std::endl;
                 }
                  running = false; // Gönderim başarısızsa muhtemelen bağlantı koptu
                  break;
             }
        }
         // Küçük bir bekleme, CPU'yu %100 kullanmamak için (isteğe bağlı)
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Program sonlanıyor
    running = false; // Diğer thread'in de durmasını sağla
     if (sock > 0) {
        close(sock);
        sock = -1;
     }

    // Receiver thread'in bitmesini bekle
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

     std::cout << "[Bilgi] İstemci programı sonlandı." << std::endl;

    return 0;
}
