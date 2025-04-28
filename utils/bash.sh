
#!/bin/bash

# Kullanım: ./cpp_init.sh [proje_adi]
# Eğer proje_adi verilmezse 'my_cpp_project' kullanılır.

# Proje adını al (varsayılan: my_cpp_project)
PROJECT_NAME=${1:-my_cpp_project}

# Hedef dizin zaten varsa hata ver ve çık
if [ -d "$PROJECT_NAME" ]; then
  echo "Hata: '$PROJECT_NAME' adında bir dizin zaten mevcut."
  exit 1
fi

# Ana proje dizinini oluştur
mkdir "$PROJECT_NAME"
cd "$PROJECT_NAME" || exit 1 # Dizin değiştirilemezse çık

# Gerekli alt dizinleri oluştur
mkdir src include obj

# Temel main.cpp dosyasını oluştur
cat << EOF > src/main.cpp
// Otomatik olarak oluşturuldu
#include <iostream>

int main() {
    std::cout << "Merhaba, C++ Projesi!" << std::endl;
    return 0;
}
EOF

# Temel Makefile dosyasını oluştur
cat << EOF > Makefile
# Kullanılacak C++ derleyicisi
CXX = g++

# Derleyici bayrakları:
# -std=c++11 : C++11 standardını kullan
# -Iinclude  : Başlık dosyaları için 'include' klasörünü ekle
# -Wall      : Tüm uyarıları göster
# -g         : Hata ayıklama bilgilerini ekle (isteğe bağlı)
CXXFLAGS = -std=c++11 -Iinclude -Wall -g

# Çalıştırılabilir dosyanın adı
TARGET = program

# Kaynak, başlık ve nesne dosyası dizinleri
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Kaynak dosyaları otomatik bul (*.cpp)
SOURCES := \$(wildcard \$(SRCDIR)/*.cpp)

# Nesne dosyalarını oluştur (obj/ dosyasında)
OBJECTS := \$(patsubst \$(SRCDIR)/%.cpp,\$(OBJDIR)/%.o,\$(SOURCES))

# Varsayılan hedef: Çalıştırılabilir dosyayı oluştur
all: \$(TARGET)

# Çalıştırılabilir dosyayı oluşturma kuralı
# Nesne dosyalarını birbirine bağlar
\$(TARGET): \$(OBJECTS)
	@echo "Linking..."
	@mkdir -p \$(dir \$@) # Eğer ana dizinde oluşturuluyorsa gerek yok ama genelde bin/ dizini için kullanılır.
	\$(CXX) \$(CXXFLAGS) \$^ -o \$@
	@echo "Build finished: \$(TARGET)"

# Nesne dosyalarını derleme kuralı (*.cpp -> obj/*.o)
\$(OBJDIR)/%.o: \$(SRCDIR)/%.cpp
	@mkdir -p \$(dir \$@) # Nesne dosyası için obj dizinini oluştur
	@echo "Compiling \$<..."
	\$(CXX) \$(CXXFLAGS) -c \$< -o \$@

# Temizleme kuralı: Oluşturulan dosyaları siler
clean:
	@echo "Cleaning up..."
	rm -rf \$(OBJDIR) \$(TARGET)
	@echo "Cleanup complete."

# Dosya olmayan hedefleri belirtir
.PHONY: all clean

EOF

echo "'$PROJECT_NAME' projesi başarıyla oluşturuldu."
echo "İçerik:"
ls -R .

# Kullanıcıya sonraki adımları hatırlat
echo ""
echo "Projeyi derlemek için '$PROJECT_NAME' dizinine gidin ve 'make' komutunu çalıştırın."
echo "Programı çalıştırmak için './program' yazın."
echo "Temizlemek için 'make clean' komutunu kullanın."

exit 0
