# Kullanılacak C++ derleyicisi
CXX = g++

# Derleyici bayrakları:
# -std=c++17 : C++17 standardını kullan
# -Iinclude  : Başlık dosyaları için 'include' klasörünü ekle
# -Wall      : Tüm uyarıları göster
# -g         : Hata ayıklama bilgilerini ekle (isteğe bağlı)
CXXFLAGS = -std=c++17 -Iinclude -Wall -g

# Linker bayrakları (Kütüphaneler buraya eklenir)
LDFLAGS = -pthread -lvncclient -lSDL2 # <--- DEĞİŞİKLİK BURADA

# Çalıştırılabilir dosyanın adı
TARGET = program

# Kaynak, başlık ve nesne dosyası dizinleri
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Kaynak dosyaları otomatik bul (*.cpp)
SOURCES := $(wildcard $(SRCDIR)/*.cpp)

# Nesne dosyalarını oluştur (obj/ dosyasında)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# Varsayılan hedef: Çalıştırılabilir dosyayı oluştur
all: $(TARGET)

# Çalıştırılabilir dosyayı oluşturma kuralı
# Nesne dosyalarını birbirine bağlar
$(TARGET): $(OBJECTS)
	@echo "Linking..."
	# @mkdir -p $(dir $@) # 'program' ana dizinde olacağı için bu satıra gerek yok.
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) # <--- DEĞİŞİKLİK BURADA (LDFLAGS eklendi)
	@echo "Build finished: $(TARGET)"

# Nesne dosyalarını derleme kuralı (*.cpp -> obj/*.o)
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@) # Nesne dosyası için obj dizinini oluştur
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Temizleme kuralı: Oluşturulan dosyaları siler
clean:
	@echo "Cleaning up..."
	rm -rf $(OBJDIR) $(TARGET)
	@echo "Cleanup complete."

# Dosya olmayan hedefleri belirtir
.PHONY: all clean
