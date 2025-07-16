#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <system_error>

#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <csignal>

// --- Veri Yapıları ---

// İstemci bilgilerini ve durumunu tutan yapı
struct ClientInfo {
    int socket_fd;
    std::string ip_address;
    std::string status = "Idle"; // Olası Durumlar: Idle, Connecting, Connected, VncReady, VncTunnelling
    std::string peer_id = "";
    std::vector<char> command_buffer; // Sadece komut modu için kullanılacak tampon
};

// Global Değişkenler ve Mutex
std::unordered_map<std::string, ClientInfo> clients_by_id;
std::unordered_map<int, std::string> id_by_socket;
std::mutex clients_mutex;

// --- Fonksiyon Bildirimleri ---

std::string generate_unique_id_unlocked();
bool send_message(int socket_fd, const std::string& message);
void print_server_clients_list();
void handle_client(int client_socket, std::string client_id);
void cleanup_client(const std::string& client_id, int client_socket);

// --- Fonksiyon Tanımları ---

// Benzersiz ID üreten fonksiyon (çağrıldığı yerde mutex kilitli olmalı)
std::string generate_unique_id_unlocked() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(100000, 999999);
    std::string new_id;
    do {
        new_id = std::to_string(dist(gen));
    } while (clients_by_id.count(new_id));
    return new_id;
}

// Bir istemciye mesaj gönderen fonksiyon
bool send_message(int socket_fd, const std::string& message) {
    if (socket_fd <= 0) return false;
    std::string full_message = message + "\n";
    #ifdef __linux__
        int flags = MSG_NOSIGNAL;
    #else
        int flags = 0;
    #endif
    ssize_t bytes_sent = ::send(socket_fd, full_message.c_str(), full_message.length(), flags);
    return (bytes_sent > 0 && (size_t)bytes_sent == full_message.length());
}

// Sunucudaki istemci listesini yazdıran fonksiyon (mutex kilitli olmalı)
void print_server_clients_list() {
    std::cout << "\n--- Sunucu: Bağlı İstemciler ---" << std::endl;
    if (clients_by_id.empty()) {
        std::cout << "(Şu an bağlı istemci yok)" << std::endl;
    } else {
        for (const auto& pair : clients_by_id) {
            std::cout << "  - ID: " << pair.first << ", IP: " << pair.second.ip_address
                      << ", Soket: " << pair.second.socket_fd << ", Durum: " << pair.second.status;
            if (!pair.second.peer_id.empty()) {
                std::cout << " (Peer: " << pair.second.peer_id << ")";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "---------------------------------" << std::endl;
    std::cout << "Yeni bağlantılar bekleniyor..." << std::endl;
}

// Bir istemci bağlantısı koptuğunda kaynakları temizleyen fonksiyon
void cleanup_client(const std::string& client_id, int client_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    
    // İstemcinin var olup olmadığını kontrol et
    if (!clients_by_id.count(client_id)) {
        return; // Zaten temizlenmiş
    }

    // Eğer bir peere bağlıysa, o peer'i bilgilendir ve Idle durumuna al
    const auto& client_info = clients_by_id.at(client_id);
    if (!client_info.peer_id.empty() && clients_by_id.count(client_info.peer_id)) {
        ClientInfo& peer_info = clients_by_id.at(client_info.peer_id);
        peer_info.status = "Idle";
        peer_info.peer_id = "";
        peer_info.command_buffer.clear();
        send_message(peer_info.socket_fd, "PEER_DISCONNECTED " + client_id);
        std::cout << "Sunucu: Bağlı olan diğer istemci (" << client_info.peer_id
                  << ") bilgilendirildi ve Idle yapıldı." << std::endl;
    }

    // Ana haritalardan istemciyi sil
    clients_by_id.erase(client_id);
    id_by_socket.erase(client_socket);

    std::cout << "\nSunucu: İstemci bağlantısı sonlandı (ID: " << client_id << ", Soket: " << client_socket << ")" << std::endl;
    print_server_clients_list();
}


// --- YENİDEN YAZILMIŞ handle_client FONKSİYONU ---
/**
 * @brief Her istemci için ayrı bir thread'de çalışan ana mantık.
 * Gelen veriyi okur ve istemcinin durumuna göre ya komut olarak işler ya da diğer istemciye yönlendirir.
 */
void handle_client(int client_socket, std::string client_id) {
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        std::cout << "Sunucu: İstemci thread'i başlatıldı ID: " << client_id << ", Soket: " << client_socket << std::endl;
    }

    char buffer[8192]; // Veri okumak için tek bir buffer

    while (true) {
        ssize_t bytes_read = ::read(client_socket, buffer, sizeof(buffer));

        if (bytes_read <= 0) {
            // Okuma hatası veya bağlantı kapanması
            break;
        }

        std::lock_guard<std::mutex> lock(clients_mutex);
        
        // İstemci hala listede mi diye kontrol et (başka bir thread temizlemiş olabilir)
        if (!clients_by_id.count(client_id)) {
            break;
        }

        ClientInfo& self = clients_by_id.at(client_id);

        // DURUM KONTROLÜ: Tünel modunda mıyız?
        if (self.status == "VncTunnelling") {
            // Evet, tünel modundayız. Veriyi doğrudan diğer istemciye yönlendir.
            if (!self.peer_id.empty() && clients_by_id.count(self.peer_id)) {
                int peer_socket = clients_by_id.at(self.peer_id).socket_fd;
                #ifdef __linux__
                    int flags = MSG_NOSIGNAL;
                #else
                    int flags = 0;
                #endif
                ::send(peer_socket, buffer, bytes_read, flags);
            }
        } 
        else {
            // Hayır, komut modundayız. Gelen veriyi komut olarak işle.
            // Okunan veriyi istemcinin kişisel komut tamponuna ekle
            self.command_buffer.insert(self.command_buffer.end(), buffer, buffer + bytes_read);

            // Tamponda tam bir komut (newline ile biten) var mı diye kontrol et
            auto& cb = self.command_buffer;
            auto newline_it = std::find(cb.begin(), cb.end(), '\n');

            while (newline_it != cb.end()) {
                // Komutu tampondan çıkar
                std::string command_line(cb.begin(), newline_it);
                cb.erase(cb.begin(), newline_it + 1);

                // Boşlukları temizle
                command_line.erase(0, command_line.find_first_not_of(" \t\r\n"));
                command_line.erase(command_line.find_last_not_of(" \t\r\n") + 1);

                if(command_line.empty()) {
                    // Bir sonraki komuta geç
                    newline_it = std::find(cb.begin(), cb.end(), '\n');
                    continue;
                }
                
                std::cout << "Sunucu: ID " << client_id << "'den komut alındı: " << command_line << std::endl;

                std::stringstream ss(command_line);
                std::string command_verb;
                ss >> command_verb;
                std::transform(command_verb.begin(), command_verb.end(), command_verb.begin(), ::tolower);
                
                // --- Komut İşleme Mantığı ---
                if (command_verb == "start_vnc_tunnel") {
                    self.status = "VncReady";
                    std::cout << "Sunucu: ID " << client_id << " durumu VncReady olarak ayarlandı." << std::endl;
                    if (!self.peer_id.empty() && clients_by_id.count(self.peer_id)) {
                        ClientInfo& peer = clients_by_id.at(self.peer_id);
                        if (peer.status == "VncReady") {
                            std::cout << "Sunucu: Her iki taraf da VncReady! ID " << client_id << " ve ID " << self.peer_id << " için VncTunnelling başlatılıyor." << std::endl;
                            
                            self.status = "VncTunnelling";
                            peer.status = "VncTunnelling";
                            
                            send_message(self.socket_fd, "TUNNEL_ACTIVE");
                            send_message(peer.socket_fd, "TUNNEL_ACTIVE");

                            // Komut modundayken birikmiş olabilecek verileri temizle ve yönlendir
                            if (!peer.command_buffer.empty()) {
                                ::send(self.socket_fd, peer.command_buffer.data(), peer.command_buffer.size(), 0);
                                peer.command_buffer.clear();
                            }
                            if (!self.command_buffer.empty()) {
                                ::send(peer.socket_fd, self.command_buffer.data(), self.command_buffer.size(), 0);
                                self.command_buffer.clear();
                            }
                        }
                    }
                }
                else if (command_verb == "connect") {
                    std::string target_id;
                    ss >> target_id;
                    if (clients_by_id.count(target_id) && client_id != target_id && self.status == "Idle" && clients_by_id.at(target_id).status == "Idle") {
                        ClientInfo& target = clients_by_id.at(target_id);
                        self.status = "Connecting";
                        self.peer_id = target_id;
                        target.status = "Connecting";
                        target.peer_id = client_id;
                        send_message(target.socket_fd, "INCOMING " + client_id);
                        send_message(self.socket_fd, "CONNECTING " + target_id);
                        std::cout << "Sunucu: " << client_id << " -> " << target_id << " bağlantı isteği gönderildi." << std::endl;
                    } else {
                        send_message(client_socket, "ERROR CONNECT: Hedef veya siz uygun durumda değilsiniz ya da ID geçersiz.");
                    }
                }
                else if (command_verb == "accept") {
                    std::string requester_id;
                    ss >> requester_id;
                    if (self.status == "Connecting" && self.peer_id == requester_id && clients_by_id.count(requester_id) && clients_by_id.at(requester_id).status == "Connecting") {
                        ClientInfo& requester = clients_by_id.at(requester_id);
                        self.status = "Connected";
                        requester.status = "Connected";
                        send_message(requester.socket_fd, "ACCEPTED " + client_id);
                        send_message(self.socket_fd, "CONNECTION_ESTABLISHED " + requester_id);
                        std::cout << "Sunucu: " << client_id << " <-> " << requester_id << " bağlantısı kuruldu." << std::endl;
                    } else {
                        send_message(client_socket, "ERROR ACCEPT: Geçersiz kabul komutu veya durum.");
                    }
                }
                // ... diğer komutlar (list, reject, disconnect, msg) buraya eklenebilir ...
                else {
                    send_message(client_socket, "ERROR Bilinmeyen komut: " + command_verb);
                }
                print_server_clients_list();

                // Bir sonraki komut için döngüye devam et
                newline_it = std::find(cb.begin(), cb.end(), '\n');
            }
        }
    } // while(true) sonu

    // Döngüden çıkıldıysa, bağlantı kopmuştur. Temizlik yap.
    cleanup_client(client_id, client_socket);
    ::close(client_socket);
    std::cout << "Sunucu: İstemci thread'i sonlandırıldı ID: " << client_id << std::endl;
}


// --- Ana Sunucu Fonksiyonu ---
int main(int argc, char *argv[]) {
    // Sinyal yönetimi (isteğe bağlı ama iyi pratik)
    signal(SIGPIPE, SIG_IGN); 

    int listen_port = 12345;
    if (argc == 2) {
        try {
            listen_port = std::stoi(argv[1]);
        } catch (const std::exception& e) {
            std::cerr << "Hata: Geçersiz port numarası. " << e.what() << std::endl;
            return 1;
        }
    }

    int server_fd;
    struct sockaddr_in address;
    if ((server_fd = ::socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Soket oluşturulamadı");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt hatası");
        ::close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Tüm arayüzlerden dinle
    address.sin_port = htons(listen_port);

    if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bağlama (bind) hatası");
        ::close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (::listen(server_fd, 10) < 0) {
        perror("Dinleme (listen) hatası");
        ::close(server_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "Sunucu başlatıldı. Dinlenen Port: " << listen_port << std::endl;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        print_server_clients_list();
    }

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_addrlen = sizeof(client_address);
        int new_socket = ::accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);

        if (new_socket < 0) {
            if (errno == EINTR) continue;
            perror("Bağlantı kabul (accept) hatası");
            continue;
        }

        std::string client_ip_str = inet_ntoa(client_address.sin_addr);
        std::string new_id_str;
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            new_id_str = generate_unique_id_unlocked();
            clients_by_id[new_id_str] = {new_socket, client_ip_str, "Idle", ""};
            id_by_socket[new_socket] = new_id_str;
        }

        std::cout << "\nYeni bağlantı kabul edildi. Gelen IP: " << client_ip_str
                  << ", Atanan ID: " << new_id_str << std::endl;
        
        if (!send_message(new_socket, "ID " + new_id_str)) {
             cleanup_client(new_id_str, new_socket); // Temizlik fonksiyonunu çağır
             ::close(new_socket);
        } else {
             {
                std::lock_guard<std::mutex> lock(clients_mutex);
                print_server_clients_list();
             }
            try {
                std::thread(handle_client, new_socket, new_id_str).detach();
            } catch (const std::system_error& e) {
                std::cerr << "Hata: İstemci thread'i başlatılamadı (ID: " << new_id_str << "): " << e.what() << std::endl;
                cleanup_client(new_id_str, new_socket);
                ::close(new_socket);
            }
        }
    }

    ::close(server_fd);
    return 0;
}
