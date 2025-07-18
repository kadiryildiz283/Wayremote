# Derleyici ve bayraklar
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g

# Hedef program isimleri
PAYLASAN_EXEC = paylasan
GORUNTULEYICI_EXEC = goruntuleyici

# Kütüphane bayrakları
LDFLAGS_PAYLASAN = -pthread
LDFLAGS_GORUNTULEYICI = -pthread -lSDL2 -lSDL2_image

# Kaynak dosyalar
PAYLASAN_SRC = src/istemci_paylasan.cpp
GORUNTULEYICI_SRC = src/istemci_goruntuleyici.cpp

all: $(PAYLASAN_EXEC) $(GORUNTULEYICI_EXEC)

$(PAYLASAN_EXEC): $(PAYLASAN_SRC)
	$(CXX) $(CXXFLAGS) -o $(PAYLASAN_EXEC) $(PAYLASAN_SRC) $(LDFLAGS_PAYLASAN)
	@echo "Build finished: $(PAYLASAN_EXEC)"

$(GORUNTULEYICI_EXEC): $(GORUNTULEYICI_SRC)
	$(CXX) $(CXXFLAGS) -o $(GORUNTULEYICI_EXEC) $(GORUNTULEYICI_SRC) $(LDFLAGS_GORUNTULEYICI)
	@echo "Build finished: $(GORUNTULEYICI_EXEC)"

clean:
	rm -f $(PAYLASAN_EXEC) $(GORUNTULEYICI_EXEC)

.PHONY: all clean
