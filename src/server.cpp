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

// İstemci bilgilerini ve durumunu tutan yapı
struct ClientInfo {
    int socket_fd;
    std::string ip_address;
    std::string status = "Idle";
    std::string pending_peer_id = "";
    std::vector<char> incoming_buffer;
};

// Global Veri Yapıları ve Mutex
std::unordered_map<std::string, ClientInfo> clients_by_id;
std::unordered_map<int, std::string> id_by_socket;
std::mutex clients_mutex;

// --- Yardımcı Fonksiyonlar ---
std::string generate_unique_id_unlocked(); // Düzeltilmiş fonksiyon bildirimi
bool send_message(int socket_fd, const std::string& message);
void print_server_clients_list();
void handle_client(int client_socket, std::string client_id_param);


// --- Fonksiyon Tanımları ---

// YENİ DÜZELTİLMİŞ ID ÜRETME FONKSİYONU
// Bu fonksiyon, çağrıldığı yerde clients_mutex'in zaten kilitli olduğunu varsayar.
std::string generate_unique_id_unlocked() {
    // Random üreteçlerini static yaparak her seferinde yeniden oluşturulmasını engelle
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(100000, 999999);
    std::string new_id;
    do {
        std::stringstream ss;
        ss << std::setw(6) << std::setfill('0') << dist(gen);
        new_id = ss.str();
        // KİLİT KALDIRILDI! Çağıran fonksiyon zaten kilitliyor.
        if (clients_by_id.find(new_id) == clients_by_id.end()) break;
    } while (true);
    return new_id;
}

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

void print_server_clients_list() {
    // Bu fonksiyon çağrıldığında clients_mutex'in zaten kilitli olduğu varsayılır.
    std::cout << "\n--- Sunucu: Bağlı İstemciler ---" << std::endl;
    if (clients_by_id.empty()) {
        std::cout << "(Şu an bağlı istemci yok)" << std::endl;
    } else {
        for (const auto& pair : clients_by_id) {
            std::cout << "  - ID: " << pair.first << ", IP: " << pair.second.ip_address
                      << ", Soket: " << pair.second.socket_fd << ", Durum: " << pair.second.status;
            if (!pair.second.pending_peer_id.empty()) {
                 std::cout << " (Peer: " << pair.second.pending_peer_id << ")";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "---------------------------------" << std::endl;
    std::cout << "Yeni bağlantılar bekleniyor..." << std::endl;
}

void handle_client(int client_socket, std::string client_id_param) {
    // ... (handle_client fonksiyonu öncekiyle aynı, buraya kopyalanabilir) ...
    // Tamlık için tekrar ekliyorum:
    std::string current_client_id = client_id_param;
    std::vector<char> read_buffer(8192);

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        std::cout << "Sunucu: İstemci thread'i başlatıldı ID: " << current_client_id
                  << ", Soket: " << client_socket << std::endl;
    }

    while (true) {
        std::string client_status;
        std::string peer_id;
        int peer_socket = -1;
        bool client_exists = false;

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = clients_by_id.find(current_client_id);
            if (it != clients_by_id.end()) {
                client_exists = true;
                ClientInfo& client_info = it->second;
                client_status = client_info.status;
                if (!client_info.pending_peer_id.empty() && clients_by_id.count(client_info.pending_peer_id)) {
                    peer_id = client_info.pending_peer_id;
                    peer_socket = clients_by_id.at(peer_id).socket_fd;
                }
            }
        }

        if (!client_exists) { break; }

        ssize_t bytes_read = ::read(client_socket, read_buffer.data(), read_buffer.size());
        if (bytes_read <= 0) { break; }

        if (client_status == "VncTunnelling" && peer_socket != -1) {
            #ifdef __linux__
                int flags = MSG_NOSIGNAL;
            #else
                int flags = 0;
            #endif
            ::send(peer_socket, read_buffer.data(), bytes_read, flags);
        } else {
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (!clients_by_id.count(current_client_id)) continue;
            ClientInfo& client_info = clients_by_id.at(current_client_id);

            client_info.incoming_buffer.insert(client_info.incoming_buffer.end(), read_buffer.data(), read_buffer.data() + bytes_read);

            if (client_info.status == "VncReady") {
                std::cout << "[DEBUG] ID " << current_client_id << " VncReady durumunda, "
                          << bytes_read << " byte veri buffer'a eklendi. Peer bekleniyor." << std::endl;
            } else {
                bool command_processed_in_cycle;
                do {
                    command_processed_in_cycle = false;
                    auto& CIB_ref = client_info.incoming_buffer;
                    auto newline_it = std::find(CIB_ref.begin(), CIB_ref.end(), '\n');
                    
                    if (newline_it != CIB_ref.end()) {
                        std::string command_line(CIB_ref.begin(), newline_it);
                        CIB_ref.erase(CIB_ref.begin(), newline_it + 1);
                        command_processed_in_cycle = true;

                        std::cout << "Sunucu: ID " << current_client_id << "'den komut alındı: " << command_line << std::endl;
                        std::stringstream ss(command_line);
                        std::string command_verb;
                        ss >> command_verb;
                        std::transform(command_verb.begin(), command_verb.end(), command_verb.begin(), ::tolower);

                        if (command_verb == "start_vnc_tunnel") {
                            client_info.status = "VncReady";
                            std::cout << "Sunucu: ID " << current_client_id << " durumu VncReady olarak ayarlandı." << std::endl;
                            if (!client_info.pending_peer_id.empty() && clients_by_id.count(client_info.pending_peer_id)) {
                                ClientInfo& peer_info = clients_by_id.at(client_info.pending_peer_id);
                                if (peer_info.status == "VncReady") {
                                    std::cout << "Sunucu: Her iki taraf da VncReady! ID " << current_client_id
                                              << " ve ID " << client_info.pending_peer_id << " için VncTunnelling başlatılıyor." << std::endl;
                                    client_info.status = "VncTunnelling";
                                    peer_info.status = "VncTunnelling";
                                    send_message(client_info.socket_fd, "TUNNEL_ACTIVE");
                                    send_message(peer_info.socket_fd, "TUNNEL_ACTIVE");

                                    if (!client_info.incoming_buffer.empty()) {
                                        ::send(peer_info.socket_fd, client_info.incoming_buffer.data(), client_info.incoming_buffer.size(), 0);
                                        client_info.incoming_buffer.clear();
                                    }
                                    if (!peer_info.incoming_buffer.empty()) {
                                        ::send(client_info.socket_fd, peer_info.incoming_buffer.data(), peer_info.incoming_buffer.size(), 0);
                                        peer_info.incoming_buffer.clear();
                                    }
                                }
                            }
                        }
                        else if (command_verb == "list") {
                            std::string list_response = "LIST_BEGIN";
                            for (const auto& pair : clients_by_id) {
                                if (pair.first == current_client_id) continue;
                                list_response += "\nID: " + pair.first + " Durum: " + pair.second.status;
                            } list_response += "\nLIST_END";
                            send_message(client_socket, list_response);
                        }
                        else if (command_verb == "connect") {
                            std::string target_id; ss >> target_id;
                             if (clients_by_id.count(target_id) && current_client_id != target_id) {
                                ClientInfo& source_info = client_info;
                                ClientInfo& target_info = clients_by_id.at(target_id);
                                if (source_info.status == "Idle" && target_info.status == "Idle") {
                                    source_info.status = "Connecting"; source_info.pending_peer_id = target_id;
                                    target_info.status = "Connecting"; target_info.pending_peer_id = current_client_id;
                                    send_message(target_info.socket_fd, "INCOMING " + current_client_id);
                                    send_message(source_info.socket_fd, "CONNECTING " + target_id);
                                    std::cout << "Sunucu: " << current_client_id << " -> " << target_id << " bağlantı isteği gönderildi." << std::endl;
                                } else { send_message(client_socket, "ERROR CONNECT: Hedef veya siz uygun durumda değilsiniz."); }
                            } else { send_message(client_socket, "ERROR CONNECT: Geçersiz veya bulunamayan ID."); }
                        }
                        else if (command_verb == "accept") {
                            std::string requester_id; ss >> requester_id;
                             if (clients_by_id.count(requester_id)) {
                                ClientInfo& accepter_info = client_info;
                                ClientInfo& requester_info = clients_by_id.at(requester_id);
                                if (accepter_info.status == "Connecting" && accepter_info.pending_peer_id == requester_id &&
                                    requester_info.status == "Connecting" && requester_info.pending_peer_id == current_client_id) {
                                    accepter_info.status = "Connected"; requester_info.status = "Connected";
                                    send_message(requester_info.socket_fd, "ACCEPTED " + current_client_id);
                                    send_message(accepter_info.socket_fd, "CONNECTION_ESTABLISHED " + requester_id);
                                    std::cout << "Sunucu: " << current_client_id << " <-> " << requester_id << " bağlantısı kuruldu." << std::endl;
                                } else { send_message(client_socket, "ERROR ACCEPT: Geçersiz kabul komutu veya durum."); }
                            } else { send_message(client_socket, "ERROR ACCEPT: Geçersiz ID."); }
                        }
                        else if (command_verb == "reject") {
                             std::string requester_id; ss >> requester_id;
                             if (clients_by_id.count(requester_id)) {
                                ClientInfo& rejecter_info = client_info;
                                ClientInfo& requester_info = clients_by_id.at(requester_id);
                                 if (rejecter_info.status == "Connecting" && rejecter_info.pending_peer_id == requester_id &&
                                    requester_info.status == "Connecting" && requester_info.pending_peer_id == current_client_id) {
                                    rejecter_info.status = "Idle"; rejecter_info.pending_peer_id = "";
                                    requester_info.status = "Idle"; requester_info.pending_peer_id = "";
                                    send_message(requester_info.socket_fd, "REJECTED " + current_client_id);
                                    send_message(rejecter_info.socket_fd, "REJECTION_SENT " + requester_id);
                                    std::cout << "Sunucu: " << current_client_id << " -> " << requester_id << " bağlantı isteğini reddetti." << std::endl;
                                } else { send_message(client_socket, "ERROR REJECT: Geçersiz reddetme komutu veya durum."); }
                            } else { send_message(client_socket, "ERROR REJECT: Geçersiz ID."); }
                        }
                        else if (command_verb == "disconnect") {
                            if (!client_info.pending_peer_id.empty() && clients_by_id.count(client_info.pending_peer_id)) {
                                ClientInfo& peer_info_ref = clients_by_id.at(client_info.pending_peer_id);
                                peer_info_ref.status = "Idle";
                                peer_info_ref.pending_peer_id = "";
                                send_message(peer_info_ref.socket_fd, "PEER_DISCONNECTED " + current_client_id);
                                 std::cout << "Sunucu: Peer " << client_info.pending_peer_id << " bilgilendirildi ve Idle yapıldı." << std::endl;
                            }
                            client_info.status = "Idle";
                            client_info.pending_peer_id = "";
                            send_message(client_socket, "DISCONNECTED_OK");
                            std::cout << "Sunucu: ID " << current_client_id << " mevcut bağlantısını kesti." << std::endl;
                        }
                        else if (command_verb == "msg") {
                            if (client_info.status == "Connected" && !client_info.pending_peer_id.empty() && clients_by_id.count(client_info.pending_peer_id)) {
                                const ClientInfo& receiver_info = clients_by_id.at(client_info.pending_peer_id);
                                std::string message_content;
                                std::getline(ss, message_content);
                                if(!message_content.empty() && message_content[0] == ' ') message_content.erase(0,1);
                                send_message(receiver_info.socket_fd, "MSG_FROM " + current_client_id + " " + message_content);
                            } else { send_message(client_socket, "ERROR MSG: Mesaj göndermek için 'Connected' durumda olmalısınız."); }
                        }
                        else {
                            std::cout << "[DEBUG] Sunucu: Bilinmeyen komut: '" << command_verb << "'" << std::endl;
                            send_message(client_socket, "ERROR Bilinmeyen komut: " + command_verb);
                        }
                        print_server_clients_list();
                    }
                } while(command_processed_in_cycle);
            }
        }
    }

    std::cout << "\nSunucu: İstemci bağlantısı koptu veya hata (ID: " << current_client_id << ", Soket: " << client_socket << ")" << std::endl;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients_by_id.count(current_client_id)) {
             const auto& client_info_on_disconnect = clients_by_id.at(current_client_id);
             if (!client_info_on_disconnect.pending_peer_id.empty() && clients_by_id.count(client_info_on_disconnect.pending_peer_id)) {
                  ClientInfo& peer_info_on_disconnect = clients_by_id.at(client_info_on_disconnect.pending_peer_id);
                  peer_info_on_disconnect.status = "Idle";
                  peer_info_on_disconnect.pending_peer_id = "";
                  send_message(peer_info_on_disconnect.socket_fd, "PEER_DISCONNECTED " + current_client_id);
                  std::cout << "Sunucu: Bağlı olan diğer istemci (" << client_info_on_disconnect.pending_peer_id
                            << ") bilgilendirildi ve Idle yapıldı." << std::endl;
             }
            clients_by_id.erase(current_client_id);
        }
        id_by_socket.erase(client_socket);
        print_server_clients_list();
    }
    ::close(client_socket);
    std::cout << "Sunucu: İstemci thread'i sonlandırıldı ID: " << current_client_id << std::endl;
}

// --- Ana Sunucu Döngüsü ---
int main(int argc, char *argv[]) {
    const char* DEFAULT_IP = "0.0.0.0";
    int DEFAULT_PORT = 12345;
    const char* listen_ip = DEFAULT_IP;
    int listen_port = DEFAULT_PORT;

    if (argc == 3) {
        listen_ip = argv[1];
        try {
            listen_port = std::stoi(argv[2]);
            if (listen_port <= 0 || listen_port > 65535) {
                 std::cerr << "Hata: Geçersiz port numarası (1-65535 arası olmalı): " << argv[2] << std::endl; return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Hata: Port numarası (" << argv[2] << ") dönüştürülemedi: " << e.what() << std::endl; return 1;
        }
    } else if (argc != 1) {
        std::cerr << "Kullanım: " << argv[0] << " [dinlenecek_ip] [dinlenecek_port]" << std::endl; return 1;
    }

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    if ((server_fd = ::socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Soket oluşturulamadı"); exit(EXIT_FAILURE);
    }
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        perror("setsockopt hatası"); ::close(server_fd); exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    if (::inet_pton(AF_INET, listen_ip, &address.sin_addr) <= 0) {
        std::cerr << "Hata: Geçersiz IP adresi: " << listen_ip << std::endl;
        ::close(server_fd); exit(EXIT_FAILURE);
    }
    address.sin_port = htons(listen_port);

    if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror(("Bağlama (bind) hatası IP:" + std::string(listen_ip) + " Port:" + std::to_string(listen_port)).c_str());
        ::close(server_fd); exit(EXIT_FAILURE);
    }
    if (::listen(server_fd, 10) < 0) {
        perror("Dinleme (listen) hatası"); ::close(server_fd); exit(EXIT_FAILURE);
    }

    std::cout << "Sunucu başlatıldı. Dinlenen IP: " << listen_ip << ", Port: " << listen_port << std::endl;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        print_server_clients_list();
    }

    while (true) {
        // Düzeltilmiş DEBUG logları, artık gerekli değil gibi, ama istersen açabilirsin.
        // std::cout << "[DEBUG_SERVER_MAIN] Accept döngüsü başı, yeni bağlantı bekleniyor..." << std::endl;
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
            new_id_str = generate_unique_id_unlocked(); // DÜZELTİLDİ
            ClientInfo client_info = {new_socket, client_ip_str, "Idle", ""};
            clients_by_id[new_id_str] = client_info;
            id_by_socket[new_socket] = new_id_str;
        }

        std::cout << "\nYeni bağlantı kabul edildi. Gelen IP: " << client_ip_str
                  << ", Atanan ID: " << new_id_str << std::endl;

        bool id_sent_successfully = send_message(new_socket, "ID " + new_id_str);
        if (!id_sent_successfully) {
            std::cerr << "Hata: İstemciye (ID: " << new_id_str << ") ID gönderilemedi." << std::endl;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients_by_id.erase(new_id_str);
                id_by_socket.erase(new_socket);
            }
            ::close(new_socket);
        } else {
             {
                  std::lock_guard<std::mutex> lock(clients_mutex);
                  print_server_clients_list();
             }
            try {
                std::thread client_thread(handle_client, new_socket, new_id_str);
                client_thread.detach();
            } catch (const std::system_error& e) {
                 std::cerr << "Hata: İstemci thread'i başlatılamadı (ID: " << new_id_str << "): " << e.what() << std::endl;
                 {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    clients_by_id.erase(new_id_str);
                    id_by_socket.erase(new_socket);
                 }
                 ::close(new_socket);
            }
        }
    }

    ::close(server_fd);
    return 0;
}
