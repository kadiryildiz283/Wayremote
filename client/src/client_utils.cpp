#include "../includes/client_utils.h" // Kendi başlık dosyamız

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <algorithm> // std::transform
#include <cstdlib>   // system(), getenv(), free(), _exit(), strdup()
#include <cstdio>    // perror()
#include <cstring>   // memset(), strdup()
#include <cstdint>   // uint8_t için
#include <SDL2/SDL.h>     // SDL2 ana başlık dosyası
#include <sys/select.h>   // select() fonksiyonu için
// Ağ işlemleri için
#include <sys/socket.h>
#include <netinet/in.h> // sockaddr_in, htons
#include <arpa/inet.h>  // inet_pton, inet_ntoa
#include <unistd.h>     // ::read, ::send, ::close, fork, execvp, _exit (POSIX)

#ifndef _WIN32
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid
#endif

// libVNCclient başlık dosyası
#include <rfb/rfbclient.h>
extern std::atomic<bool> client_a_waiting_for_tunnel_activation;
extern std::atomic<bool> client_a_vnc_data_mode_active; // YENİ EXTERN
// --- Global Değişkenlere Erişim (client_main.cpp'de tanımlı olanlar) ---
extern std::atomic<bool> running;
extern std::mutex cout_mutex;
// İstemci A'nın TUNNEL_ACTIVE bekleme durumu için (client_main.cpp'de tanımlı)
extern std::atomic<bool> client_a_waiting_for_tunnel_activation;
// --- libVNCclient için Global Framebuffer ve Yardımcılar ---
static std::vector<uint8_t> g_vnc_framebuffer_storage;
static int g_vnc_fb_width = 0;
static int g_vnc_fb_height = 0;
static unsigned int g_vnc_client_bpp = 0;
static unsigned int g_vnc_client_depth = 0;

// --- libVNCclient Callback Fonksiyonları ---
static rfbBool AllocFrameBuffer(rfbClient* client) {
    client->format.bitsPerPixel = 32;
    client->format.depth = 24;
    client->format.bigEndian = FALSE;
    client->format.trueColour = TRUE;
    client->format.redMax = 255; client->format.greenMax = 255; client->format.blueMax = 255;
    client->format.redShift = 16; client->format.greenShift = 8; client->format.blueShift = 0; // RGBA varsayımı

    size_t new_size = (size_t)client->width * client->height * (client->format.bitsPerPixel / 8);
    if (g_vnc_framebuffer_storage.size() < new_size) {
        try {
            g_vnc_framebuffer_storage.resize(new_size);
        } catch (const std::bad_alloc& e) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cerr << "[VNC Lib HATA] AllocFrameBuffer: Bellek ayrılamadı - " << e.what() << std::endl;
            return FALSE;
        }
    }
    client->frameBuffer = g_vnc_framebuffer_storage.data();
    if (client->frameBuffer) {
        g_vnc_fb_width = client->width;
        g_vnc_fb_height = client->height;
        g_vnc_client_bpp = client->format.bitsPerPixel;
        g_vnc_client_depth = client->format.depth;
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "[VNC Lib] AllocFrameBuffer: İstemci Formatı " << g_vnc_fb_width << "x" << g_vnc_fb_height
                      << " bpp:" << g_vnc_client_bpp << " depth:" << g_vnc_client_depth
                      << " | Buffer boyutu: " << g_vnc_framebuffer_storage.size() << " byte" << std::endl;
        }
        return TRUE;
    }
    return FALSE;
}

static void GotFrameBufferUpdate(rfbClient* client, int x, int y, int w, int h) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "[VNC Lib] GotFrameBufferUpdate: x=" << x << ", y=" << y << ", w=" << w << ", h=" << h << std::endl;
    // TODO (SDL): Burada client->frameBuffer içindeki güncellenmiş piksel verisini SDL ile ekrana çiz.
}

static char* GetPassword(rfbClient* client) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "[VNC Lib] GetPassword çağrıldı. Şifresiz devam ediliyor." << std::endl;
    return strdup(""); // libvncclient bu belleği serbest bırakır
}

// --- Diğer Yardımcı Fonksiyonlar ---
#ifndef _WIN32
bool command_exists(const std::string& command_name) {
    std::string check_cmd = "which " + command_name + " > /dev/null 2>&1";
    int ret = system(check_cmd.c_str());
    return (ret == 0);
}
#endif

bool send_server_message(int sock_fd, const std::string& message) {
    if (sock_fd <= 0) return false;
    std::string full_message = message + "\n";
    #ifdef __linux__
        int flags = MSG_NOSIGNAL;
    #else
        int flags = 0;
    #endif
    ssize_t bytes_sent = ::send(sock_fd, full_message.c_str(), full_message.length(), flags);
    if (bytes_sent < 0) {
        // perror("send_server_message HATA"); // Detaylı hata için açılabilir
        return false;
    }
    return (size_t)bytes_sent == full_message.length();
}

void start_vnc_server() {
    // Bu fonksiyon çağrıldığında cout_mutex'in dışarıda kilitli olduğu varsayılır.
    std::cout << "[DEBUG] Entering start_vnc_server function." << std::endl;
    int result = -1;
    std::string command_to_run_str;
    char** argv_list = nullptr;
    bool command_attempted = false;
    bool proceed_to_execute = false;
    std::string session_type_str = "unknown";

    #ifdef _WIN32
        std::cout << "[Platform] Windows algılandı." << std::endl;
        std::cout << "[Bilgi] Önceden yüklenmiş VNC servisi başlatılıyor..." << std::endl;
        const char* vnc_service_name = "YourVNCServiceName"; // !!! DEĞİŞTİRİLMELİ !!!
        std::string command_to_run_win = "sc start ";
        command_to_run_win += vnc_service_name;
        std::cout << "[DEBUG] Windows komutu deneniyor: " << command_to_run_win << std::endl;
        result = system(command_to_run_win.c_str());
        command_attempted = true;
        if (result == 0) {
            std::cout << "[Bilgi] 'sc start " << vnc_service_name << "' komutu başarıyla çalıştırıldı (Kod: 0)." << std::endl;
        } else {
            std::cerr << "[Hata] VNC servisi ('" << vnc_service_name << "') başlatılamadı! (sc dönüş kodu: " << result << ")" << std::endl;
            result = -1;
        }
    #elif defined(__linux__)
        std::cout << "[Platform] Linux algılandı." << std::endl;
        const char* session_type_env = getenv("XDG_SESSION_TYPE");
        session_type_str = session_type_env ? session_type_env : "unknown";
        std::cout << "[Bilgi] Oturum Türü (XDG_SESSION_TYPE): " << session_type_str << std::endl;

        if (session_type_str == "wayland") {
            command_to_run_str = "wayvnc";
            if (command_exists(command_to_run_str)) {

std::cout << "[Bilgi] 'wayvnc' bulundu. Argümanlar hazırlanıyor..." << std::endl;
                
                std::string socket_path = "/tmp/wayvnc_sock_" + std::to_string(getpid());
                std::cout << "[DEBUG] wayvnc için kullanılacak benzersiz soket yolu: " << socket_path << std::endl;

                // --- DEĞİŞİKLİK: Debug parametresi '-d' eklendi ---
                argv_list = new char*[6]; // "wayvnc", "-d", "127.0.0.1", "-S", <yol>, NULL için 6 eleman
                argv_list[0] = strdup("wayvnc");
                argv_list[1] = strdup("-d"); // DEBUG parametresi
                argv_list[2] = strdup("127.0.0.1");
                argv_list[3] = strdup("-S");
                argv_list[4] = strdup(socket_path.c_str());
                argv_list[5] = NULL;
                // --- DEĞİŞİKLİK SONU ---
                
                proceed_to_execute = true;
           } else {
                std::cerr << "[HATA] 'wayvnc' komutu bulunamadı! Lütfen kurun." << std::endl;
            }           
        } else if (session_type_str == "x11") {
            command_to_run_str = "x11vnc";
            if (command_exists(command_to_run_str)) {
                std::cout << "[Bilgi] 'x11vnc' bulundu. Argümanlar hazırlanıyor..." << std::endl;
                argv_list = new char*[7];
                argv_list[0] = strdup("x11vnc"); argv_list[1] = strdup("-localhost");
                argv_list[2] = strdup("-nopw"); argv_list[3] = strdup("-once");
                argv_list[4] = strdup("-quiet"); argv_list[5] = strdup("-bg"); argv_list[6] = NULL;
                proceed_to_execute = true;
            } else { std::cerr << "[HATA] 'x11vnc' komutu bulunamadı! Lütfen kurun." << std::endl; }
        } else { std::cerr << "[Uyarı] Bilinmeyen Linux oturum türü: '" << session_type_str << "'" << std::endl; }

        if (proceed_to_execute && argv_list != nullptr && argv_list[0] != nullptr) {
            std::cout << "[Bilgi] '" << command_to_run_str << "' fork/execvp ile deneniyor..." << std::endl;
            pid_t pid = fork();
            if (pid == -1) { perror("[HATA] fork başarısız"); result = -1; }
            else if (pid == 0) { // Çocuk işlem
                execvp(argv_list[0], argv_list);
                perror(("[HATA] execvp başarısız (" + std::string(argv_list[0]) + ")").c_str());
                for(int i = 0; argv_list[i] != NULL; ++i) { free(argv_list[i]); } delete[] argv_list;
                _exit(1); // _exit!
            } else { // Ebeveyn işlem
                std::cout << "[Bilgi] '" << argv_list[0] << "' işlemi (PID: " << pid << ") başarıyla başlatıldı (ebeveyn)." << std::endl;
                result = 0;
            }
            command_attempted = true;
            if (pid != 0 && argv_list != nullptr) { // Ebeveyn veya fork hatasında temizle
                for(int i = 0; argv_list[i] != NULL; ++i) { free(argv_list[i]); } delete[] argv_list; argv_list = nullptr;
            }
        } else if (!command_to_run_str.empty() && !proceed_to_execute) { // Komut adı var ama bulunamadı/execute edilmedi
            result = -1; command_attempted = true;
        } else if (proceed_to_execute && (argv_list == nullptr || argv_list[0] == nullptr)) { // Hatalı argüman listesi
             std::cerr << "[HATA] Komut için argüman listesi oluşturulamadı: " << command_to_run_str << std::endl;
             result = -1; command_attempted = true; // Denendi ama başarısız
        } else { result = -1; } // Diğer durumlar
    #elif defined(__APPLE__)
        std::cout << "[Platform] macOS algılandı." << std::endl; command_attempted = false;
        std::cout << "[Bilgi] Lütfen Sistem Ayarları > Paylaşma > Ekran Paylaşma'yı etkinleştirin." << std::endl;
    #else
        std::cout << "[Uyarı] Bilinmeyen işletim sistemi." << std::endl; command_attempted = false;
    #endif

    if (command_attempted) { // Sadece bir komut denemesi yapıldıysa sonucu değerlendir
        if (result != 0) { std::cerr << "[Hata] Genel VNC sunucu başlatma denemesi başarısız oldu." << std::endl; }
    }
    // Not: (Not: Henüz VNC trafiği...) mesajları process_server_message'a taşındı.
    std::cout << "[DEBUG] Exiting start_vnc_server function." << std::endl;
}

// VNC Uplink Thread (Agent B - Yerel VNC'den Sunucuya)
void vnc_uplink_thread_func(int local_vnc_fd, int sock_to_relay, std::atomic<bool>& app_is_running_ref, std::mutex& c_mutex_ref) {
    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Uplink] Thread başlatıldı. Yerel VNC (Soket: " << local_vnc_fd
                  << ") dinleniyor. Relay sunucusu (Soket: " << sock_to_relay << ")" << std::endl;
    }
    // Sunucu komutları küçük harfe çevirdiği için küçük harf gönderiyoruz
    if (!send_server_message(sock_to_relay, "start_vnc_tunnel")) {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cerr << "[VNC Uplink] HATA: Sunucuya 'start_vnc_tunnel' gönderilemedi." << std::endl;
        if (local_vnc_fd > 0) ::close(local_vnc_fd);
        std::cout << "[VNC Uplink] Thread sonlandırılıyor ('start_vnc_tunnel' hatası)." << std::endl;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Uplink] Sunucuya 'start_vnc_tunnel' komutu gönderildi." << std::endl;
    }

    std::vector<char> buffer(8192);
    ssize_t bytes_read; ssize_t bytes_sent;
    while (app_is_running_ref) {
        bytes_read = ::read(local_vnc_fd, buffer.data(), buffer.size());
        if (bytes_read > 0) {
            { std::lock_guard<std::mutex> lock(c_mutex_ref);
              std::cout << "[VNC Uplink] Yerel VNC'den " << bytes_read << " byte okundu. Relay sunucusuna gönderiliyor..." << std::endl; }
            #ifdef __linux__
                int flags = MSG_NOSIGNAL;
            #else
                int flags = 0;
            #endif
            bytes_sent = ::send(sock_to_relay, buffer.data(), bytes_read, flags);
            if (bytes_sent < 0) { std::lock_guard<std::mutex> lock(c_mutex_ref); perror("[VNC Uplink] Relay'e veri gönderme hatası"); break; }
            else if (bytes_sent < bytes_read) { std::lock_guard<std::mutex> lock(c_mutex_ref); std::cerr << "[VNC Uplink] UYARI: Relay'e eksik veri gönderildi." << std::endl;}
        } else if (bytes_read == 0) { std::lock_guard<std::mutex> lock(c_mutex_ref); std::cout << "[VNC Uplink] Yerel VNC bağlantısı kapandı." << std::endl; break; }
        else { // bytes_read < 0
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            std::lock_guard<std::mutex> lock(c_mutex_ref); perror("[VNC Uplink] Yerel VNC'den okuma hatası"); break;
        }
    }
    if (local_vnc_fd > 0) { ::close(local_vnc_fd); }
    { std::lock_guard<std::mutex> lock(c_mutex_ref); std::cout << "[VNC Uplink] Thread sonlandırıldı." << std::endl; }
}

void vnc_control_downlink_thread_func(int local_vnc_fd, int sock_to_relay, std::atomic<bool>& app_is_running_ref, std::mutex& c_mutex_ref) {
    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Downlink(B)] Thread başlatıldı. Relay (Soket: " << sock_to_relay
                  << ") dinleniyor. Hedef: Yerel VNC (Soket: " << local_vnc_fd << ")" << std::endl;
    }

    std::vector<char> buffer(8192);
    ssize_t bytes_read;
    ssize_t bytes_sent;

    while (app_is_running_ref) {
        // 1. Relay sunucusundan veri oku (bu, İstemci A'dan gelen kontrol verisidir)
        bytes_read = ::read(sock_to_relay, buffer.data(), buffer.size());

        if (bytes_read > 0) {
            {
                std::lock_guard<std::mutex> lock(c_mutex_ref);
                std::cout << "[VNC Downlink(B)] Relay sunucusundan " << bytes_read << " byte kontrol verisi alındı. Yerel VNC'ye gönderiliyor..." << std::endl;
            }

            // 2. Okunan veriyi yerel VNC sunucusuna yaz
            bytes_sent = ::send(local_vnc_fd, buffer.data(), bytes_read, 0);
            if (bytes_sent < 0) {
                std::lock_guard<std::mutex> lock(c_mutex_ref);
                perror("[VNC Downlink(B)] Yerel VNC'ye veri gönderme hatası");
                break; // Hata durumunda döngüyü sonlandır
            } else if (bytes_sent < bytes_read) {
                std::lock_guard<std::mutex> lock(c_mutex_ref);
                std::cerr << "[VNC Downlink(B)] UYARI: Yerel VNC'ye tüm veri gönderilemedi." << std::endl;
            }

        } else if (bytes_read == 0) {
            std::lock_guard<std::mutex> lock(c_mutex_ref);
            std::cout << "[VNC Downlink(B)] Relay sunucusu bağlantısı kapandı." << std::endl;
            break;
        } else { // Hata
            if (errno == EINTR) { continue; } // Sinyal kesintisi, devam et
            std::lock_guard<std::mutex> lock(c_mutex_ref);
            perror("[VNC Downlink(B)] Relay sunucusundan okuma hatası");
            break;
        }
    }
    // Bu thread sonlandığında, muhtemelen tüm VNC oturumu bitmiştir.
    // Soketler diğer thread veya ana program tarafından kapatılacaktır.
    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Downlink(B)] Thread sonlandırıldı." << std::endl;
    }
}
void vnc_downlink_thread_func(int sock_to_relay, std::atomic<bool>& app_is_running_ref, std::mutex& c_mutex_ref) {
    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Downlink] Thread başlatıldı. Relay (Soket: " << sock_to_relay << ") dinleniyor." << std::endl;
        std::cout << "[VNC Lib] libVNCclient başlatılıyor..." << std::endl;
    }

    rfbClient* client = rfbGetClient(8,3,4);
    if (!client) {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cerr << "[VNC Lib HATA] rfbGetClient başarısız!" << std::endl;
        return;
    }

    client->MallocFrameBuffer = AllocFrameBuffer;
    client->GotFrameBufferUpdate = GotFrameBufferUpdate;
    client->GetPassword = GetPassword;
    client->canHandleNewFBSize = TRUE;
    client->sock = sock_to_relay;

    if (!InitialiseRFBConnection(client)) {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cerr << "[VNC Lib HATA] InitialiseRFBConnection başarısız!" << std::endl;
        rfbClientCleanup(client);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Lib] Bağlantı başlatıldı (InitialiseRFBConnection başarılı)." << std::endl;
        std::cout << "[VNC Lib] Anlaşılan RFB versiyonu: " << (int)client->major << "." << (int)client->minor << std::endl;
        std::cout << "[VNC Lib] Masaüstü Adı: " << (client->desktopName ? client->desktopName : "(yok)") << std::endl;
        std::cout << "[VNC Lib] Framebuffer: " << client->width << "x" << client->height << std::endl;
    }

    // --- YENİ KISIM: SDL Penceresi Oluşturma ---
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cerr << "[SDL HATA] SDL başlatılamadı: " << SDL_GetError() << std::endl;
        rfbClientCleanup(client);
        return;
    }

    SDL_Window* window = SDL_CreateWindow(
        (std::string("Uzak Masaüstü: ") + (client->desktopName ? client->desktopName : "")).c_str(), // Pencere başlığı
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, // Pencere konumu
        client->width, client->height, // Boyutlar libVNCclient'ten
        SDL_WINDOW_SHOWN // Pencere gösterilsin
    );

    if (!window) {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cerr << "[SDL HATA] Pencere oluşturulamadı: " << SDL_GetError() << std::endl;
        SDL_Quit();
        rfbClientCleanup(client);
        return;
    }
    
    // Şimdilik sadece boş bir pencere açıyoruz.
    // Sonraki adımda renderer ve texture oluşturup framebuffer'ı çizeceğiz.

    // --- Ana Olay ve Mesaj Döngüsü ---
    SDL_Event event;
    while (app_is_running_ref) {
        // 1. SDL olaylarını işle (kullanıcının pencereyi kapatması gibi)
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                {
                    std::lock_guard<std::mutex> lock(c_mutex_ref);
                    std::cout << "[SDL Bilgi] Pencere kapatma isteği alındı. Program sonlandırılıyor." << std::endl;
                }
                app_is_running_ref = false; // Ana programın kapanmasını tetikle
            }
            // TODO (Adım 2a): Klavye ve fare olaylarını burada yakalayacağız.
        }

        // 2. VNC soketinde okunacak veri var mı diye kontrol et (engellemesiz)
        fd_set fds;
        struct timeval tv;
        int ret;

        FD_ZERO(&fds);
        FD_SET(client->sock, &fds);
        tv.tv_sec = 0; // Bekleme yapma
        tv.tv_usec = 1000; // 1 milisaniye timeout (çok kısa)

        ret = select(client->sock + 1, &fds, NULL, NULL, &tv);
        if (ret == -1) {
            std::lock_guard<std::mutex> lock(c_mutex_ref);
            perror("[VNC Downlink HATA] select() hatası");
            app_is_running_ref = false; // Ciddi hata, çık
        } else if (ret > 0) { // Sokette okunacak veri var
            int result = HandleRFBServerMessage(client); // Veriyi işle
            if (result <= 0) {
                 std::lock_guard<std::mutex> lock(c_mutex_ref);
                 if (result < 0) { std::cerr << "[VNC Lib HATA] HandleRFBServerMessage hata verdi." << std::endl; }
                 else { std::cout << "[VNC Lib] Sunucu bağlantısı kapandı." << std::endl; }
                 app_is_running_ref = false; // Bağlantı koptu, çık
            }
        }
        // Eğer ret == 0 ise, timeout oldu, okunacak veri yok. Döngü devam eder.

        // 3. Ekrana çizim yap (şimdilik sadece arkaplanı temizle)
        // Sonraki adımda buraya texture çizimi gelecek.

        // Kısa bir bekleme (CPU'yu %100 kullanmamak için)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // --- Temizlik ---
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    rfbClientCleanup(client);
    {
        std::lock_guard<std::mutex> lock(c_mutex_ref);
        std::cout << "[VNC Downlink] Thread sonlandırıldı (SDL ve libVNCclient temizlendi)." << std::endl;
    }
}


// Sunucudan Gelen Mesajları İşleyen Ana Fonksiyon
void process_server_message(const std::string& server_msg_line, std::string& my_id_ref, std::mutex& cout_mtx_param, int sock_to_server) {
    std::lock_guard<std::mutex> lock(cout_mtx_param);
    std::stringstream ss(server_msg_line);
    std::string msg_type_original;
    ss >> msg_type_original;

    std::string msg_type = msg_type_original;
    std::transform(msg_type.begin(), msg_type.end(), msg_type.begin(), ::tolower); // Komutları küçük harfe çevir

    std::cout << "\n[Sunucu] " << server_msg_line << std::endl;

    if (msg_type == "id") {
        ss >> my_id_ref;
        std::cout << "[Bilgi] Size atanan ID: " << my_id_ref << std::endl;
    } else if (msg_type == "incoming") {
        std::string source_id; ss >> source_id;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "[Bağlantı İsteği] ID '" << source_id << "' sizinle bağlanmak istiyor." << std::endl;
        std::cout << "Kabul etmek için: accept " << source_id << std::endl;
        std::cout << "Reddetmek için:   reject " << source_id << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    } else if (msg_type == "connecting") {
        std::string target_id; ss >> target_id;
        std::cout << "[Bilgi] ID '" << target_id << "' hedefine bağlantı isteği gönderildi, yanıt bekleniyor..." << std::endl;
    } else if (msg_type == "accepted") { // İstemci A (bağlanan)
        std::string peer_id_b; ss >> peer_id_b; // Bu, accept eden İstemci B'nin ID'si
        std::cout << "[Bilgi] Bağlantı isteğiniz ID '" << peer_id_b << "' tarafından KABUL EDİLDİ." << std::endl;

        if (!send_server_message(sock_to_server, "start_vnc_tunnel")) { // Küçük harf
            std::cerr << "[HATA] Sunucuya 'start_vnc_tunnel' gönderilemedi (Agent A)." << std::endl;
        } else {
            std::cout << "[Bilgi] Sunucuya 'start_vnc_tunnel' komutu gönderildi (Agent A)." << std::endl;
        }
        
        // VNC downlink thread'i TUNNEL_ACTIVE mesajı gelince başlatılacak.
        client_a_waiting_for_tunnel_activation = true; // client_main.cpp'de tanımlı global
        std::cout << "[Bilgi] VNC Downlink (libVNCclient) thread'i için sunucudan TUNNEL_ACTIVE bekleniyor..." << std::endl;

    } else if (msg_type == "rejected") {
        std::string peer_id; ss >> peer_id;
        std::cout << "[Bilgi] Bağlantı isteğiniz ID '" << peer_id << "' tarafından REDDEDİLDİ." << std::endl;
        client_a_waiting_for_tunnel_activation = false; // Reddedildiyse bekleme
    } else if (msg_type == "connection_established") { // İstemci B (kabul eden)
        std::string peer_id_a; ss >> peer_id_a; // Bu, istek yapan İstemci A'nın ID'si
        std::cout << "[Bilgi] ID '" << peer_id_a << "' ile BAĞLANTI KURULDU." << std::endl;
        
        std::cout << "[DEBUG_PROCESS] start_vnc_server() çağrılmadan hemen önce." << std::endl;
        start_vnc_server();
        std::cout << "[DEBUG_PROCESS] start_vnc_server() çağrıldıktan hemen sonra." << std::endl;
        
        const int VNC_START_DELAY_SECONDS = 3;
        std::cout << "[DEBUG_PROCESS] Yerel VNC sunucusunun başlaması için " << VNC_START_DELAY_SECONDS << " saniye bekleniyor..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(VNC_START_DELAY_SECONDS));
        std::cout << "[DEBUG_PROCESS] Bekleme tamamlandı." << std::endl;
        
        std::cout << "[Tünel-Adım1] Yerel VNC sunucusuna (127.0.0.1:5900) bağlanmayı deneniyor..." << std::endl;
        int local_vnc_sock = -1; struct sockaddr_in local_vnc_addr;
        const char* LOCAL_VNC_IP = "127.0.0.1"; const int LOCAL_VNC_PORT = 5900;
        
        local_vnc_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (local_vnc_sock < 0) { perror("[HATA] Yerel VNC soketi oluşturulamadı"); }
        else {
            memset(&local_vnc_addr, 0, sizeof(local_vnc_addr));
            local_vnc_addr.sin_family = AF_INET; local_vnc_addr.sin_port = htons(LOCAL_VNC_PORT);
            if (inet_pton(AF_INET, LOCAL_VNC_IP, &local_vnc_addr.sin_addr) <= 0) { 
                perror(("[HATA] Geçersiz yerel VNC IP adresi: " + std::string(LOCAL_VNC_IP)).c_str()); 
                ::close(local_vnc_sock); local_vnc_sock = -1;
            } else {
                std::cout << "[DEBUG] Yerel VNC'ye connect() çağrılıyor..." << std::endl;
                if (::connect(local_vnc_sock, (struct sockaddr *)&local_vnc_addr, sizeof(local_vnc_addr)) < 0) { 
                    perror(("[HATA] Yerel VNC sunucusuna bağlanılamadı (" + std::string(LOCAL_VNC_IP) + ":" + std::to_string(LOCAL_VNC_PORT) + ")").c_str()); 
                    std::cerr << "       - VNC sunucusu (" << LOCAL_VNC_IP << ":" << LOCAL_VNC_PORT << ") gerçekten başlatıldı ve çalışıyor mu?" << std::endl; 
                    ::close(local_vnc_sock); local_vnc_sock = -1;
                } else {
                                  std::cout << "[Bilgi] Yerel VNC sunucusuna başarıyla bağlanıldı (Soket: " << local_vnc_sock << ")." << std::endl;
                    
                    // --- DEĞİŞİKLİK BURADA: İKİ THREAD'İ DE BAŞLAT ---
                    std::cout << "[Bilgi] İki yönlü VNC tünel thread'leri başlatılıyor..." << std::endl;
                    
                    // Uplink Thread (Yerel VNC -> Relay Sunucusu)
                    std::thread vnc_up_thread(vnc_uplink_thread_func, local_vnc_sock, sock_to_server, std::ref(running), std::ref(cout_mtx_param));
                    
                    // Downlink Thread (Relay Sunucusu -> Yerel VNC)
                    std::thread vnc_down_thread(vnc_control_downlink_thread_func, local_vnc_sock, sock_to_server, std::ref(running), std::ref(cout_mtx_param));
                    
                    // Thread'leri ayır, kendi başlarına çalışsınlar
                    vnc_up_thread.detach();
                    vnc_down_thread.detach();
                    
                }
            }
        }
        std::cout << "       (İpucu: İstemci A tarafında VNC Görüntüleyici (dahili) başlayacak.)" << std::endl;
        std::cout << "       Bağlantıyı bitirmek için 'disconnect' komutunu kullanın." << std::endl;
    }
    else if (msg_type == "tunnel_active") {
        std::cout << "[Bilgi] Sunucu VNC tünelinin aktif olduğunu bildirdi." << std::endl;
        if (client_a_waiting_for_tunnel_activation) { // client_main.cpp'de tanımlı global
            client_a_waiting_for_tunnel_activation = false;
        }
            client_a_vnc_data_mode_active = true;
            // Şimdi libVNCclient thread'ini başlat, soket artık onun.
            std::cout << "[Bilgi] TUNNEL_ACTIVE alındı, VNC Downlink (libVNCclient) thread'i başlatılıyor..." << std::endl;
            std::thread vnc_session_thread(vnc_downlink_thread_func, sock_to_server, std::ref(running), std::ref(cout_mtx_param));
            vnc_session_thread.detach();
    } else if (msg_type == "peer_disconnected") { 
        std::string peer_id; ss >> peer_id; 
        std::cout << "[Bilgi] ID '" << peer_id << "' bağlantısı KESİLDİ." << std::endl; 
        client_a_waiting_for_tunnel_activation = false; // Eğer bekliyorsa artık beklemesin
        // TODO: Aktif VNC thread'lerini (uplink/downlink) burada sonlandırmak için bir flag veya mekanizma.
        // 'running' flag'i zaten genel olarak bunu yapar ama daha spesifik bir session flag'i olabilir.
    } else if (msg_type == "disconnected_ok") { 
        std::cout << "[Bilgi] Mevcut bağlantınız başarıyla sonlandırıldı." << std::endl; 
        client_a_waiting_for_tunnel_activation = false;
        // TODO: Aktif VNC thread'lerini sonlandır.
    } else if (msg_type == "msg_from") { 
        std::string source_id; ss >> source_id;
        std::string message_content; std::getline(ss, message_content);
        if(!message_content.empty() && message_content[0] == ' ') message_content.erase(0,1);
        std::cout << "[Mesaj (" << source_id << ")] " << message_content << std::endl;
    } else if (msg_type == "error") { 
        std::string error_message; std::getline(ss, error_message);
        if(!error_message.empty() && error_message[0] == ' ') error_message.erase(0,1);
        std::cerr << "[Sunucu Hatası] " << error_message << std::endl;
    } else if (msg_type == "list_begin") { std::cout << "--- Bağlı İstemciler Listesi ---" << std::endl; }
    else if (msg_type == "list_end") { std::cout << "--------------------------------" << std::endl; }
    else if (msg_type_original.rfind("ID: ", 0) == 0 ) { // LIST içeriği için orijinal msg_type_original (büyük harf 'ID:')
         std::cout << "  " << server_msg_line << std::endl;
    } else {
        // std::cerr << "[Uyarı] Bilinmeyen sunucu mesajı: " << server_msg_line << std::endl;
    }

    std::cout << "> ";
    std::cout.flush();
}
