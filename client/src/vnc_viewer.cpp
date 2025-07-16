#include <SDL2/SDL.h>
#include <rfb/rfbclient.h> // LibVNCClient başlık dosyası (gerçek yolu kontrol edin)
#include <string>
#include <thread>
#include <atomic>
#include <iostream>
#include <vector> // Piksel verilerini yönetmek için gerekebilir

static int g_last_width = 0;
static int g_last_height = 0;

// Framebuffer güncelleme callback'i için global veya sınıf üyesi bazı değişkenler gerekebilir.
// Örneğin, yeni bir frame geldiğini işaret eden bir flag ve mutex'ler.
// Bu kısım dikkatli tasarlanmalı. Şimdilik basitleştirilmiş bir yapı sunuyorum.
// Global değişkenler yerine, bu bilgileri callback'e iletmek için rfbClient->clientData kullanılabilir.
SDL_Texture* g_vnc_texture = nullptr;
SDL_Renderer* g_vnc_renderer = nullptr; // Renderer'ı callback'e erişilebilir kılmak gerekebilir
std::atomic<bool> g_new_frame_ready = false;
// Mutex'ler de gerekebilir eğer framebuffer'a birden fazla thread erişiyorsa

// LibVNCClient için Framebuffer Güncelleme Callback'i
// BU FONKSİYON DİKKATLİCE TASARLANMALIDIR (THREAD SAFETY vb.)
static void handle_framebuffer_update(rfbClient* client) {
    if (!client || !client->frameBuffer) return;

    // İlk frame ise veya boyut değiştiyse texture'ı oluştur/yeniden oluştur
if (g_vnc_texture == nullptr || client->width != g_last_width || client->height != g_last_height) {
        
        // Boyut değiştiyse, yeni boyutları sakla
        g_last_width = client->width;
        g_last_height = client->height;
        
        if (g_vnc_texture) {
            SDL_DestroyTexture(g_vnc_texture);
        }
        g_vnc_texture = SDL_CreateTexture(g_vnc_renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, client->width, client->height);
        if (!g_vnc_texture) {
            // Hata yönetimi...
            return;
        }
    }    // Framebuffer'ı texture'a kopyala
    // Bu kısım client->format.trueColour ve client->format.bitsPerPixel gibi değerlere göre değişir.
    // Örnek: Eğer VNC formatı ve texture formatı doğrudan uyumluysa:
    SDL_UpdateTexture(g_vnc_texture, nullptr, client->frameBuffer, client->width * (client->format.bitsPerPixel / 8));

    g_new_frame_ready = true; // Ana döngüye yeni frame olduğunu bildir
}

// LibVNCClient için piksel formatını ayarlama (isteğe bağlı ama genellikle iyi fikir)
static rfbBool set_pixel_format(rfbClient* client) {
    client->format.bitsPerPixel = 32;
    client->format.depth = 24;
    client->format.trueColour = TRUE;
    client->format.redMax = 255; client->format.greenMax = 255; client->format.blueMax = 255;
    client->format.redShift = 16; client->format.greenShift = 8; client->format.blueShift = 0;
  return true;
}


void launch_vnc_viewer_window(const std::string& vnc_server_host, int vnc_server_port, std::atomic<bool>& session_is_active) {
    SDL_Window* window = nullptr;
    // Renderer ve Texture bu fonksiyona özel olmalı, global değil.
    // Ancak callback'lerin bunlara erişimi için bir yol bulunmalı (client->clientData ile taşınabilir).
    // Şimdilik global g_vnc_renderer ve g_vnc_texture kullanılıyor ama bu ideal değil.
    SDL_Renderer* local_renderer = nullptr; // Bu renderer global g_vnc_renderer'a atanacak.
    SDL_Texture* local_texture = nullptr;  // Bu texture global g_vnc_texture'a atanacak.


    char window_title[100];
    snprintf(window_title, sizeof(window_title), "VNC Viewer - %s:%d", vnc_server_host.c_str(), vnc_server_port);

    window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return;
    }

    local_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!local_renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        return;
    }
    g_vnc_renderer = local_renderer; // Geçici çözüm, callback'in erişimi için.

    rfbClient* rfb_client = rfbGetClient(8, 3, 4); // Default VNC pixel format (8bpp, 3 bytes, 4 bytes per pixel)
    if (!rfb_client) {
        std::cerr << "rfbGetClient failed" << std::endl;
        SDL_DestroyRenderer(local_renderer);
        SDL_DestroyWindow(window);
        return;
    }

    // Client verisi olarak renderer ve texture adreslerini saklayabiliriz.
    // struct MyClientData { SDL_Renderer* ren; SDL_Texture** tex_ptr; ... };
    // MyClientData cdata = {local_renderer, &local_texture, ...};
    // rfb_client->clientData = &cdata; // Callback'ler buradan erişir.

    // Callback'leri ayarla
    // rfb_client->GotXCutText = handle_clipboard_update; // İsteğe bağlı
    // rfb_client->GetPassword = get_vnc_password_from_user; // Şifre gerekiyorsa
    // Kendi pixel formatımızı istemek genellikle daha kontrollü olur.
    // rfb_client->SetPreferredPixelFormat = set_pixel_format;


    std::cout << "Connecting to VNC server " << vnc_server_host << ":" << vnc_server_port << std::endl;
    if (!ConnectToRFBServer(rfb_client, vnc_server_host.c_str(), vnc_server_port)) {
        rfbClientLog("Error: Failed to connect to VNC server %s:%d\n", vnc_server_host.c_str(), vnc_server_port);
        rfbClientCleanup(rfb_client); // rfbClientCleanup yerine rfbClientPtr rfbCleanUp(rfbClientPtr client) olmalı
        SDL_DestroyRenderer(local_renderer);
        SDL_DestroyWindow(window);
        return;
    }
    std::cout << "Connected to VNC server. Desktop name: " << rfb_client->desktopName << std::endl;
    std::cout << "Dimensions: " << rfb_client->width << "x" << rfb_client->height << std::endl;

    // İstenen pixel formatını ayarla (bağlantıdan sonra)
    if (!set_pixel_format(rfb_client)) {
        rfbClientLog("Failed to set desired pixel format.\n");
        // Bağlantıyı sonlandır veya varsayılan formatla devam et.
    }


    bool viewer_running = true;
    SDL_Event event;

    // Texture'ı ilk boyutlara göre oluştur
    g_vnc_texture = SDL_CreateTexture(local_renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, rfb_client->width, rfb_client->height);
    local_texture = g_vnc_texture; // globalden locale eşitleme (kötü pratik)

    if (!local_texture) {
         std::cerr << "Initial SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
         viewer_running = false; // veya hata yönetimi
    }


    while (viewer_running && session_is_active.load()) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                viewer_running = false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                 // Pencere yeniden boyutlandırıldığında VNC'ye yeni boyutu bildirebilir veya
                 // sadece çizim alanını ayarlayabilirsiniz. Tam VNC yeniden boyutlandırma karmaşıktır.
                 // Şimdilik sadece SDL renderer'ı güncelleyelim.
                 // SDL_RenderSetViewport(local_renderer, ...);
            }
            // TODO: Fare ve Klavye olaylarını yakalayıp SendPointerEvent ve SendKeyEvent ile VNC'ye gönder
            // if (event.type == SDL_MOUSEMOTION) { ... SendPointerEvent(rfb_client, event.motion.x, event.motion.y, event.motion.state); ... }
            // if (event.type == SDL_KEYDOWN) { ... SendKeyEvent(rfb_client, event.key.keysym.sym, TRUE); ...}
        }

        // VNC sunucusundan mesajları işle
        // WaitForMessage belirli bir süre bekler, 0 hemen döner (non-blocking).
        // Hata veya bağlantı kopması durumunda -1 dönebilir.
        int time_to_wait_ms = 10; // Çok kısa bir süre bekle
        if (WaitForMessage(rfb_client, time_to_wait_ms) < 0) {
            rfbClientLog("Connection lost or error in WaitForMessage.\n");
            viewer_running = false; // Bağlantı koptu, döngüden çık
            break;
        }
        // HandleRFBServerMessage(rfb_client) çağrısı WaitForMessage içinde veya sonrasında yapılır.
        // LibVNCClient genellikle bunu kendi içinde halleder ve callback'leri tetikler.

        if (g_new_frame_ready.load()) {
            SDL_RenderClear(local_renderer);
            if (local_texture) { // local_texture kullanmalı, g_vnc_texture değil
                SDL_RenderCopy(local_renderer, local_texture, nullptr, nullptr);
            }
            SDL_RenderPresent(local_renderer);
            g_new_frame_ready = false;
        }

        // SDL_Delay(16); // ~60 FPS, ama WaitForMessage zaten bekliyor olabilir.
    }

    std::cout << "Closing VNC viewer for " << vnc_server_host << ":" << vnc_server_port << std::endl;
    rfbClientCleanup(rfb_client); // rfbClientPtr rfbCleanUp(rfbClientPtr client)
    if (local_texture) SDL_DestroyTexture(local_texture);
    SDL_DestroyRenderer(local_renderer);
    SDL_DestroyWindow(window);
    g_vnc_renderer = nullptr; // globali temizle
    g_vnc_texture = nullptr;  // globali temizle
}
