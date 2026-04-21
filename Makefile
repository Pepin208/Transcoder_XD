# Makefile Universal para H265 Transcoder
# Compatible con Windows (CMD, PowerShell, MSYS2, w64devkit), Linux y macOS

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
INCLUDES = -I./include

# Deteccion de Sistema Operativo
ifeq ($(OS),Windows_NT)
    EXE        = .exe
    LDFLAGS    = -static -static-libgcc -static-libstdc++
    IS_WINDOWS = 1
    # Herramienta de descarga: curl esta disponible en W10+ por defecto
    DOWNLOADER = curl -L
    # PowerShell disponible en W10+ sin dependencias externas
    PS         = powershell -NoProfile -ExecutionPolicy Bypass -Command
else
    EXE        =
    LDFLAGS    =
    IS_WINDOWS = 0
    DOWNLOADER = curl -L
endif

TARGET = h265_transcoder$(EXE)
SRC    = main.cpp
OBJ    = main.o

# Rutas
BIN_DIR     = ./bin
FFMPEG_EXE  = $(BIN_DIR)/ffmpeg$(EXE)
FFPROBE_EXE = $(BIN_DIR)/ffprobe$(EXE)

JSON_DIR = ./include/nlohmann
JSON_HPP = $(JSON_DIR)/json.hpp
JSON_URL = https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

# FFmpeg Windows — BtbN GPL build
FFMPEG_ZIP = ffmpeg_temp.zip

.PHONY: all clean fetch_deps fetch_ffmpeg update_ffmpeg distclean help

# ─── Regla principal ────────────────────────────────────────────────────────
all: fetch_deps fetch_ffmpeg $(TARGET)
	@echo "Limpiando archivos objeto temporales..."
ifeq ($(IS_WINDOWS),1)
	@$(PS) "Remove-Item -Force -ErrorAction SilentlyContinue '$(OBJ)'"
else
	@rm -f $(OBJ)
endif
	@echo "Compilacion finalizada con exito."

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ─── Dependencia JSON ────────────────────────────────────────────────────────
fetch_deps: $(JSON_HPP)

$(JSON_HPP):
	@echo "Verificando dependencia nlohmann/json..."
ifeq ($(IS_WINDOWS),1)
	@$(PS) "New-Item -ItemType Directory -Force -Path '$(JSON_DIR)' | Out-Null"
	@$(PS) "\
		if (-not (Test-Path '$(JSON_HPP)')) { \
			Write-Host 'Descargando nlohmann/json.hpp...'; \
			Invoke-WebRequest -Uri '$(JSON_URL)' -OutFile '$(JSON_HPP)'; \
		} else { \
			Write-Host 'nlohmann/json.hpp ya existe, omitiendo descarga.'; \
		}"
else
	@mkdir -p $(JSON_DIR)
	@if [ ! -f "$(JSON_HPP)" ]; then \
		echo "Descargando nlohmann/json.hpp..."; \
		$(DOWNLOADER) $(JSON_URL) -o $(JSON_HPP); \
	else \
		echo "nlohmann/json.hpp ya existe, omitiendo descarga."; \
	fi
endif

# ─── FFmpeg ──────────────────────────────────────────────────────────────────
fetch_ffmpeg:
ifeq ($(IS_WINDOWS),1)
	@$(PS) "\
		\$$binOk = (Test-Path '$(FFMPEG_EXE)') -and (Test-Path '$(FFPROBE_EXE)'); \
		if (\$$binOk) { \
			Write-Host 'Binarios FFmpeg ya presentes en $(BIN_DIR), omitiendo descarga.'; \
		} else { \
			Write-Host 'Binarios FFmpeg no encontrados en $(BIN_DIR), descargando...'; \
			exit 1; \
		}" || $(MAKE) update_ffmpeg
else
	@if command -v ffmpeg >/dev/null 2>&1; then \
		echo "FFmpeg detectado en el sistema (Linux/macOS)."; \
	else \
		echo "ERROR: FFmpeg no esta instalado."; \
		echo "  Debian/Ubuntu : sudo apt install ffmpeg"; \
		echo "  Arch          : sudo pacman -S ffmpeg"; \
		echo "  macOS         : brew install ffmpeg"; \
		exit 1; \
	fi
endif

# ─── Descarga FFmpeg (solo Windows) ─────────────────────────────────────────
FFMPEG_URL = https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip

update_ffmpeg:
	@echo "Descargando FFmpeg para Windows..."
ifeq ($(IS_WINDOWS),1)
	@$(PS) "New-Item -ItemType Directory -Force -Path '$(BIN_DIR)' | Out-Null"
	curl -L --progress-bar "$(FFMPEG_URL)" -o "$(FFMPEG_ZIP)"
	@echo "Extrayendo ffmpeg.exe y ffprobe.exe..."
	@$(PS) "\
		Add-Type -AssemblyName System.IO.Compression.FileSystem; \
		\$$zip = [IO.Compression.ZipFile]::OpenRead('$(FFMPEG_ZIP)'); \
		\$$targets = @('ffmpeg.exe','ffprobe.exe'); \
		foreach (\$$entry in \$$zip.Entries) { \
			if (\$$targets -contains \$$entry.Name) { \
				\$$dst = Join-Path '$(BIN_DIR)' \$$entry.Name; \
				[IO.Compression.ZipFileExtensions]::ExtractToFile(\$$entry, \$$dst, \$$true); \
				Write-Host \"Extraido: \$$(\$$entry.Name)\"; \
			} \
		} \
		\$$zip.Dispose()"
	@$(PS) "Remove-Item -Force -ErrorAction SilentlyContinue '$(FFMPEG_ZIP)'"
	@echo "FFmpeg listo en $(BIN_DIR)."
else
	@echo "update_ffmpeg es solo para Windows."
endif

# ─── Limpieza ────────────────────────────────────────────────────────────────
clean:
	@echo "Limpiando binario y objetos..."
ifeq ($(IS_WINDOWS),1)
	@$(PS) "Remove-Item -Force -ErrorAction SilentlyContinue '$(OBJ)','$(TARGET)'"
else
	@rm -f $(OBJ) $(TARGET)
endif
	@echo "Limpieza completada."

distclean: clean
	@echo "Limpieza profunda (include/ y bin/)..."
ifeq ($(IS_WINDOWS),1)
	@$(PS) "Remove-Item -Recurse -Force -ErrorAction SilentlyContinue 'include','bin'"
else
	@rm -rf include bin
endif
	@echo "Limpieza profunda completada."

# ─── Ayuda ───────────────────────────────────────────────────────────────────
help:
	@echo "Targets disponibles:"
	@echo "  all          Descarga dependencias y compila (default)"
	@echo "  clean        Elimina binario y objetos"
	@echo "  distclean    Elimina todo incluyendo include/ y bin/"
	@echo "  update_ffmpeg  Descarga FFmpeg para Windows (solo Windows)"
	@echo "  help         Muestra esta ayuda"
