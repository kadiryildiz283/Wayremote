#include <iostream>
#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// --- Veri Yapıları ---
struct ClientInfo {
    int socket_fd;
    std::string ip_address;
    std::string status = "Idle"; // Idle, Connecting, Connected
    std::string pending_peer_id = ""; // Bağlantı isteği bekleyen/gönderen ID
};

std::unordered_map<std::string, ClientInfo> clients_by_id; // ID -> ClientInfo
std::unordered_map<int, std::string> id_by_socket;     // socket_fd -> ID
std::mutex clients_mutex;                              // Veri yapılarını korumak için mutex

// --- Yardımcı Fonksiyonlar ---

// Rastgele benzersiz ID üretir
std::string generate_unique_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(100000, 999999);
    std::string new_id;
    do {
        std::stringstream ss;
        ss << std::setw(6) << std::setfill('0') << dist(gen);
        new_id = ss.str();
    } while (clients_by_id.count(new_id)); // Haritada zaten varsa tekrar üret
    return new_id;
}

// Sokete güvenli mesaj gönderir (sonuna '\n' ekler)
bool send_message(int socket_fd, const std::string& message) {
    std::string full_message = message + "\n";
    return send(socket_fd, full_message.c_str(), full_message.length(), 0) >= 0;
}

// Bağlı istemcilerin listesini sunucu konsoluna yazdırır
void print_server_clients_list() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    system("clear"); // Terminali temizle
    std::cout << "--- Sunucu: Bağlı İstemciler ---" << std::endl;
    if (clients_by_id.empty()) {
        std::cout << "(Şu an bağlı istemci yok)" << std::endl;
    } else {
        for (const auto& pair : clients_by_id) {
            std::cout << "  - ID: " << pair.first
                      << ", IP: " << pair.second.ip_address
                      << ", Soket: " << pair.second.socket_fd
                      << ", Durum: " << pair.second.status;
            if (!pair.second.pending_peer_id.empty()) {
                 std::cout << " (Peer: " << pair.second.pending_peer_id << ")";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "---------------------------------" << std::endl;
    std::cout << "Yeni bağlantılar bekleniyor..." << std::endl;
}


// --- İstemci İşleme Mantığı ---
void handle_client(int client_socket, std::string client_id) {
    char buffer[2048];
    std::string partial_message; // Tamamlanmamış mesajları biriktir

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);

        if (bytes_read <= 0) {
            // Hata veya bağlantı kapandı
            break; // Döngüden çık, temizlik aşağıda yapılacak
        }

        partial_message += buffer;

        // Gelen verideki tam mesajları işle ('\n' ile bitenler)
        size_t newline_pos;
        while ((newline_pos = partial_message.find('\n')) != std::string::npos) {
            std::string command_line = partial_message.substr(0, newline_pos);
            partial_message.erase(0, newline_pos + 1);

            // Komutları işle
            std::cout << "Sunucu: ID " << client_id << "'den komut alındı: " << command_line << std::endl; // Debug
            std::stringstream ss(command_line);
            std::string command;
            ss >> command;

            std::lock_guard<std::mutex> lock(clients_mutex); // Komut işlerken kilitle

            // --- LIST Komutu ---
            if (command == "LIST") {
                std::string list_response = "LIST_BEGIN";
                for (const auto& pair : clients_by_id) {
                     // Kendisini listeye ekleme
                    if (pair.first == client_id) continue;
                    list_response += "\nID: " + pair.first + " Durum: " + pair.second.status;
                }
                list_response += "\nLIST_END";
                send_message(client_socket, list_response);
            }
            // --- CONNECT Komutu ---
            else if (command == "CONNECT") {
                std::string target_id;
                ss >> target_id;

                if (clients_by_id.count(client_id) && clients_by_id.count(target_id) && client_id != target_id) {
                    auto& source_info = clients_by_id[client_id];
                    auto& target_info = clients_by_id[target_id];

                    if (source_info.status == "Idle" && target_info.status == "Idle") {
                        source_info.status = "Connecting";
                        source_info.pending_peer_id = target_id;
                        target_info.status = "Connecting";
                        target_info.pending_peer_id = client_id;

                        send_message(target_info.socket_fd, "INCOMING " + client_id);
                        send_message(source_info.socket_fd, "CONNECTING " + target_id);
                        std::cout << "Sunucu: " << client_id << " -> " << target_id << " bağlantı isteği gönderildi." << std::endl;
                    } else {
                        send_message(client_socket, "ERROR Hedef veya siz uygun durumda değilsiniz.");
                    }
                } else {
                     send_message(client_socket, "ERROR Geçersiz veya bulunamayan ID.");
                }
                 print_server_clients_list(); // Liste durumunu güncelle
            }
            // --- ACCEPT Komutu ---
             else if (command == "ACCEPT") {
                std::string requester_id;
                ss >> requester_id;

                 if (clients_by_id.count(client_id) && clients_by_id.count(requester_id)) {
                    auto& accepter_info = clients_by_id[client_id];
                    auto& requester_info = clients_by_id[requester_id];

                    // Doğru durumda olup olmadıklarını kontrol et
                    if (accepter_info.status == "Connecting" && accepter_info.pending_peer_id == requester_id &&
                        requester_info.status == "Connecting" && requester_info.pending_peer_id == client_id)
                    {
                         accepter_info.status = "Connected";
                         requester_info.status = "Connected";
                         // pending_peer_id'yi temizlemeye gerek yok, kiminle bağlı olduğunu gösterir

                         send_message(requester_info.socket_fd, "ACCEPTED " + client_id);
                         send_message(accepter_info.socket_fd, "CONNECTION_ESTABLISHED " + requester_id);
                         std::cout << "Sunucu: " << client_id << " <-> " << requester_id << " bağlantısı kuruldu." << std::endl;
                    } else {
                         send_message(client_socket, "ERROR Geçersiz kabul komutu veya durum.");
                    }
                 } else {
                     send_message(client_socket, "ERROR Geçersiz ID.");
                 }
                 print_server_clients_list(); // Liste durumunu güncelle
            }
            // --- REJECT Komutu ---
            else if (command == "REJECT") {
                std::string requester_id;
                ss >> requester_id;

                if (clients_by_id.count(client_id) && clients_by_id.count(requester_id)) {
                    auto& rejecter_info = clients_by_id[client_id];
                    auto& requester_info = clients_by_id[requester_id];

                    if (rejecter_info.status == "Connecting" && rejecter_info.pending_peer_id == requester_id &&
                        requester_info.status == "Connecting" && requester_info.pending_peer_id == client_id)
                    {
                        rejecter_info.status = "Idle";
                        requester_info.status = "Idle";
                        rejecter_info.pending_peer_id = "";
                        requester_info.pending_peer_id = "";

                        send_message(requester_info.socket_fd, "REJECTED " + client_id);
                        send_message(rejecter_info.socket_fd, "REJECTION_SENT " + requester_id);
                         std::cout << "Sunucu: " << client_id << " -> " << requester_id << " bağlantı isteğini reddetti." << std::endl;
                    } else {
                        send_message(client_socket, "ERROR Geçersiz reddetme komutu veya durum.");
                    }
                } else {
                    send_message(client_socket, "ERROR Geçersiz ID.");
                }
                 print_server_clients_list(); // Liste durumunu güncelle
            }
             // --- MSG Komutu (Şimdilik basit metin aktarımı) ---
             else if (command == "MSG") {
                 if (clients_by_id.count(client_id)) {
                     auto& sender_info = clients_by_id[client_id];
                     if (sender_info.status == "Connected" && !sender_info.pending_peer_id.empty()) {
                         std::string peer_id = sender_info.pending_peer_id;
                         if(clients_by_id.count(peer_id)) {
                             auto& receiver_info = clients_by_id[peer_id];
                             // Komut satırının geri kalanını mesaj olarak al
                             size_t first_space = command_line.find(' ');
                             std::string message_content = (first_space != std::string::npos) ? command_line.substr(first_space + 1) : "";
                             send_message(receiver_info.socket_fd, "MSG_FROM " + client_id + " " + message_content);
                             //std::cout << "Sunucu: " << client_id << " -> " << peer_id << " mesaj iletildi." << std::endl; // Çok kalabalık yapabilir
                         }
                     } else {
                          send_message(client_socket, "ERROR Mesaj göndermek için bağlı olmalısınız.");
                     }
                 }
             }
             // --- DISCONNECT Komutu (Bağlantıyı sonlandırmak için) ---
              else if (command == "DISCONNECT") {
                   if (clients_by_id.count(client_id)) {
                       auto& self_info = clients_by_id[client_id];
                       if (self_info.status == "Connected" && !self_info.pending_peer_id.empty()) {
                           std::string peer_id = self_info.pending_peer_id;
                            if(clients_by_id.count(peer_id)) {
                                auto& peer_info = clients_by_id[peer_id];
                                send_message(peer_info.socket_fd, "PEER_DISCONNECTED " + client_id);
                                peer_info.status = "Idle";
                                peer_info.pending_peer_id = "";
                                std::cout << "Sunucu: " << peer_id << " bağlantısı " << client_id << " tarafından kesildi." << std::endl;
                            }
                           self_info.status = "Idle";
                           self_info.pending_peer_id = "";
                           send_message(client_socket, "DISCONNECTED_OK");
                           std::cout << "Sunucu: " << client_id << " mevcut bağlantısını kesti." << std::endl;
                       } else {
                           send_message(client_socket, "ERROR Kesilecek aktif bağlantı yok.");
                       }
                   }
                    print_server_clients_list(); // Liste durumunu güncelle
              }
            // Bilinmeyen komutları yoksayabilir veya hata gönderebiliriz
            // else { send_message(client_socket, "ERROR Bilinmeyen komut."); }

        } // while (newline_pos)
    } // while (true) - read loop

    // --- Bağlantı Koptuğunda Temizlik ---
    std::cout << "\nSunucu: İstemci bağlantısı koptu veya hata (ID: " << client_id << ", Soket: " << client_socket << ")" << std::endl;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients_by_id.count(client_id)) {
             auto& client_info = clients_by_id[client_id];
             // Eğer başka bir istemciyle bağlıysa, diğer istemciye haber ver
             if (client_info.status == "Connected" && !client_info.pending_peer_id.empty()) {
                  std::string peer_id = client_info.pending_peer_id;
                  if(clients_by_id.count(peer_id)) {
                       auto& peer_info = clients_by_id[peer_id];
                       send_message(peer_info.socket_fd, "PEER_DISCONNECTED " + client_id);
                       peer_info.status = "Idle";
                       peer_info.pending_peer_id = "";
                       std::cout << "Sunucu: Bağlantıdaki diğer istemci (" << peer_id << ") bilgilendirildi." << std::endl;
                  }
             }
            clients_by_id.erase(client_id); // Ana haritadan sil
        }
        id_by_socket.erase(client_socket); // Ters lookup haritasından sil
    }
    print_server_clients_list(); // Liste durumunu güncelle
    close(client_socket);        // Soketi kapat
}


// --- Ana Sunucu Döngüsü ---
int main() {
    const int PORT = 12345;
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Soket oluşturulamadı"); exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt hatası"); close(server_fd); exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Tüm IP'lerden dinle
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bağlama (bind) hatası"); close(server_fd); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) { // Kuyruk boyutu artırıldı
        perror("Dinleme (listen) hatası"); close(server_fd); exit(EXIT_FAILURE);
    }

    std::cout << "Sunucu başlatıldı. Port " << PORT << " dinleniyor..." << std::endl;
    print_server_clients_list();

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_addrlen = sizeof(client_address);
        int new_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);

        if (new_socket < 0) {
            perror("Bağlantı kabul (accept) hatası");
            continue;
        }

        std::string client_ip = inet_ntoa(client_address.sin_addr);
        std::string new_id;

        // Yeni istemci için bilgileri kaydet (Mutex içinde)
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            new_id = generate_unique_id();
            ClientInfo client_info = {new_socket, client_ip, "Idle", ""};
            clients_by_id[new_id] = client_info;
            id_by_socket[new_socket] = new_id;
        }

         std::cout << "\nYeni bağlantı kabul edildi. IP: " << client_ip << ", Atanan ID: " << new_id << std::endl;


        // İstemciye ID'sini gönder
        if (!send_message(new_socket, "ID " + new_id)) {
             std::cerr << "Hata: İstemciye ID gönderilemedi (ID: " << new_id << ")" << std::endl;
             // Hata durumunda temizlik yap
             {
                  std::lock_guard<std::mutex> lock(clients_mutex);
                  clients_by_id.erase(new_id);
                  id_by_socket.erase(new_socket);
             }
             close(new_socket);
             print_server_clients_list(); // Liste durumunu güncelle
             continue; // Bir sonraki bağlantıyı bekle
        }


        print_server_clients_list(); // Bağlantı sonrası listeyi güncelle

        // İstemciyi ayrı bir thread'de işle
        std::thread client_thread(handle_client, new_socket, new_id);
        client_thread.detach();
    }

    close(server_fd);
    return 0;
}
