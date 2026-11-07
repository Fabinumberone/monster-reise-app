# PC-Werkzeug. Braucht einen Host-g++ (z. B. MinGW/WinLibs).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
SRC      := main.cpp $(wildcard ../core/*.cpp)
OUT      := ../build/pw3d-reise.exe

$(OUT): $(SRC) $(wildcard ../core/*.hpp)
	@mkdir -p ../build
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -static

# Nur Syntaxprüfung mit dem 3DS-Compiler (kein Host-Compiler nötig).
check:
	/c/devkitPro/devkitARM/bin/arm-none-eabi-g++ -std=c++17 -Wall -Wextra -fsyntax-only $(SRC)

.PHONY: check
