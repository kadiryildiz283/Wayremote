#include <iostream>
#include <string>
#include <vector> // std::vector için eklendi
#include <unordered_map>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>      // memset için
#include <unistd.h>     // close, read, write, ::read, ::send, ::close (POSIX)
#include <sys/socket.h> // socket, bind, listen, accept, send, recv, setsockopt
#include <netinet/in.h> // sockaddr_in, htons, INADDR_ANY
#include <arpa/inet.h>  // inet_ntoa, inet_pton
#include <algorithm>
#include <thread>
// İstemci // bilgilerini ve durumunu tutan yapı
struct ClientInfo {
    int socket_fd;
    std::string ip_address;
    std::string status = "Idle"; // Olası durumlar: Idle, Connecting, Connected, VncReady, VncTunnelling
    std::string pending_peer_id = ""; // Bağlantı isteği/aktif bağlantıdaki diğer istemcinin ID'si
    std::vector<char> incoming_buffer; // Komut modunda parçalı mesajları biriktirmek için
};

// Global Veri Yapıları ve Mutex
std::unordered_map<std::string, ClientInfo> clients_by_id; // ID -> ClientInfo
std::unordered_map<int, std::string> id_by_socket;     // socket_fd -> ID
std::mutex clients_mutex;                              // Bu veri yapılarına erişimi korumak için

// --- Yardımcı Fonksiyonlar ---

// Benzersiz 6 haneli ID üretir
std::string generate_unique_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(100000, 999999); // 6 haneli ID
    std::string new_id;
    // Mutex kilidi burada gerekli değil, çünkü ID üretimi global haritaya yazmadan önce yapılır.
    // Ancak, count haritayı okuduğu için teorik olarak bir data race olabilir.
    // Pratikte ID üretimi hızlı olduğu için ve çakışma nadir olacağı için risk düşük.
    // Daha güvenli olması için ID üretimi sırasında da harita kilitlenebilir.
    // Şimdilik basit bırakalım.
    do {
        std::stringstream ss;
        ss << std::setw(6) << std::setfill('0') << dist(gen);
        new_id = ss.str();
        // clients_by_id.count() okuma yaptığı için kilitlemek daha doğru olur
        std::lock_guard<std::mutex> lock(clients_mutex); // Güvenlik için eklendi
        if (clients_by_id.find(new_id) == clients_by_id.end()) break; // Haritada yoksa çık
    } while (true);
    return new_id;
}

// Sokete mesaj gönderir (sonuna '\n' ekler)
bool send_message(int socket_fd, const std::string& message) {
    if (socket_fd <= 0) return false;
    std::string full_message = message + "\n";
    #ifdef __linux__
        int flags = MSG_NOSIGNAL; // SIGPIPE sinyalini engelle (Linux)
    #else
        int flags = 0;
    #endif
    ssize_t bytes_sent = ::send(socket_fd, full_message.c_str(), full_message.length(), flags);
    if (bytes_sent < 0) {
        // perror("Sunucu send_message hatası"); // Çok fazla log üretebilir
        return false;
    }
    return (size_t)bytes_sent == full_message.length();
}

// Bağlı istemcilerin listesini sunucu konsoluna yazdırır
void print_server_clients_list() {
    // Bu fonksiyon çağrıldığında clients_mutex'in zaten kilitli olduğu varsayılır.
    // std::lock_guard<std::mutex> lock(clients_mutex); // Bu yüzden bu satır YORUMDA kalmalı.

    // system("clear"); // Her seferinde temizlemek yerine alta ekleme daha iyi olabilir
    std::cout << "\n--- Sunucu: Bağlı İstemciler ---" << std::endl;
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
void handle_client(int client_socket, std::string client_id_param) {
    std::string current_client_id = client_id_param; // Bu thread'in yönettiği istemcinin ID'si
    std::vector<char> read_buffer(8192); // 8KB okuma tamponu

    std::cout << "Sunucu: İstemci thread'i başlatıldı ID: " << current_client_id
              << ", Soket: " << client_socket << std::endl;

    while (true) {
        std::string client_status;
        std::string peer_id_for_relay;
        int peer_socket_for_relay = -1;
        bool is_client_still_valid = false;
        std::vector<char>* current_incoming_buffer = nullptr;

        // Gerekli bilgileri ve buffer'ı mutex altında al
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (clients_by_id.count(current_client_id)) {
                is_client_still_valid = true;
                ClientInfo& client_info_ref = clients_by_id.at(current_client_id); // Referans alalım
                client_status = client_info_ref.status;
                current_incoming_buffer = &client_info_ref.incoming_buffer; // Buffer'a pointer

                if ((client_status == "Connected" || client_status == "VncReady" || client_status == "VncTunnelling") &&
                    !client_info_ref.pending_peer_id.empty() &&
                    clients_by_id.count(client_info_ref.pending_peer_id)) {
                    peer_id_for_relay = client_info_ref.pending_peer_id;
                    peer_socket_for_relay = clients_by_id.at(peer_id_for_relay).socket_fd;
                }
            }
        }

        if (!is_client_still_valid || (client_status != "VncTunnelling" && !current_incoming_buffer) ) {
            std::cout << "Sunucu: İstemci " << current_client_id
                      << " artık haritada değil veya buffer'ı yok, thread sonlandırılıyor." << std::endl;
            break;
        }

        // Veri Oku
        ssize_t bytes_read = ::read(client_socket, read_buffer.data(), read_buffer.size());

        if (bytes_read <= 0) { // Bağlantı koptu veya hata
            break; // Temizlik aşağıda yapılacak
        }

        // Veriyi İşle
        if (client_status == "VncTunnelling" && peer_socket_for_relay != -1) {
            // --- VNC Tünelleme Modu ---
            // Bu modda, okunan ham veriyi doğrudan peer'e gönderiyoruz.
            // MSG_NOSIGNAL ile gönderim yapalım (Linux için)
            #ifdef __linux__
                int flags = MSG_NOSIGNAL;
            #else
                int flags = 0;
            #endif
            ssize_t bytes_sent_relay = ::send(peer_socket_for_relay, read_buffer.data(), bytes_read, flags);
            if (bytes_sent_relay < 0) {
                // perror(("[Relay HATA] ID " + peer_id_for_relay + " hedefine veri gönderilemedi").c_str());
                 std::lock_guard<std::mutex> lock(clients_mutex); // Konsol için
                 std::cerr << "[Relay HATA] ID " << current_client_id << " -> " << peer_id_for_relay
                           << " veri gönderilemedi. (errno: " << errno << ")" << std::endl;
                // Belki burada her iki istemcinin durumunu da Idle'a çekmek gerekebilir.
            } else {
                 std::lock_guard<std::mutex> lock(clients_mutex); // Konsol için
                 std::cout << "[Relay] Sunucu: ID " << current_client_id << " -> ID " << peer_id_for_relay
                           << " : " << bytes_sent_relay << "/" << bytes_read << " byte HAM VERİ aktarıldı." << std::endl;
            }
        } else {
            // --- Komut Modu ---
            // Okunan veriyi istemcinin kendi incoming_buffer'ına ekle (mutex altında yapılmalı)
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                if (clients_by_id.count(current_client_id)) { // Hala var mı diye son bir kontrol
                     clients_by_id.at(current_client_id).incoming_buffer.insert(
                        clients_by_id.at(current_client_id).incoming_buffer.end(),
                        read_buffer.data(),
                        read_buffer.data() + bytes_read
                    );
                } else {
                    continue; // İstemci bu arada silinmiş olabilir
                }
            }


            // Buffer'dan satır satır komutları işle
            bool command_processed_this_cycle = true;
            while(command_processed_this_cycle) {
                command_processed_this_cycle = false;
                std::string command_line;
                // Buffer'dan bir satır ayıkla (mutex altında)
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    if(clients_by_id.count(current_client_id)) {
                        std::vector<char>& CIB = clients_by_id.at(current_client_id).incoming_buffer;
                        auto newline_it = std::find(CIB.begin(), CIB.end(), '\n');
                        if (newline_it != CIB.end()) {
                            command_line.assign(CIB.begin(), newline_it);
                            CIB.erase(CIB.begin(), newline_it + 1);
                            command_processed_this_cycle = true; // Bir komut işlenecek
                        }
                    } else {
                        break; // İstemci yoksa döngüden çık
                    }
                }

                if (!command_processed_this_cycle || command_line.empty()) {
                    break; // İşlenecek tam bir komut satırı yoksa bekle
                }

                // Komutu işle (Artık mutex'i komut işleme fonksiyonları içinde alacağız)
                std::cout << "Sunucu: ID " << current_client_id << "'den komut alındı: " << command_line << std::endl;
                std::stringstream ss(command_line);
                std::string command_verb; // Komut fiili
                ss >> command_verb;

                // Komutları küçük harfe çevirerek kontrol etmek daha esnek olabilir
                // std::transform(command_verb.begin(), command_verb.end(), command_verb.begin(), ::tolower);

                std::lock_guard<std::mutex> lock(clients_mutex); // Harita işlemleri için tekrar kilitle

                if (command_verb == "START_VNC_TUNNEL" || command_verb == "start_vnc_tunnel") {
                    if (clients_by_id.count(current_client_id)) {
                        ClientInfo& initiator_info = clients_by_id.at(current_client_id);
                        if ((initiator_info.status == "Connected") &&
                            !initiator_info.pending_peer_id.empty() &&
                            clients_by_id.count(initiator_info.pending_peer_id)) {
                            
                            initiator_info.status = "VncReady";
                            std::cout << "Sunucu: ID " << current_client_id << " durumu VncReady olarak ayarlandı." << std::endl;

                            ClientInfo& peer_info = clients_by_id.at(initiator_info.pending_peer_id);
                            if (peer_info.status == "VncReady") {
                                std::cout << "Sunucu: Her iki taraf da VncReady! ID " << current_client_id 
                                          << " ve ID " << initiator_info.pending_peer_id << " için VncTunnelling başlatılıyor." << std::endl;
                                initiator_info.status = "VncTunnelling";
                                peer_info.status = "VncTunnelling";
                                // İsteğe bağlı: İstemcilere tünelin aktif olduğunu bildiren bir mesaj gönderilebilir
                                send_message(initiator_info.socket_fd, "TUNNEL_ACTIVE");
                                send_message(peer_info.socket_fd, "TUNNEL_ACTIVE");
                            } else {
                                std::cout << "Sunucu: ID " << current_client_id << " VncReady, peer ("
                                          << initiator_info.pending_peer_id << ") bekleniyor (Durumu: " << peer_info.status << ")" << std::endl;
                            }
                        } else {
                            send_message(client_socket, "ERROR START_VNC_TUNNEL: Uygun durumda değilsiniz veya peer yok.");
                        }
                    }
                }
                else if (command_verb == "LIST" || command_verb == "list") {
                    std::string list_response = "LIST_BEGIN";
                    for (const auto& pair : clients_by_id) {
                        if (pair.first == current_client_id) continue; // Kendisini listeleme
                        list_response += "\nID: " + pair.first + " Durum: " + pair.second.status;
                    }
                    list_response += "\nLIST_END";
                    send_message(client_socket, list_response);
                }
                else if (command_verb == "CONNECT" || command_verb == "connect") {
                    std::string target_id;
                    ss >> target_id;
                    if (clients_by_id.count(current_client_id) && clients_by_id.count(target_id) && current_client_id != target_id) {
                        ClientInfo& source_info = clients_by_id.at(current_client_id);
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
                else if (command_verb == "ACCEPT" || command_verb == "accept") {
                    std::string requester_id;
                    ss >> requester_id;
                     if (clients_by_id.count(current_client_id) && clients_by_id.count(requester_id)) {
                        ClientInfo& accepter_info = clients_by_id.at(current_client_id);
                        ClientInfo& requester_info = clients_by_id.at(requester_id);
                        if (accepter_info.status == "Connecting" && accepter_info.pending_peer_id == requester_id &&
                            requester_info.status == "Connecting" && requester_info.pending_peer_id == current_client_id) {
                            accepter_info.status = "Connected"; // VNC Tüneli için START_VNC_TUNNEL beklenir
                            requester_info.status = "Connected";
                            send_message(requester_info.socket_fd, "ACCEPTED " + current_client_id);
                            send_message(accepter_info.socket_fd, "CONNECTION_ESTABLISHED " + requester_id);
                            std::cout << "Sunucu: " << current_client_id << " <-> " << requester_id << " bağlantısı kuruldu." << std::endl;
                        } else { send_message(client_socket, "ERROR ACCEPT: Geçersiz kabul komutu veya durum."); }
                    } else { send_message(client_socket, "ERROR ACCEPT: Geçersiz ID."); }
                }
                else if (command_verb == "REJECT" || command_verb == "reject") {
                    std::string requester_id;
                    ss >> requester_id;
                    if (clients_by_id.count(current_client_id) && clients_by_id.count(requester_id)) {
                        ClientInfo& rejecter_info = clients_by_id.at(current_client_id);
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
                else if (command_verb == "MSG" || command_verb == "msg") {
                    if (clients_by_id.count(current_client_id)) {
                        const ClientInfo& sender_info = clients_by_id.at(current_client_id);
                        if (sender_info.status == "Connected" && !sender_info.pending_peer_id.empty() && clients_by_id.count(sender_info.pending_peer_id)) {
                            const ClientInfo& receiver_info = clients_by_id.at(sender_info.pending_peer_id);
                            std::string message_content;
                            std::getline(ss, message_content); // Komuttan sonraki tüm satırı al
                            if(!message_content.empty() && message_content[0] == ' ') message_content.erase(0,1); // Baştaki boşluğu sil
                            send_message(receiver_info.socket_fd, "MSG_FROM " + current_client_id + " " + message_content);
                        } else { send_message(client_socket, "ERROR MSG: Mesaj göndermek için 'Connected' durumda olmalısınız."); }
                    }
                }
                else if (command_verb == "DISCONNECT" || command_verb == "disconnect") {
                     if (clients_by_id.count(current_client_id)) {
                        ClientInfo& self_info = clients_by_id.at(current_client_id);
                        if (!self_info.pending_peer_id.empty() && clients_by_id.count(self_info.pending_peer_id)) {
                            ClientInfo& peer_info_ref = clients_by_id.at(self_info.pending_peer_id);
                            peer_info_ref.status = "Idle";
                            peer_info_ref.pending_peer_id = "";
                            send_message(peer_info_ref.socket_fd, "PEER_DISCONNECTED " + current_client_id);
                             std::cout << "Sunucu: Peer " << self_info.pending_peer_id << " bilgilendirildi ve Idle yapıldı." << std::endl;
                        }
                        self_info.status = "Idle";
                        self_info.pending_peer_id = "";
                        send_message(client_socket, "DISCONNECTED_OK");
                        std::cout << "Sunucu: ID " << current_client_id << " mevcut bağlantısını kesti." << std::endl;
                    }
                }
                else {
                    std::cout << "[DEBUG] Sunucu: Bilinmeyen komut (Komut Modunda): '" << command_verb << "'" << std::endl;
                    send_message(client_socket, "ERROR Bilinmeyen komut: " + command_verb);
                }
                print_server_clients_list(); // Her komuttan sonra listeyi güncelleyerek yazdır
            } // while(newline_pos) - komut satırı işleme döngüsü
        } // if (VncTunnelling) else (Komut Modu)
    } // while(true) - ana read döngüsü

    // --- Bağlantı Koptuğunda Temizlik (handle_client sonu) ---
    std::cout << "\nSunucu: İstemci bağlantısı koptu veya hata (ID: " << current_client_id
              << ", Soket: " << client_socket << ")" << std::endl;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients_by_id.count(current_client_id)) {
             ClientInfo& client_info = clients_by_id.at(current_client_id); // .at() kullanmak daha güvenli (count ile kontrol sonrası)
             if (!client_info.pending_peer_id.empty() && clients_by_id.count(client_info.pending_peer_id)) {
                  ClientInfo& peer_info = clients_by_id.at(client_info.pending_peer_id);
                  // Peer'in durumunu da resetle ve bilgilendir
                  peer_info.status = "Idle";
                  peer_info.pending_peer_id = "";
                  send_message(peer_info.socket_fd, "PEER_DISCONNECTED " + current_client_id);
                  std::cout << "Sunucu: Bağlantıdaki diğer istemci (" << client_info.pending_peer_id
                            << ") bilgilendirildi ve Idle yapıldı." << std::endl;
             }
            clients_by_id.erase(current_client_id);
        }
        id_by_socket.erase(client_socket);
    }
    print_server_clients_list(); // Son durumu yazdır
    ::close(client_socket);      // Soketi kapat
    std::cout << "Sunucu: İstemci thread'i sonlandırıldı ID: " << current_client_id
              << ", Soket: " << client_socket << std::endl;
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
                 std::cerr << "Hata: Geçersiz port numarası (1-65535 arası olmalı): " << argv[2] << std::endl;
                 return 1;
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "Hata: Port numarası (" << argv[2] << ") sayısal olmalı: " << e.what() << std::endl;
            return 1;
        } catch (const std::out_of_range& e) {
            std::cerr << "Hata: Port numarası (" << argv[2] << ") aralık dışı: " << e.what() << std::endl;
            return 1;
        }
    } else if (argc != 1) {
        std::cerr << "Kullanım: " << argv[0] << " [dinlenecek_ip] [dinlenecek_port]" << std::endl;
        std::cerr << "Argüman verilmezse varsayılan olarak " << DEFAULT_IP << ":" << DEFAULT_PORT << " kullanılır." << std::endl;
        return 1;
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
        std::cerr << "Hata: Geçersiz IP adresi: " << listen_ip << " (errno: " << errno << " - " << strerror(errno) << ")" << std::endl;
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
        std::lock_guard<std::mutex> lock(clients_mutex); // print_server_clients_list çağrısı için
        print_server_clients_list();
    }


    // server.cpp -> main() fonksiyonu içindeki accept döngüsü

    while (true) {
        std::cout << "[DEBUG_SERVER_MAIN] Accept döngüsü başı, yeni bağlantı bekleniyor..." << std::endl;
        struct sockaddr_in client_address;
        socklen_t client_addrlen = sizeof(client_address);
        int new_socket = ::accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen);

        if (new_socket < 0) {
            if (errno == EINTR) { // Sinyal tarafından kesildiyse normal, devam et
                std::cout << "[DEBUG_SERVER_MAIN] accept() EINTR ile kesildi, devam ediliyor." << std::endl;
                continue;
            }
            perror("Bağlantı kabul (accept) hatası"); // Diğer hataları yazdır
            std::cout << "[DEBUG_SERVER_MAIN] accept() hatası, döngüye devam ediliyor." << std::endl;
            // Ciddi bir hata durumunda belki sunucuyu durdurmak gerekebilir,
            // ama şimdilik devam etmesini sağlayalım.
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Hızlı döngüyü engelle
            continue;
        }
        // accept() başarılı olduysa buraya gelinir
        std::cout << "[DEBUG_SERVER_MAIN] accept() başarılı. Yeni soket: " << new_socket << std::endl;

        std::string client_ip_str = inet_ntoa(client_address.sin_addr);
        std::string new_id_str;

        std::cout << "[DEBUG_SERVER_MAIN] ID üretiliyor..." << std::endl;
        // ID üretimi generate_unique_id içinde zaten kilitli
        new_id_str = generate_unique_id();
        std::cout << "[DEBUG_SERVER_MAIN] ID üretildi: " << new_id_str << std::endl;

        { // Client haritalara ekleme bloğu
            std::lock_guard<std::mutex> lock(clients_mutex);
            std::cout << "[DEBUG_SERVER_MAIN] Client haritalara ekleniyor (Soket: " << new_socket << ", ID: " << new_id_str << ")" << std::endl;
            ClientInfo client_info = {new_socket, client_ip_str, "Idle", ""};
            clients_by_id[new_id_str] = client_info;
            id_by_socket[new_socket] = new_id_str;
            std::cout << "[DEBUG_SERVER_MAIN] Client haritalara eklendi." << std::endl;
        } // Kilit burada serbest bırakılır

        // Normal log mesajı (mutex dışında)
        std::cout << "\nYeni bağlantı kabul edildi. Gelen IP: " << client_ip_str
                  << ", Atanan ID: " << new_id_str << std::endl;

        std::cout << "[DEBUG_SERVER_MAIN] İstemciye (Soket: " << new_socket << ", ID: " << new_id_str << ") ID gönderiliyor..." << std::endl;
        bool id_sent_successfully = send_message(new_socket, "ID " + new_id_str);

        if (!id_sent_successfully) {
            std::cerr << "[HATA_SERVER_MAIN] İstemciye (ID: " << new_id_str << ", Soket: " << new_socket
                      << ") ID GÖNDERİLEMEDİ." << std::endl;
            { // Başarısız istemciyi haritalardan temizle
                std::lock_guard<std::mutex> lock(clients_mutex);
                if(clients_by_id.count(new_id_str)) clients_by_id.erase(new_id_str);
                if(id_by_socket.count(new_socket)) id_by_socket.erase(new_socket); // new_socket ile silmek daha doğru
            }
            ::close(new_socket); // Soketi kapat
            std::cout << "[DEBUG_SERVER_MAIN] ID gönderim hatası sonrası istemci temizlendi." << std::endl;
             { // Listeyi yazdırmadan önce kilitle
                  std::lock_guard<std::mutex> lock(clients_mutex);
                  print_server_clients_list(); // Güncel listeyi (boş olmalı) göster
             }
        } else {
            std::cout << "[DEBUG_SERVER_MAIN] ID başarıyla gönderildi. Liste yazdırılıyor..." << std::endl;
             { // Listeyi yazdırmadan önce kilitle
                  std::lock_guard<std::mutex> lock(clients_mutex);
                  print_server_clients_list(); // Güncel listeyi göster
             }
            std::cout << "[DEBUG_SERVER_MAIN] Liste yazdırıldı. Thread başlatılıyor..." << std::endl;

            // Sadece ID başarıyla gönderildiyse thread'i başlat
            try {
                std::thread client_thread(handle_client, new_socket, new_id_str);
                client_thread.detach();
                std::cout << "[DEBUG_SERVER_MAIN] handle_client thread'i (ID: " << new_id_str << ") başarıyla başlatıldı." << std::endl;
            } catch (const std::system_error& e) {
                 std::cerr << "[HATA_SERVER_MAIN] İstemci thread'i başlatılamadı (ID: " << new_id_str << "): " << e.what() << std::endl;
                 // Thread başlatılamazsa istemciyi temizle
                 {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    if(clients_by_id.count(new_id_str)) clients_by_id.erase(new_id_str);
                    if(id_by_socket.count(new_socket)) id_by_socket.erase(new_socket);
                 }
                 ::close(new_socket);
                  { // Listeyi yazdırmadan önce kilitle
                      std::lock_guard<std::mutex> lock(clients_mutex);
                      print_server_clients_list(); // Güncel listeyi göster
                  }
            }
        } // if (id_sent_successfully) else
    } // while(true) - accept döngüsü    ::close(server_fd);
    return 0;
}
