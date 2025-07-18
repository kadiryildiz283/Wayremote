#include <iostream>
#include <string>
#include <sstream> // <-- BU SATIRI EKLEYİN
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib> // system() için
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

std::atomic<bool> g_running(true);
std::mutex g_cout_mutex;

std::vector<uint8_t> captureScreenToMemory() {
    FILE* pipe = popen("grim -", "r");
    if (!pipe) return {};
    std::vector<uint8_t> buffer;
    char read_buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(read_buffer, 1, sizeof(read_buffer), pipe)) > 0) {
        buffer.insert(buffer.end(), read_buffer, read_buffer + bytes_read);
    }
    pclose(pipe);
    return buffer;
}

void streaming_thread_func(int viewer_socket) {
    while (g_running.load()) {
        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<uint8_t> png_data = captureScreenToMemory();
        if (png_data.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        uint32_t frame_size = png_data.size();
        uint32_t network_byte_order_size = htonl(frame_size);
        if (send(viewer_socket, &network_byte_order_size, sizeof(network_byte_order_size), MSG_NOSIGNAL) <= 0) break;
        if (send(viewer_socket, png_data.data(), frame_size, MSG_NOSIGNAL) <= 0) break;
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        if (duration.count() < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 - duration.count()));
        }
    }
    g_running = false;
    std::cout << "[Yayın] Thread sonlandırıldı." << std::endl;
}

// YENİ FONKSİYON: Görüntüleyiciden gelen girdi komutlarını dinler ve uygular
void input_receiver_thread_func(int viewer_socket) {
    std::vector<char> buffer(1024);
    std::string command_buffer;

    while(g_running.load()) {
        ssize_t bytes_read = read(viewer_socket, buffer.data(), buffer.size() - 1);
        if (bytes_read <= 0) {
            g_running = false;
            break;
        }
        buffer[bytes_read] = '\0';
        command_buffer += buffer.data();

        size_t pos;
        while ((pos = command_buffer.find('\n')) != std::string::npos) {
            std::string command_line = command_buffer.substr(0, pos);
            command_buffer.erase(0, pos + 1);

            std::stringstream ss(command_line);
            std::string cmd;
            ss >> cmd;
            
            if (cmd == "MOVE") {
                int x, y;
                ss >> x >> y;
                std::string ydotool_cmd = "ydotool mousemove " + std::to_string(x) + " " + std::to_string(y);
                system(ydotool_cmd.c_str());
            } else if (cmd == "LCLICK") {
                system("ydotool click 0xC0"); // Sol tıklama
            }
        }
    }
    std::cout << "[Girdi] Thread sonlandırıldı." << std::endl;
}

int main(int argc, char *argv[]) {
    // ... (soket, bind, listen, accept kodları önceki gibi) ...
    if (argc != 2) {
        std::cerr << "Kullanım: " << argv[0] << " <dinlenecek_port>" << std::endl; return 1;
    }
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(std::stoi(argv[1]));
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { perror("bind hatası"); return 1; }
    if (listen(server_fd, 1) < 0) { perror("listen hatası"); return 1; }
    std::cout << "[Paylaşan] Görüntüleyici bağlantısı bekleniyor..." << std::endl;
    int viewer_socket = accept(server_fd, nullptr, nullptr);
    ::close(server_fd);
    std::cout << "[Paylaşan] Görüntüleyici bağlandı. Akış ve girdi dinleme başlıyor..." << std::endl;

    // İki thread'i de başlat
    std::thread stream_thread(streaming_thread_func, viewer_socket);
    std::thread input_thread(input_receiver_thread_func, viewer_socket);

    stream_thread.join();
    input_thread.join();

    std::cout << "[Paylaşan] Oturum sonlandı." << std::endl;
    return 0;
}

