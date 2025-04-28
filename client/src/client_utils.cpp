#include "../includes/client_utils.h" // İlgili başlık dosyası
#include <iostream>     // std::cout, std::cerr, std::endl
#include <string>       // std::string
#include <sstream>      // std::stringstream
#include <cstdlib>      // system(), getenv(), free(), _exit()
#include <cstdio>       // perror()
#include <cstring>      // strdup(), memset() (gerçi memset main'deydi)
#include <mutex>        // std::mutex, std::lock_guard (process_server_message'dan parametre olarak gelir)

// Platforma Özel Kütüphaneler (Linux/macOS için)
#ifndef _WIN32
#include <unistd.h>     // fork, execvp, _exit, read, close (gerçi read/close main'deydi)
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid (kullanılmasa da genellikle ilgili)
#include <sys/socket.h> // send()
#else
// Windows'a özel bir include gerekirse buraya eklenebilir (örn. Windows.h)
// Ancak şu anki kod sadece system("sc start...") kullandığı için <cstdlib> yeterli.
#endif


// --- Yardımcı Fonksiyonlar ---

#ifndef _WIN32
// Linux/macOS için komutun PATH içinde olup olmadığını kontrol eder
bool command_exists(const std::string& command_name) {
    std::string check_cmd = "which " + command_name + " > /dev/null 2>&1";
    int ret = system(check_cmd.c_str());
    return (ret == 0);
}
#endif

// Sunucuya mesaj gönderir (sonuna '\n' ekler)
bool send_server_message(int sock, const std::string& message) {
    if (sock <= 0) return false;
    std::string full_message = message + "\n";
    // MSG_NOSIGNAL flag'i, kırık pipe durumunda SIGPIPE sinyali göndermesini engeller (Linux/BSD)
    // Windows'ta bu flag yoktur, ancak send genellikle hata kodu döndürür.
    #ifdef __linux__
        int flags = MSG_NOSIGNAL;
    #else
        int flags = 0;
    #endif
    ssize_t bytes_sent = send(sock, full_message.c_str(), full_message.length(), flags);
    if (bytes_sent < 0) {
        // perror("send_server_message HATA"); // Çok fazla log üretebilir
        return false;
    }
    return (size_t)bytes_sent == full_message.length();
}


// VNC Sunucusunu Platforma Göre Başlatan Fonksiyon
// ÖNEMLİ: Bu fonksiyon çağrıldığında 'cout_mtx' kilidinin
// çağıran fonksiyon (process_server_message) tarafından alınmış olduğu varsayılır.
void start_vnc_server() {
    // NO MUTEX LOCK HERE

    std::cout << "[DEBUG] Entering start_vnc_server function." << std::endl;

    int result = -1; // Başarı durumu için (-1: denemedi/hata, 0: başarılı deneme)
    std::string command_to_run_str; // Sadece loglama için komut adı
    char** argv_list = nullptr; // execvp için argüman listesi
    bool command_attempted = false; // Bir komut denendi mi?
    bool proceed_to_execute = false; // Kontrol sonrası çalıştırmaya uygun mu?
    std::string session_type_str = "unknown"; // Linux için

    #ifdef _WIN32
        // --- Windows ---
        std::cout << "[Platform] Windows algılandı." << std::endl;
        std::cout << "[Bilgi] Önceden yüklenmiş VNC servisi başlatılıyor..." << std::endl;

        // !!! DEĞİŞTİRİLMELİ !!! Gerçek Windows Servis Adı buraya yazılacak.
        const char* vnc_service_name = "YourVNCServiceName"; // Örn: "tvnserver", "uvnc_service"

        std::string command_to_run = "sc start ";
        command_to_run += vnc_service_name;
        std::cout << "[DEBUG] Windows komutu deneniyor: " << command_to_run << std::endl;

        result = system(command_to_run.c_str());
        command_attempted = true;

        if (result == 0) {
             std::cout << "[Bilgi] 'sc start " << vnc_service_name << "' komutu başarıyla çalıştırıldı (Kod: 0)." << std::endl;
             std::cout << "       (Servisin durumu kontrol edilmedi, başlatıldığı veya zaten çalıştığı varsayıldı)." << std::endl;
             std::cout << "       VNC sunucusunun localhost:5900 (veya yapılandırılan port) üzerinde dinlediğini varsayıyoruz." << std::endl;
             // result = 0; // Zaten 0
        } else {
             std::cerr << "[Hata] VNC servisi ('" << vnc_service_name << "') başlatılamadı! (sc start dönüş kodu: " << result << ")" << std::endl;
             std::cerr << "       Olası Nedenler: Servis bulunamadı, Yetki yok, Servis devre dışı vb." << std::endl;
             result = -1; // Başarısızlık olarak işaretle
        }

    #elif defined(__linux__)
        // --- Linux ---
        std::cout << "[Platform] Linux algılandı." << std::endl;
        const char* session_type_env = getenv("XDG_SESSION_TYPE");
        session_type_str = session_type_env ? session_type_env : "unknown";
        std::cout << "[Bilgi] Oturum Türü (XDG_SESSION_TYPE): " << session_type_str << std::endl;

        if (session_type_str == "wayland") {
            command_to_run_str = "wayvnc";
            if (command_exists(command_to_run_str)) {
                std::cout << "[Bilgi] 'wayvnc' bulundu. Argümanlar hazırlanıyor..." << std::endl;
                argv_list = new char*[3];
                argv_list[0] = strdup("wayvnc");
                argv_list[1] = strdup("127.0.0.1");
                argv_list[2] = NULL;
                proceed_to_execute = true;
            } else {
                std::cerr << "[HATA] 'wayvnc' komutu bulunamadı!" << std::endl;
                std::cerr << "       Lütfen dağıtımınıza uygun komut ile kurun." << std::endl;
            }
        } else if (session_type_str == "x11") {
            command_to_run_str = "x11vnc";
             if (command_exists(command_to_run_str)) {
                 std::cout << "[Bilgi] 'x11vnc' bulundu. Argümanlar hazırlanıyor..." << std::endl;
                 argv_list = new char*[7];
                 argv_list[0] = strdup("x11vnc");
                 argv_list[1] = strdup("-localhost");
                 argv_list[2] = strdup("-nopw");
                 argv_list[3] = strdup("-once");
                 argv_list[4] = strdup("-quiet");
                 argv_list[5] = strdup("-bg");
                 argv_list[6] = NULL;
                 proceed_to_execute = true;
             } else {
                 std::cerr << "[HATA] 'x11vnc' komutu bulunamadı!" << std::endl;
                 std::cerr << "       Lütfen dağıtımınıza uygun komut ile kurun." << std::endl;
             }
        } else {
             std::cerr << "[Uyarı] Bilinmeyen veya desteklenmeyen Linux oturum türü: '" << session_type_str << "'" << std::endl;
        }

        // Eğer çalıştırmaya uygunsa fork/exec yap
        if (proceed_to_execute && argv_list != nullptr && argv_list[0] != nullptr) {
            std::cout << "[Bilgi] '" << command_to_run_str << "' fork/execvp ile deneniyor..." << std::endl;
            pid_t pid = fork();

            if (pid == -1) {
                perror("[HATA] fork başarısız");
                result = -1;
            } else if (pid == 0) {
                // --- Çocuk İşlem ---
                // std::cout << "[DEBUG] Çocuk işlem: execvp(" << argv_list[0] << ", ...) çağrılıyor..." << std::endl; // Çocukta cout riskli olabilir
                execvp(argv_list[0], argv_list);
                // Eğer execvp dönerse hata vardır
                perror(("[HATA] execvp başarısız (" + std::string(argv_list[0]) + ")").c_str());
                // Belleği temizle ve çık
                for(int i = 0; argv_list[i] != NULL; ++i) { free(argv_list[i]); }
                delete[] argv_list;
                _exit(1); // Çocuğu _exit ile sonlandır
            } else {
                // --- Ebeveyn İşlem ---
                std::cout << "[Bilgi] '" << argv_list[0] << "' işlemi (PID: " << pid << ") başarıyla başlatıldı (ebeveyn)." << std::endl;
                result = 0; // Başarılı başlatma (ebeveyn açısından)
            }
            command_attempted = true; // Komut denendi

             // Ebeveyn veya fork hatası durumunda ayrılan belleği temizle
            if (pid != 0 && argv_list != nullptr) {
                 for(int i = 0; argv_list[i] != NULL; ++i) { free(argv_list[i]); }
                 delete[] argv_list;
                 argv_list = nullptr; // Önemli: İşaretçiyi null yap
            }

        } else if (command_attempted) { // proceed_to_execute false ise veya argv_list hatalıysa
             result = -1; // Başlatma denenemedi
             // argv_list null kontrolü yukarıda var, tekrar temizlemeye gerek yok
             if (argv_list != nullptr) { // Eğer sadece proceed_to_execute false idiyse ama liste ayrıldıysa
                 for(int i = 0; argv_list[i] != NULL; ++i) { free(argv_list[i]); }
                 delete[] argv_list;
                 argv_list = nullptr;
             }
        } // Linux exec bitti


    #elif defined(__APPLE__)
        // --- macOS ---
        std::cout << "[Platform] macOS algılandı." << std::endl;
        std::cout << "[Bilgi] Lütfen Sistem Ayarları > Paylaşma > Ekran Paylaşma'yı etkinleştirin." << std::endl;
        command_attempted = false; // Bir komut denenmedi

    #else
        // --- Bilinmeyen OS ---
        std::cout << "[Uyarı] Bilinmeyen işletim sistemi." << std::endl;
        command_attempted = false; // Bir komut denenmedi
    #endif

    // Sonuç kontrolü (Sadece bir işlem başlatma denemesi yapıldıysa)
    if (command_attempted) {
        if (result == 0) {
            // Başarı mesajı platforma özel blok içinde zaten yazıldı.
        } else {
             std::cerr << "[Hata] Genel işlem başlatma denemesi başarısız oldu (örn. fork hatası veya komut bulunamadı)." << std::endl;
        }
    }

    std::cout << "       (Not: Henüz VNC trafiği tünellemesi aktif değil.)" << std::endl;
    std::cout << "       Bağlantıyı bitirmek için 'disconnect' komutunu kullanın." << std::endl;

    std::cout << "[DEBUG] Exiting start_vnc_server function." << std::endl;
}


// Sunucudan Gelen Mesajları İşleyen Fonksiyon
void process_server_message(const std::string& server_msg_line, std::string& my_id_ref, std::mutex& cout_mtx, int sock) {
    // Kilidi fonksiyonun başında alıp sonunda bırakmak, içerideki tüm cout kullanımlarını korur
    std::lock_guard<std::mutex> lock(cout_mtx);

    std::stringstream ss(server_msg_line);
    std::string msg_type;
    ss >> msg_type;

    // Ham mesajı yazdırmayı isteğe bağlı yapabiliriz, bazen çok kalabalık olabilir.
    std::cout << "\n[Sunucu] " << server_msg_line << std::endl;

    if (msg_type == "ID") {
        ss >> my_id_ref; // Referans üzerinden my_id'yi güncelle
        std::cout << "[Bilgi] Size atanan ID: " << my_id_ref << std::endl;
    } else if (msg_type == "INCOMING") {
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

        // VNC Sunucusunu Başlatmak İçin İlgili Fonksiyonu Çağır
        // Bu fonksiyonun içindeki cout'lar da dışarıdaki mutex tarafından korunuyor.
        start_vnc_server();

    } else if (msg_type == "PEER_DISCONNECTED") {
        std::string peer_id;
        ss >> peer_id;
        std::cout << "[Bilgi] ID '" << peer_id << "' bağlantısı KESİLDİ." << std::endl;
    } else if (msg_type == "DISCONNECTED_OK") {
         std::cout << "[Bilgi] Mevcut bağlantınız başarıyla sonlandırıldı." << std::endl;
    } else if (msg_type == "MSG_FROM") {
        std::string source_id;
        ss >> source_id;
        // Mesajın geri kalanını al (ilk iki kelimeden sonrasını)
        std::string message_content;
        std::string word1, word2;
        ss >> word1 >> word2; // msg_type ve source_id'yi atla (zaten okundu)
        std::getline(ss, message_content); // Satırın geri kalanını al
        if (!message_content.empty() && message_content[0] == ' ') { // Başta boşluk varsa kaldır
            message_content.erase(0, 1);
        }
        std::cout << "[Mesaj (" << source_id << ")] " << message_content << std::endl;
    } else if (msg_type == "ERROR") {
         std::string error_message;
         std::getline(ss, error_message); // Hata mesajının geri kalanını al
         if (!error_message.empty() && error_message[0] == ' ') { error_message.erase(0, 1); }
         std::cerr << "[Sunucu Hatası] " << error_message << std::endl;
    } else if (msg_type == "LIST_BEGIN") {
         std::cout << "--- Bağlı İstemciler Listesi ---" << std::endl;
    } else if (msg_type == "LIST_END") {
         std::cout << "--------------------------------" << std::endl;
    } else if (server_msg_line.rfind("ID: ", 0) == 0) { // LIST içeriği için basit kontrol
         std::cout << "  " << server_msg_line << std::endl;
    } else {
        // Bilinmeyen mesaj türü - Ham log yeterli olabilir.
        // std::cerr << "[Uyarı] Bilinmeyen sunucu mesajı türü: " << msg_type << std::endl;
    }

    // Yeni komut istemini yazdır
    std::cout << "> ";
    std::cout.flush();
}
