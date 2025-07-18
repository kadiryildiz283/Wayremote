/**
 * istemci_goruntuleyici.cpp (NİHAİ ZAFER SÜRÜMÜ - Tek Thread, Bloke Etmeyen Soket)
 * DERLEME: g++ -std=c++17 -o goruntuleyici istemci_goruntuleyici.cpp -pthread -lSDL2 -lSDL2_image
 */
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h> // fcntl (soketi bloke etmeyen moda almak için)
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Kullanım: " << argv[0] << " <paylasan_ip> <paylasan_port>" << std::endl; return 1;
    }

    int host_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in host_addr;
    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(std::stoi(argv[2]));
    inet_pton(AF_INET, argv[1], &host_addr.sin_addr);

    if (connect(host_socket, (struct sockaddr *)&host_addr, sizeof(host_addr)) < 0) { 
        perror("Bağlantı hatası"); return 1; 
    }
    
    // --- EN ÖNEMLİ ADIM: SOKETİ BLOKE ETMEYEN MODA ALIYORUZ ---
    fcntl(host_socket, F_SETFL, O_NONBLOCK);

    std::cout << "[Bilgi] Bağlanıldı. Video akışı bekleniyor..." << std::endl;

    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);
    
    SDL_Window* window = SDL_CreateWindow("Wayremote Görüntüleyici", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 400, 240, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = nullptr;
    
    std::vector<uint8_t> network_buffer;
    bool quit = false;
    SDL_Event event;

    // --- TEK ANA DÖNGÜ ---
    while (!quit) {
        // --- Ağ Kısmı: Veri Varsa Oku ---
        char temp_buffer[4096];
        ssize_t bytes_read = read(host_socket, temp_buffer, sizeof(temp_buffer));
        if (bytes_read > 0) {
            network_buffer.insert(network_buffer.end(), temp_buffer, temp_buffer + bytes_read);
        }

        // --- Paket İşleme Kısmı: Tam bir kare var mı diye kontrol et ---
        while (network_buffer.size() >= sizeof(uint32_t)) {
            uint32_t net_size;
            memcpy(&net_size, network_buffer.data(), sizeof(uint32_t));
            uint32_t frame_size = ntohl(net_size);

            if (network_buffer.size() >= sizeof(uint32_t) + frame_size) {
                // Tam bir paketimiz var!
                std::vector<uint8_t> png_data(network_buffer.begin() + sizeof(uint32_t), network_buffer.begin() + sizeof(uint32_t) + frame_size);
                
                // İşlediğimiz paketi buffer'dan sil
                network_buffer.erase(network_buffer.begin(), network_buffer.begin() + sizeof(uint32_t) + frame_size);

                SDL_RWops* rw = SDL_RWFromConstMem(png_data.data(), png_data.size());
                SDL_Surface* surface = IMG_Load_RW(rw, 1);
                if (surface) {
                    if(texture) SDL_DestroyTexture(texture);
                    texture = SDL_CreateTextureFromSurface(renderer, surface);
                    SDL_FreeSurface(surface);
                    
                    // Pencere boyutunu gelen resme göre ayarla
                   
                }
            } else {
                break; // Henüz paketin tamamı gelmemiş
            }
        }

        // --- SDL Olay Kısmı ---
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit = true;
            else if (event.type == SDL_MOUSEMOTION) {
                std::string cmd = "MOVE " + std::to_string(event.motion.x) + " " + std::to_string(event.motion.y) + "\n";
                send(host_socket, cmd.c_str(), cmd.length(), 0);
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if(event.button.button == SDL_BUTTON_LEFT) {
                    send(host_socket, "LCLICK\n", 7, 0);
                }
            }
        }
        
        // --- Çizim Kısmı (Her zaman çalışır) ---
        SDL_SetRenderDrawColor(renderer, 20, 20, 40, 255);
        SDL_RenderClear(renderer);
        if (texture) {
            SDL_RenderCopy(renderer, texture, NULL, NULL);
        }
        SDL_RenderPresent(renderer);
    }
    
    // Temizlik
    if(texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    close(host_socket);
    
    std::cout << "[Görüntüleyici] Oturum sonlandı." << std::endl;
    return 0;
}
