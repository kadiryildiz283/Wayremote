#ifndef CLIENT_UTILS_H
#define CLIENT_UTILS_H

#include <string>
#include <atomic>
#include <mutex>

// Global değişkenlere erişim (veya bunları parametre olarak alan fonksiyonlar)
// extern std::atomic<bool>& running_flag; // Örnek
// extern std::mutex& cout_mutex; // Örnek

// Yardımcı Fonksiyon Bildirimleri
bool send_server_message(int sock, const std::string& message);
void process_server_message(const std::string& server_msg_line, std::string& my_id_ref, std::mutex& cout_mtx, int sock); // 4 parametre
void start_vnc_server(std::mutex& cout_mtx); // Platforma özel VNC başlatıcı

#endif // CLIENT_UTILS_H
