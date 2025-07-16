#ifndef VNC_VIEWER_H
#define VNC_VIEWER_H

#include <string>
#include <atomic> // std::atomic için

// VNC viewer penceresini başlatan fonksiyonun bildirimi
void launch_vnc_viewer_window(
    const std::string& vnc_server_host,
    int vnc_server_port,
    std::atomic<bool>& session_is_active
);

// Belki ileride VNC viewer ayarları için bir struct eklenebilir:
// struct VncViewerSettings { ... };

#endif // VNC_VIEWER_H
