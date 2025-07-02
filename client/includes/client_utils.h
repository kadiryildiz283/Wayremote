#ifndef CLIENT_UTILS_H
#define CLIENT_UTILS_H

#include <string>
#include <atomic> // std::atomic için
#include <mutex>  // std::mutex için

extern std::atomic<bool> running;
extern std::mutex cout_mutex;
extern std::atomic<bool> client_a_waiting_for_tunnel_activation; // <<-- BU SATIRI EKLEYİN
// --- Fonksiyon Bildirimleri ---

/**
 * @brief Sunucuya formatlanmış bir mesaj gönderir (sonuna '\n' ekler).
 * @param sock Sunucuya bağlı olan soket tanımlayıcısı.
 * @param message Gönderilecek mesaj metni.
 * @return Mesaj başarıyla gönderildiyse true, aksi takdirde false.
 */
bool send_server_message(int sock, const std::string& message);

/**
 * @brief Sunucudan gelen bir mesaj satırını işler ve uygun eylemleri tetikler.
 * @param server_msg_line Sunucudan gelen tam mesaj satırı (genellikle '\n' ile biter).
 * @param my_id_ref İstemcinin kendi ID'sini saklayan string'e referans (ID mesajı gelirse güncellenir).
 * @param cout_mtx Konsol çıktılarını senkronize etmek için kullanılan mutex'e referans.
 * @param sock_to_server İstemcinin ana sunucuya olan bağlantı soketi.
 */
void process_server_message(const std::string& server_msg_line,
                            std::string& my_id_ref,
                            std::mutex& cout_mtx,
                            int sock_to_server);

/**
 * @brief Platforma göre uygun VNC sunucusunu başlatmayı dener (Kontrol Edilen İstemci - Agent B için).
 * Bu fonksiyon çağrıldığında cout_mutex'in dışarıda (process_server_message içinde)
 * zaten kilitli olduğu varsayılır, bu yüzden içindeki std::cout çağrıları güvendedir.
 */
void start_vnc_server();

/**
 * @brief Yerel VNC sunucusundan veri okur ve relay sunucusuna gönderir (Kontrol Edilen İstemci - Agent B için).
 * Bu fonksiyon yeni bir thread üzerinde çalıştırılmak üzere tasarlanmıştır.
 * @param local_vnc_fd Yerel VNC sunucusuna (örn. wayvnc) olan soket tanımlayıcısı.
 * @param sock_to_relay Merkezi relay sunucusuna olan soket tanımlayıcısı.
 * @param app_is_running_ref Ana uygulamanın genel çalışma durumunu gösteren atomik boolean'a referans.
 * @param c_mutex_ref Konsol çıktıları için paylaşılan mutex'e referans.
 */
void vnc_uplink_thread_func(int local_vnc_fd,
                            int sock_to_relay,
                            std::atomic<bool>& app_is_running_ref,
                            std::mutex& c_mutex_ref);

/**
 * @brief Relay sunucusundan gelen VNC verisini alır ve libVNCclient ile işler (Kontrol Eden İstemci - Agent A için).
 * Bu fonksiyon yeni bir thread üzerinde çalıştırılmak üzere tasarlanmıştır.
 * @param sock_to_relay Merkezi relay sunucusuna olan soket tanımlayıcısı.
 * @param app_is_running_ref Ana uygulamanın genel çalışma durumunu gösteren atomik boolean'a referans.
 * @param c_mutex_ref Konsol çıktıları için paylaşılan mutex'e referans.
 */

void vnc_control_downlink_thread_func(int local_vnc_fd,
                                      int sock_to_relay,
                                      std::atomic<bool>& app_is_running_ref,
                                      std::mutex& c_mutex_ref);
// manage_vnc_proxy_session_thread fonksiyonu, libVNCclient'in doğrudan entegrasyonuyla
// şimdilik gereksiz hale geldiği için kaldırıldı. Eğer İstemci A'nın ayrı bir
// yerel VNC proxy sunucusu gibi davranması istenirse tekrar eklenebilir.

#endif // CLIENT_UTILS_H
