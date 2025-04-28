#include <iostream>
#include <string>
#include <unordered_map> // ID -> Bağlantı Bilgisi eşlemesi için
#include <random>       // Rastgele ID üretimi için
#include <mutex>        // Thread safety için (temel ekleme)
#include <optional>     // Bir değerin olup olmadığını belirtmek için (C++17)
#include <sstream>      // Sayıyı string'e çevirmek için
#include <iomanip>      // String formatlama için (örn. başa sıfır ekleme)

// Gerçek uygulamada socket descriptor, IP adresi vb. bilgiler içerebilir
struct ConnectionInfo {
    int socket_fd; // Örnek olarak socket dosya tanımlayıcısını tutalım
    std::string ip_address;
};

// Kimlik (ID) yönetimini yapan sınıf
class IDManager {
public:
    IDManager() {
        // Rastgele sayı üreteci için başlangıç ayarı
        std::random_device rd;
        gen_ = std::mt19937(rd());
        dist_ = std::uniform_int_distribution<>(1000000, 9999999); // 7 haneli ID için
    }

    // Yeni bir istemci kaydeder ve ona benzersiz bir ID atar
    // Başarılı olursa ID'yi, olmazsa boş bir optional döner
    std::optional<std::string> registerClient(const ConnectionInfo& conn_info) {
        std::lock_guard<std::mutex> lock(map_mutex_); // Mutex ile haritaya erişimi kilitle

        std::string new_id;
        int max_tries = 10; // Çakışma durumunda kaç kez denenecek
        int tries = 0;

        do {
            // Rastgele 6 haneli bir sayı üretip string'e çevir
            std::stringstream ss;
            ss << std::setw(6) << std::setfill('0') << dist_(gen_);
            new_id = ss.str();
            tries++;
            // Eğer bu ID zaten haritada yoksa veya deneme sayısı aşılırsa döngüden çık
            if (client_map_.find(new_id) == client_map_.end()) {
                break;
            }
        } while (tries < max_tries);

        // Eğer benzersiz ID bulunamadıysa (çok düşük ihtimal ama olabilir)
        if (tries == max_tries && client_map_.find(new_id) != client_map_.end()) {
             std::cerr << "Hata: Benzersiz ID üretilemedi!" << std::endl;
             return std::nullopt; // Boş optional döndür
        }

        // Yeni ID ile bağlantı bilgisini haritaya ekle
        client_map_[new_id] = conn_info;
        std::cout << "İstemci kaydedildi. ID: " << new_id << ", Soket: " << conn_info.socket_fd << std::endl;
        return new_id; // Oluşturulan ID'yi döndür
    }

    // Verilen ID'ye sahip istemcinin kaydını siler
    void unregisterClient(const std::string& id) {
        std::lock_guard<std::mutex> lock(map_mutex_); // Mutex ile haritaya erişimi kilitle

        auto it = client_map_.find(id);
        if (it != client_map_.end()) {
            std::cout << "İstemci kaydı siliniyor. ID: " << id << ", Soket: " << it->second.socket_fd << std::endl;
            client_map_.erase(it);
        } else {
            std::cout << "Silinecek istemci bulunamadı. ID: " << id << std::endl;
        }
    }

    // Verilen ID'ye karşılık gelen bağlantı bilgisini döndürür
    // Bulamazsa boş bir optional döner
    std::optional<ConnectionInfo> getClientInfo(const std::string& id) {
         std::lock_guard<std::mutex> lock(map_mutex_); // Okuma için de kilitleme (basitlik için)

         auto it = client_map_.find(id);
         if (it != client_map_.end()) {
             return it->second; // Bulunan bilgiyi döndür
         } else {
             return std::nullopt; // Bulunamadı, boş optional döndür
         }
    }

    // Mevcut kayıtlı istemci sayısını döndürür
    size_t getClientCount() const {
       // std::lock_guard<std::mutex> lock(map_mutex_); // const methodlarda mutex kullanımı dikkat gerektirir
       // Sadece sayıyı okumak genellikle daha az kritiktir ama tam güvenlik için lock gerekebilir.
       // Şimdilik kilitsiz bırakalım.
       return client_map_.size();
    }


private:
    std::unordered_map<std::string, ConnectionInfo> client_map_; // ID -> Bağlantı Bilgisi
    std::mt19937 gen_; // Rastgele sayı üreteci (Mersenne Twister)
    std::uniform_int_distribution<> dist_; // Dağılım (6 haneli sayılar için)
    std::mutex map_mutex_; // Haritaya erişimi korumak için mutex
};

// --- Örnek Kullanım ---
int main() {
    IDManager id_manager;

    // Örnek bağlantı bilgileri oluşturalım
    ConnectionInfo client1_info = {1001, "192.168.1.10"};
    ConnectionInfo client2_info = {1002, "10.0.0.5"};
    ConnectionInfo client3_info = {1003, "88.54.12.91"};

    // İstemcileri kaydedelim
    auto id1_opt = id_manager.registerClient(client1_info);
    auto id2_opt = id_manager.registerClient(client2_info);
    auto id3_opt = id_manager.registerClient(client3_info);

    std::string id1 = id1_opt.value_or("HATA"); // Optional'dan değeri al (veya hata stringi)
    std::string id2 = id2_opt.value_or("HATA");
    std::string id3 = id3_opt.value_or("HATA");


    std::cout << "\nMevcut istemci sayısı: " << id_manager.getClientCount() << std::endl;

    // Bir istemci bilgisini sorgulayalım
    if (id2 != "HATA") {
        auto info_opt = id_manager.getClientInfo(id2);
        if (info_opt) { // Eğer değer varsa (boş değilse)
            std::cout << "ID " << id2 << " için bilgiler: Soket=" << info_opt->socket_fd
                      << ", IP=" << info_opt->ip_address << std::endl;
        } else {
            std::cout << "ID " << id2 << " bulunamadı." << std::endl;
        }
    }


    // Bir istemcinin kaydını silelim
    if (id1 != "HATA") {
       id_manager.unregisterClient(id1);
    }


     std::cout << "Bir istemci silindikten sonra mevcut istemci sayısı: " << id_manager.getClientCount() << std::endl;

    // Silinen istemciyi tekrar sorgulayalım
    if (id1 != "HATA") {
        auto info_opt = id_manager.getClientInfo(id1);
         if (info_opt) {
             std::cout << "ID " << id1 << " için bilgiler: Soket=" << info_opt->socket_fd << std::endl;
         } else {
             std::cout << "ID " << id1 << " artık bulunamıyor (silindi)." << std::endl;
         }
    }


    return 0;
}
