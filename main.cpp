#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <sstream>
#include <clocale>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// Estructuras de medios
struct StreamInfo {
    int index;
    std::string codec_type;
    std::string codec_name;
    std::string language;
    std::string title;
};

struct MediaInfo {
    int video_stream_index = -1;
    double framerate = 24.0;
    std::vector<StreamInfo> audio_streams;
    std::vector<StreamInfo> subtitle_streams;
};

// Configuración de usuario
struct UserConfig {
    std::string language = "";
    std::string encoder = "";
    int quality_q = -1;
    std::string opus_bitrate = "";
    std::string bin_path = "";
    bool enable_10bit = false;
    bool ffmpeg_ready = false; // Estado de verificación de binarios
    std::string hwaccel_device = ""; // Para Linux (/dev/dri/renderD*)
    std::string encoder_profile = ""; // Speed, Balanced, Quality
};

UserConfig g_config;

// Diccionario de traducciones
std::map<std::string, std::map<std::string, std::string>> i18n = {
    {"es", {
        {"lang_prompt", "¿Qué idioma prefieres? (1: Español, 2: Inglés): "},
        {"enc_prompt", "Selecciona el encoder preferido:\n"},
        {"q_prompt", "Introduce el nivel de calidad Q (Sugerido 16-25, menor es mejor calidad): "},
        {"bitrate_prompt", "Introduce el bitrate para Opus (ej. 96k, 128k, 160k, 192k): "},
        {"cfg_saved", "Configuración guardada en user.cfg."},
        {"welcome", "--- H265 Transcoder ---"},
        {"ffmpeg_not_found", "Advertencia: FFmpeg no encontrado en el PATH. Verifica tu instalación."},
        {"ffprobe_error", "Error ejecutando ffprobe. Verifica que esté en el PATH."},
        {"mode_prompt", "¿Qué deseas procesar? (1: Archivo individual, 2: Carpeta/Batch): "},
        {"file_prompt", "Ingresa la ruta del archivo de video: "},
        {"dir_prompt", "Ingresa la ruta de la carpeta: "},
        {"audio_prompt", "Introduce los índices de AUDIO a conservar (separados por espacio, Enter para ninguno): "},
        {"sub_prompt", "Introduce los índices de SUBTÍTULOS a conservar (separados por espacio, Enter para ninguno): "},
        {"another_prompt", "¿Deseas procesar otro archivo/carpeta? (s/n): "},
        {"start_encode", "Iniciando codificación..."},
        {"finished", "Proceso completado."},
        {"bin_prompt", "Arrastra la CARPETA que contiene ffmpeg.exe y ffprobe.exe (especialmente para entornos portables): "},
        {"bin_invalid", "La carpeta especificada no contiene ffmpeg.exe o ffprobe.exe. Inténtalo de nuevo."},
        {"10bit_prompt", "El hardware SÍ soporta 10 bits. ¿Deseas activar Calidad de 10 bits? (s/n): "},
        {"10bit_disabled", "El hardware NO soporta 10 bits. Se procesará en 8 bits."}
    }},
    {"en", {
        {"lang_prompt", "Which language do you prefer? (1: Spanish, 2: English): "},
        {"enc_prompt", "Select preferred encoder:\n"},
        {"q_prompt", "Enter quality level Q (Suggested 16-25, lower is better quality): "},
        {"bitrate_prompt", "Enter Opus bitrate (e.g., 96k, 128k, 160k, 192k): "},
        {"cfg_saved", "Configuration saved to user.cfg."},
        {"welcome", "--- H265 Transcoder ---"},
        {"ffmpeg_not_found", "Warning: FFmpeg not found in PATH. Check your installation."},
        {"ffprobe_error", "Error executing ffprobe. Make sure it is in your PATH."},
        {"mode_prompt", "What do you want to process? (1: Single file, 2: Folder/Batch): "},
        {"file_prompt", "Enter the path to the video file: "},
        {"dir_prompt", "Enter the folder path: "},
        {"audio_prompt", "Enter AUDIO indices to keep (space separated, Enter for none): "},
        {"sub_prompt", "Enter SUBTITLE indices to keep (space separated, Enter for none): "},
        {"another_prompt", "Do you want to process another file/folder? (y/n): "},
        {"start_encode", "Starting encoding..."},
        {"finished", "Process completed."},
        {"bin_prompt", "Drag and drop the FOLDER containing ffmpeg.exe and ffprobe.exe: "},
        {"bin_invalid", "The specified folder does not contain ffmpeg.exe or ffprobe.exe. Please try again."},
        {"10bit_prompt", "Hardware supports 10-bit. Do you want to enable 10-bit quality? (y/n): "},
        {"10bit_disabled", "Hardware does NOT support 10-bit. Processing in 8-bit."}
    }}
};

std::string t(const std::string& key) {
    if (g_config.language.empty()) return i18n["en"][key];
    return i18n[g_config.language][key];
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void saveConfig() {
    std::ofstream file("user.cfg");
    if (file.is_open()) {
        file << "# H265 Transcoder Configuration\n";
        file << "LANGUAGE=" << g_config.language << "\n";
        file << "ENCODER=" << g_config.encoder << "\n";
        file << "QUALITY_Q=" << g_config.quality_q << "\n";
        file << "OPUS_BITRATE=" << g_config.opus_bitrate << "\n";
        if (!g_config.bin_path.empty()) file << "BIN_PATH=" << g_config.bin_path << "\n";
        file << "ENABLE_10BIT=" << (g_config.enable_10bit ? "1" : "0") << "\n";
        file << "FFMPEG_READY=" << (g_config.ffmpeg_ready ? "1" : "0") << "\n";
        if (!g_config.hwaccel_device.empty()) file << "HWACCEL_DEVICE=" << g_config.hwaccel_device << "\n";
        if (!g_config.encoder_profile.empty()) file << "ENCODER_PROFILE=" << g_config.encoder_profile << "\n";
        file.close();
    }
}

bool loadConfig() {
    std::ifstream file("user.cfg");
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t delimiterPos = line.find('=');
        if (delimiterPos == std::string::npos) continue;

        std::string key   = trim(line.substr(0, delimiterPos));
        std::string value = trim(line.substr(delimiterPos + 1));
        if (value.empty()) continue;

        if (key == "LANGUAGE") {
            g_config.language = value;

        } else if (key == "ENCODER") {
            g_config.encoder = value;

        } else if (key == "QUALITY_Q") {
            try {
                int q = std::stoi(value);
                #ifdef __APPLE__
                if (q >= 1 && q <= 100) g_config.quality_q = q;
                else {
                    std::cerr << "[WARN] QUALITY_Q fuera de rango (1-100), ignorado: " << value << "\n";
                    g_config.quality_q = -1;
                }
                #else
                if (q >= 0 && q <= 51) g_config.quality_q = q;
                else {
                    std::cerr << "[WARN] QUALITY_Q fuera de rango (0-51), ignorado: " << value << "\n";
                    g_config.quality_q = -1;
                }
                #endif
            } catch (...) {
                std::cerr << "[WARN] QUALITY_Q inválido en user.cfg: " << value << "\n";
                g_config.quality_q = -1;
            }

        } else if (key == "OPUS_BITRATE") {
            std::string digits;
            for (char c : value)
                if (std::isdigit(c)) digits += c;
                if (!digits.empty()) g_config.opus_bitrate = digits;
                else std::cerr << "[WARN] OPUS_BITRATE inválido en user.cfg: " << value << "\n";

        } else if (key == "BIN_PATH") {
            g_config.bin_path = value;

        } else if (key == "ENABLE_10BIT") {
            g_config.enable_10bit = (value == "1" || value == "true");

        } else if (key == "FFMPEG_READY") {
            g_config.ffmpeg_ready = (value == "1" || value == "true");

        } else if (key == "HWACCEL_DEVICE") {
            g_config.hwaccel_device = value;

        } else if (key == "ENCODER_PROFILE") {
            g_config.encoder_profile = value;
        }
    }
    file.close();

    // Validación final: config usable solo si tiene los campos mínimos
    bool valid = !g_config.language.empty()
    && !g_config.encoder.empty()
    && g_config.quality_q != -1
    && !g_config.opus_bitrate.empty();

    if (!valid) {
        std::cerr << "[WARN] user.cfg incompleto o corrupto. Se ejecutará el asistente de configuración.\n";
    }

    return valid;
}

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

std::atomic<FILE*> g_current_pipe{nullptr};
std::atomic<bool>  g_interrupted{false};

void signalHandler(int signum) {
    g_interrupted = true;

    FILE* pipe = g_current_pipe.exchange(nullptr);
    if (pipe) {
        #ifdef _WIN32
        system("taskkill /IM ffmpeg.exe /F /T >nul 2>&1");
        #else
        system("pkill -f ffmpeg >/dev/null 2>&1");
        #endif
        PCLOSE(pipe);
    }

    // Salida limpia sin más output
    std::cout << "\n\n[!] Interrumpido por el usuario.\n" << std::flush;
    _exit(0); // _exit evita destructores/flush que generan output extra
}

std::string execCommand(const std::string& cmd) {
    std::string result;
    char buffer[128];
    std::string final_cmd = cmd;
    #ifdef _WIN32
    final_cmd = "\"" + cmd + "\"";
    #endif
    FILE* pipe = POPEN(final_cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) result += buffer;
    PCLOSE(pipe);
    return result;
}

std::string getBinPath(const std::string& bin_name) {
    #ifdef _WIN32
    if (!g_config.bin_path.empty()) {
        fs::path p = fs::path(g_config.bin_path) / (bin_name + ".exe");
        if (fs::exists(p)) return "\"" + p.string() + "\"";
    }
    #endif
    return bin_name;
}

void verifyBinaries() {
    #ifdef _WIN32
    // 1. Verificación rápida si el flag ya está activo
    if (g_config.ffmpeg_ready) {
        if (!g_config.bin_path.empty()) {
            if (fs::exists(fs::path(g_config.bin_path) / "ffmpeg.exe")) return;
        } else if (system("ffmpeg -version >nul 2>&1") == 0) {
            return;
        }
    }

    // 2. Prioridad: Carpeta local ./bin/ (Makefile)
    fs::path local_bin = fs::current_path() / "bin";
    if (fs::exists(local_bin / "ffmpeg.exe") && fs::exists(local_bin / "ffprobe.exe")) {
        g_config.bin_path = local_bin.string();
        g_config.ffmpeg_ready = true;
        saveConfig();
        return;
    }

    // 3. PATH del sistema
    if (system("ffmpeg -version >nul 2>&1") == 0) {
        g_config.ffmpeg_ready = true;
        saveConfig();
        return;
    }

    // 4. Prompt manual
    while (true) {
        std::cout << t("ffmpeg_not_found") << "\n";
        std::cout << t("bin_prompt");
        std::string input;
        std::getline(std::cin, input);
        input = trim(input);
        input.erase(std::remove(input.begin(), input.end(), '\"'), input.end());

        fs::path p(input);
        if (fs::exists(p) && fs::is_directory(p)) {
            if (fs::exists(p / "ffmpeg.exe") && fs::exists(p / "ffprobe.exe")) {
                g_config.bin_path = p.string();
                g_config.ffmpeg_ready = true;
                saveConfig();
                break;
            }
        }
        std::cout << t("bin_invalid") << "\n\n";
    }
    #endif
}

std::string formatSize(uintmax_t bytes) {
    double size = bytes;
    int i = 0;
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    while (size >= 1024 && i < 4) { size /= 1024; i++; }
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %s", size, units[i]);
    return std::string(buf);
}

void printSavings(uintmax_t original, uintmax_t encoded, int file_index = 0, int total_files = 0) {
    if (original == 0) return;

    auto toMB = [](uintmax_t bytes) {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    };

    double orig_mb    = toMB(original);
    double enc_mb     = toMB(encoded);
    double saved_mb   = orig_mb - enc_mb;
    double saved_pct  = (saved_mb / orig_mb) * 100.0;
    bool   grew       = encoded > original;

    std::cout << std::fixed << std::setprecision(2);

    if (file_index > 0 && total_files > 0)
        std::cout << "\n[" << file_index << "/" << total_files << "] ";
    else
        std::cout << "\n";

    std::cout << "─────────────────────────────────\n";
    std::cout << (g_config.language == "es" ? "  Original : " : "  Original : ")
    << std::setw(8) << orig_mb  << " MB\n";
    std::cout << (g_config.language == "es" ? "  Resultado: " : "  Result   : ")
    << std::setw(8) << enc_mb   << " MB\n";
    std::cout << "─────────────────────────────────\n";

    if (grew) {
        std::cout << (g_config.language == "es"
        ? "  [!] El archivo creció "
        : "  [!] File grew by ")
        << std::abs(saved_mb) << " MB ("
        << std::abs(saved_pct) << "%)\n";
    } else {
        std::cout << (g_config.language == "es" ? "  Ahorro   : " : "  Saved    : ")
        << std::setw(8) << saved_mb << " MB ("
        << saved_pct << "%)\n";
    }
    std::cout << "─────────────────────────────────\n";
}

struct HardwareNames {
    std::vector<std::string> gpu_names;
    std::string cpu_name;
};

HardwareNames getSystemHardwareNames() {
    HardwareNames hw;

    #ifdef _WIN32
    // Windows: PowerShell + CIM o wmic
    auto tryPS = [](const std::string& query) -> std::string {
        std::string cmd = "powershell -NoProfile -Command \"" + query + "\" 2>nul";
        FILE* p = POPEN(cmd.c_str(), "r");
        if (!p) return "";
        char buf[256]; std::string out;
        while (fgets(buf, sizeof(buf), p)) out += buf;
        PCLOSE(p);
        return trim(out);
    };

    std::string gpu_raw = tryPS(
        "Get-CimInstance Win32_VideoController | "
        "Select-Object -ExpandProperty Name | "
        "Where-Object { $_ -ne $null }"
    );

    if (gpu_raw.empty()) {
        FILE* p = POPEN("wmic path win32_VideoController get name /value 2>nul", "r");
        if (p) {
            char buf[256];
            while (fgets(buf, sizeof(buf), p)) {
                std::string line = trim(std::string(buf));
                if (line.rfind("Name=", 0) == 0) gpu_raw += line.substr(5) + "\n";
            }
            PCLOSE(p);
        }
    }

    std::stringstream ss(gpu_raw);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (!line.empty()) hw.gpu_names.push_back(line);
    }

    hw.cpu_name = tryPS("Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name -First 1");
    if (hw.cpu_name.empty()) {
        FILE* p = POPEN("wmic cpu get name /value 2>nul", "r");
        if (p) {
            char buf[256];
            while (fgets(buf, sizeof(buf), p)) {
                std::string l = trim(std::string(buf));
                if (l.rfind("Name=", 0) == 0) { hw.cpu_name = l.substr(5); break; }
            }
            PCLOSE(p);
        }
    }

    #elif __APPLE__
    // macOS: system_profiler y sysctl
    {
        FILE* p = POPEN("system_profiler SPDisplaysDataType 2>/dev/null | grep 'Chipset Model'", "r");
        if (p) {
            char buf[256];
            while (fgets(buf, sizeof(buf), p)) {
                std::string l = trim(std::string(buf));
                size_t pos = l.find(": ");
                if (pos != std::string::npos) hw.gpu_names.push_back(trim(l.substr(pos + 2)));
            }
            PCLOSE(p);
        }
    }
    {
        FILE* p = POPEN("sysctl -n machdep.cpu.brand_string 2>/dev/null", "r");
        if (p) {
            char buf[256];
            if (fgets(buf, sizeof(buf), p)) hw.cpu_name = trim(std::string(buf));
            PCLOSE(p);
        }

        // Si no hay GPU listada explícitamente pero es Apple Silicon, lo asignamos por defecto
        if (hw.gpu_names.empty() && hw.cpu_name.find("Apple") != std::string::npos) {
            hw.gpu_names.push_back("Apple Silicon");
        }
    }

    #else
    // Linux: Detección estricta por /proc y /sys sin dependencias externas
    {
        // Obtener CPU
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string cpu_line;
        while (std::getline(cpuinfo, cpu_line)) {
            if (cpu_line.rfind("model name", 0) == 0) {
                size_t colon = cpu_line.find(':');
                if (colon != std::string::npos) {
                    hw.cpu_name = trim(cpu_line.substr(colon + 1));
                    break;
                }
            }
        }

        // Obtener GPU(s) desde DRM
        std::string drm_path = "/sys/class/drm/";
        if (fs::exists(drm_path)) {
            for (const auto& entry : fs::directory_iterator(drm_path)) {
                std::string dir_name = entry.path().filename().string();
                // Buscar carpetas card0, card1, etc. Ignorar subcarpetas como card0-DP-1
                if (dir_name.rfind("card", 0) == 0 && dir_name.find('-') == std::string::npos) {
                    std::ifstream vendor_file(entry.path() / "device/vendor");
                    std::string vendor_id;
                    std::string gpu_name = "GPU_Desconocida";
                    if (std::getline(vendor_file, vendor_id)) {
                        vendor_id = trim(vendor_id);
                        if (vendor_id == "0x1002") gpu_name = "AMD";
                        else if (vendor_id == "0x8086") gpu_name = "Intel";
                        else if (vendor_id == "0x10de") gpu_name = "NVIDIA";
                        else gpu_name += "_" + vendor_id;
                    }
                    if (std::find(hw.gpu_names.begin(), hw.gpu_names.end(), gpu_name) == hw.gpu_names.end()) {
                        hw.gpu_names.push_back(gpu_name);
                    }
                }
            }
        }
    }
    #endif

    // Fallbacks si no se obtuvo nada
    if (hw.gpu_names.empty()) hw.gpu_names.push_back("GPU desconocida");
    if (hw.cpu_name.empty()) hw.cpu_name = "CPU desconocida";

    return hw;
}

struct EncoderOption {
    std::string id;      // "hevc_nvenc"
    std::string label;   // "H265 NVIDIA (RTX 3070)"
};

std::vector<EncoderOption> detectEncoders() {
    std::vector<EncoderOption> available;
    HardwareNames hw = getSystemHardwareNames();

    // Devuelve TODAS las GPUs que contienen el keyword (case-insensitive)
    auto findAllGpus = [&](const std::string& keyword) -> std::vector<std::string> {
        std::vector<std::string> matches;
        std::string kw = keyword;
        std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
        for (const auto& g : hw.gpu_names) {
            std::string gl = g;
            std::transform(gl.begin(), gl.end(), gl.begin(), ::tolower);
            if (gl.find(kw) != std::string::npos)
                matches.push_back(g);
        }
        return matches;
    };

    // Test encode real — 1 frame, silencioso
    auto testEncoder = [&](const std::string& enc, const std::string& hwaccel_dev = "") -> bool {
        std::string cmd = getBinPath("ffmpeg");

        if (!hwaccel_dev.empty()) {
            if (enc == "hevc_vaapi") cmd += " -vaapi_device " + hwaccel_dev;
        }

        // AMF requiere parámetros mínimos explícitos para inicializarse
        if (enc == "hevc_amf") {
            cmd += " -f lavfi -i nullsrc=s=256x256:r=24"
            " -vf format=yuv420p -frames:v 1"
            " -c:v hevc_amf -rc cqp -qp_i 20 -qp_p 20"
            " -f null -";
        } else if (enc == "hevc_vaapi") {
            cmd += " -f lavfi -i nullsrc=s=256x256:d=1 -frames:v 1"
            " -vf format=nv12,hwupload"
            " -c:v " + enc + " -f null -";
        } else {
            cmd += " -f lavfi -i nullsrc=s=256x256:d=1 -frames:v 1"
            " -c:v " + enc + " -f null -";
        }

        #ifdef _WIN32
        cmd += " 2>nul";
        #else
        cmd += " 2>/dev/null";
        #endif

        FILE* p = POPEN(cmd.c_str(), "r");
        if (!p) return false;
        char buf[256];
        while (fgets(buf, sizeof(buf), p)) {}
        return (PCLOSE(p) == 0);
    };

    struct EncoderDef {
        std::string id;           // id ffmpeg
        std::string gpu_keyword;  // keyword para buscar en gpu_names
        std::string label_prefix; // prefijo del label visible
        bool use_gpu_name;        // false = usar cpu_name (libx265)
    };

    std::vector<EncoderDef> candidates;

    #if defined(__APPLE__)
    candidates = {
        {"hevc_videotoolbox", "apple",   "H265 Apple",   true},
        {"hevc_videotoolbox", "intel",   "H265 Intel",   true},
        {"hevc_videotoolbox", "amd",     "H265 AMD",     true},
        {"libx265",           "",        "H265 CPU",     false},
    };
    #elif defined(__linux__)
    candidates = {
        {"hevc_vaapi",        "amd",     "H265 VAAPI AMD",     true},
        {"hevc_vaapi",        "intel",   "H265 VAAPI Intel",   true},
        {"hevc_nvenc",        "nvidia",  "H265 NVIDIA",        true},
        {"libx265",           "",        "H265 CPU",           false},
    };
    #else
    candidates = {
        {"hevc_nvenc",        "nvidia",  "H265 NVIDIA",  true},
        {"hevc_nvenc",        "geforce", "H265 NVIDIA",  true},
        {"hevc_amf",          "amd",     "H265 AMD",     true},
        {"hevc_amf",          "radeon",  "H265 AMD",     true},
        {"hevc_qsv",          "intel",   "H265 Intel",   true},
        {"libx265",           "",        "H265 CPU",     false},
    };
    #endif

    // encoder_id → ya fue agregado con ese label_prefix
    std::map<std::string, std::set<std::string>> added;

    for (const auto& cand : candidates) {

        if (!cand.use_gpu_name) {
            // libx265 — CPU
            if (added.count(cand.id)) continue;
            if (!testEncoder(cand.id)) continue;
            std::string label = cand.label_prefix + " (" + hw.cpu_name + ")";
            available.push_back({cand.id, label});
            added[cand.id];
            continue;
        }

        std::vector<std::string> gpus = findAllGpus(cand.gpu_keyword);
        if (gpus.empty()) continue;

        for (const std::string& gpu_name : gpus) {
            if (added[cand.id].count(gpu_name)) continue;

            bool success = false;
            std::string selected_dev = "";

            #ifdef __linux__
            if (cand.id == "hevc_vaapi") {
                // Iterar sobre /dev/dri/renderD*
                for (int i = 128; i < 140; ++i) {
                    std::string dev_path = "/dev/dri/renderD" + std::to_string(i);
                    if (fs::exists(dev_path)) {
                        if (testEncoder(cand.id, dev_path)) {
                            success = true;
                            selected_dev = dev_path;
                            break;
                        }
                    }
                }
            } else {
                success = testEncoder(cand.id);
            }
            #else
            success = testEncoder(cand.id);
            #endif

            if (!success) {
                std::cout << "  [!] " << gpu_name
                << " no soporta " << cand.id << ", descartado.\n";
                continue;
            }

            std::string label = cand.label_prefix + " (" + gpu_name + ")";
            if (!selected_dev.empty()) {
                label += " [" + selected_dev + "]";
            }
            available.push_back({cand.id, label});
            added[cand.id].insert(gpu_name);
        }
    }

    return available;
}

void verifyHardwareCapabilities() {
    if (g_config.encoder == "libx265") { g_config.enable_10bit = true; return; }

    bool supports_10bit = false;

    if (g_config.encoder == "hevc_vaapi") {
        std::string cmd = getBinPath("ffmpeg");
        if (!g_config.hwaccel_device.empty()) {
            cmd += " -vaapi_device " + g_config.hwaccel_device;
        }
        cmd += " -f lavfi -i nullsrc=s=256x256:d=1 -vf \"format=p010le,hwupload\" -c:v hevc_vaapi -frames:v 1 -f null -";
        #ifdef _WIN32
        cmd += " >nul 2>&1";
        #else
        cmd += " >/dev/null 2>&1";
        #endif
        supports_10bit = (system(cmd.c_str()) == 0);
    } else {
        std::string output = execCommand(getBinPath("ffmpeg") + " -h encoder=" + g_config.encoder + " 2>&1");
        supports_10bit = (output.find("p010le") != std::string::npos);
    }

    if (supports_10bit) {
        std::cout << "\n" << t("10bit_prompt");
        std::string input;
        std::getline(std::cin, input);
        input = trim(input);
        g_config.enable_10bit = (input == "s" || input == "y" || input == "S" || input == "Y");
    } else {
        std::cout << "\n" << t("10bit_disabled") << "\n";
        g_config.enable_10bit = false;
    }
    saveConfig();
}

std::string buildFfmpegCommand(
    const std::string& input_file,
    const std::string& output_file,
    const MediaInfo& info,
    const std::vector<int>& selected_audios,
    const std::vector<int>& selected_subs)
{
    // Validaciones previas
    if (g_config.encoder.empty()) {
        std::cerr << "[ERROR] buildFfmpegCommand: encoder no configurado.\n";
        return "";
    }
    #ifdef __APPLE__
    if (g_config.quality_q < 1 || g_config.quality_q > 100) {
        std::cerr << "[ERROR] buildFfmpegCommand: quality_q inválido (" << g_config.quality_q << ").\n";
        return "";
    }
    #else
    if (g_config.quality_q < 0 || g_config.quality_q > 51) {
        std::cerr << "[ERROR] buildFfmpegCommand: quality_q inválido (" << g_config.quality_q << ").\n";
        return "";
    }
    #endif
    if (g_config.opus_bitrate.empty()) {
        std::cerr << "[ERROR] buildFfmpegCommand: opus_bitrate no configurado.\n";
        return "";
    }
    if (info.video_stream_index < 0) {
        std::cerr << "[ERROR] buildFfmpegCommand: no se encontró stream de video en el archivo.\n";
        return "";
    }

    std::stringstream cmd;
    const int q   = g_config.quality_q;
    const int gop = static_cast<int>(info.framerate * 10.0);
    const std::string pix_fmt = g_config.enable_10bit ? "-pix_fmt p010le " : "";

    // Binario
    cmd << getBinPath("ffmpeg") << " -y ";

    // Hardware accel según encoder
    if (g_config.encoder == "hevc_nvenc") {
        cmd << "-hwaccel cuda -hwaccel_output_format cuda ";
    } else if (g_config.encoder == "hevc_qsv") {
        cmd << "-hwaccel qsv -hwaccel_output_format qsv ";
    } else if (g_config.encoder == "hevc_videotoolbox") {
        cmd << "-hwaccel videotoolbox ";
    } else if (g_config.encoder == "hevc_vaapi") {
        if (!g_config.hwaccel_device.empty()) {
            cmd << "-vaapi_device " << g_config.hwaccel_device << " ";
        }
        cmd << "-hwaccel vaapi -hwaccel_output_format vaapi ";
    } else if (g_config.encoder == "hevc_amf") {
        cmd << "-hwaccel auto ";
    }

    // Input
    cmd << "-i \"" << input_file << "\" ";

    // Si es VAAPI, inyectamos el filtro de formato manteniendo 10-bit si es necesario
    if (g_config.encoder == "hevc_vaapi") {
        // Usamos scale_vaapi para cambiar el formato dentro de la GPU (necesario en Intel)
        if (g_config.enable_10bit) {
            cmd << "-vf \"scale_vaapi=format=p010le\" ";
        } else {
            cmd << "-vf \"scale_vaapi=format=nv12\" ";
        }
    }

    // Encoder + parámetros de calidad
    cmd << "-c:v " << g_config.encoder << " ";

    std::string profile = g_config.encoder_profile;

    if (g_config.encoder == "hevc_nvenc") {
        std::string preset = "p4";
        std::string extra_args = "-rc-lookahead 120 -bf 3 -b_ref_mode middle -spatial-aq 1 -temporal-aq 1 ";

        if (profile == "speed") {
            preset = "p3";
            extra_args = "-rc-lookahead 60 -bf 3 -spatial-aq 1 ";
        } else if (profile == "quality") {
            preset = "p6";
            extra_args = "-rc-lookahead 240 -bf 4 -b_ref_mode middle -spatial-aq 1 -temporal-aq 1 ";
        }

        cmd << "-rc:v vbr -cq:v " << q
        << " -qmin:v " << q << " -qmax:v " << q
        << " -preset " << preset << " " << extra_args << pix_fmt;

    } else if (g_config.encoder == "hevc_qsv") {
        std::string preset = "medium";
        std::string extra_args = "-look_ahead_depth 70 -bf 5 -b_strategy 1 ";

        if (profile == "speed") {
            preset = "veryfast";
            extra_args = "-look_ahead_depth 40 -bf 3 ";
        } else if (profile == "quality") {
            preset = "slow";
            extra_args = "-look_ahead_depth 100 -bf 7 -b_strategy 1 ";
        }

        cmd << "-global_quality:v " << q
        << " -preset " << preset << " " << extra_args
        << (g_config.enable_10bit ? "-vf vpp_qsv=format=p010le " : "");

    } else if (g_config.encoder == "hevc_amf") {
        std::string qual = "balanced";
        std::string extra_args = "-bf 3 ";

        if (profile == "speed") {
            qual = "speed";
            extra_args = "-bf 2 ";
        } else if (profile == "quality") {
            qual = "quality";
            extra_args = "-bf 4 ";
        }

        cmd << "-rc:v cqp -qp_i:v " << q
        << " -qp_p:v " << q
        << " -quality:v " << qual << " " << extra_args << pix_fmt;

    } else if (g_config.encoder == "hevc_vaapi") {
        std::string extra_args = "-bf 4 ";
        cmd << "-rc_mode CQP -global_quality " << q << " " << extra_args;

    } else if (g_config.encoder == "hevc_videotoolbox") {
        cmd << "-q:v " << q << " " << pix_fmt;

    } else {
        // libx265 / software
        std::string preset = "medium";
        std::string extra_args = "-x265-params \"rc-lookahead=120:bframes=6:b-adapt=2\" ";

        if (profile == "speed") {
            preset = "fast";
            extra_args = "-x265-params \"rc-lookahead=60:bframes=4\" ";
        } else if (profile == "quality") {
            preset = "slow";
            extra_args = "-x265-params \"rc-lookahead=240:bframes=8:b-adapt=2\" ";
        }

        cmd << "-crf:v " << q << " -preset " << preset << " " << extra_args << pix_fmt;
    }

    // GOP
    cmd << "-g " << gop << " ";

    // Mapeo de streams
    cmd << "-map 0:v:0 ";
    for (int idx : selected_audios) cmd << "-map 0:" << idx << " ";
    for (int idx : selected_subs)   cmd << "-map 0:" << idx << " ";

    // Audio
    cmd << "-af \"aformat=channel_layouts=stereo\" -ac 2 -c:a libopus -b:a " << g_config.opus_bitrate << "000 -vbr:a on ";

    // Subtítulos, capítulos y attachments
    cmd << "-c:s copy -map_chapters 0 -map 0:t? -c:t copy ";

    // Output
    cmd << "\"" << output_file << "\"";

    return cmd.str();
}

bool runCommand(const std::string& cmd) {
    if (g_interrupted) return false;

    std::string full_cmd = cmd + " 2>&1";
    #ifdef _WIN32
    full_cmd = "\"" + full_cmd + "\"";
    #endif

    FILE* pipe = POPEN(full_cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] No se pudo abrir el proceso.\n";
        return false;
    }

    g_current_pipe = pipe;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        if (g_interrupted) break;
        std::cout << buf;
        std::cout.flush();
    }

    bool closed_by_signal = (g_current_pipe.exchange(nullptr) == nullptr);
    PCLOSE(pipe);

    if (closed_by_signal || g_interrupted) return false;

    return true;
}

MediaInfo analyzeMedia(const std::string& filepath) {
    MediaInfo info;

    std::string cmd = getBinPath("ffprobe")
    + " -v error -show_streams -print_format json \""
    + filepath + "\"";

    #ifdef _WIN32
    cmd = "\"" + cmd + "\" 2>nul";
    #else
    cmd += " 2>/dev/null";
    #endif

    FILE* p = POPEN(cmd.c_str(), "r");

    if (!p) {
        std::cerr << "[ERROR] No se pudo ejecutar ffprobe en: " << filepath << "\n";
        return info;
    }

    std::string output;
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) output += buf;
    PCLOSE(p);

    if (output.empty()) {
        std::cerr << "[ERROR] ffprobe no devolvió datos para: " << filepath << "\n";
        return info;
    }
    if (output[0] != '{') {
        std::cerr << "[ERROR] ffprobe devolvió salida inesperada:\n" << output.substr(0, 200) << "\n";
        return info;
    }

    try {
        json j = json::parse(output);

        if (!j.contains("streams") || !j["streams"].is_array()) {
            std::cerr << "[ERROR] JSON de ffprobe no contiene streams válidos.\n";
            return info;
        }

        for (const auto& stream : j["streams"]) {
            if (!stream.contains("codec_type")) continue;

            std::string type = stream["codec_type"].get<std::string>();
            int idx = stream.value("index", -1);
            if (idx < 0) continue;

            if (type == "video" && info.video_stream_index == -1) {
                info.video_stream_index = idx;
                std::string fps_str = stream.value("r_frame_rate", "24/1");
                size_t slash = fps_str.find('/');
                if (slash != std::string::npos) {
                    try {
                        double num = std::stod(fps_str.substr(0, slash));
                        double den = std::stod(fps_str.substr(slash + 1));
                        if (den > 0.0) info.framerate = num / den;
                    } catch (...) {
                        std::cerr << "[WARN] No se pudo parsear framerate: " << fps_str << ", usando 24fps.\n";
                        info.framerate = 24.0;
                    }
                }

            } else if (type == "audio" || type == "subtitle") {
                StreamInfo s;
                s.index      = idx;
                s.codec_type = type;
                s.codec_name = stream.value("codec_name", "unknown");

                if (stream.contains("tags") && stream["tags"].is_object()) {
                    s.language = stream["tags"].value("language", "und");
                    s.title    = stream["tags"].value("title", "");
                } else {
                    s.language = "und";
                    s.title    = "";
                }

                if (type == "audio")    info.audio_streams.push_back(s);
                else                    info.subtitle_streams.push_back(s);
            }
        }

    } catch (const json::parse_error& e) {
        std::cerr << "[ERROR] JSON inválido de ffprobe: " << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] analyzeMedia: " << e.what() << "\n";
    }

    return info;
}

void setupConfig() {
    std::string input;

    // Idioma
    while (true) {
        std::cout << i18n["en"]["lang_prompt"];
        std::getline(std::cin, input);
        std::string t_in = trim(input);
        if (t_in == "1") { g_config.language = "es"; break; }
        if (t_in == "2") { g_config.language = "en"; break; }
    }
    std::cout << "\n" << t("welcome") << "\n\n";

    // Verificar binarios
    verifyBinaries();

    // Detectar encoders con labels amigables
    std::cout << t("enc_prompt");
    std::cout << (g_config.language == "es"
    ? "Detectando hardware compatible, un momento...\n"
    : "Detecting compatible hardware, please wait...\n");

    std::vector<EncoderOption> encoders = detectEncoders();

    if (encoders.empty()) {
        std::cerr << "[ERROR] No se encontró ningún encoder compatible. Verifica tu instalación de FFmpeg.\n";
        return;
    }

    // Mostrar opciones
    while (true) {
        std::cout << "\n" << t("enc_prompt");
        for (size_t i = 0; i < encoders.size(); ++i)
            std::cout << "  " << i + 1 << ") " << encoders[i].label << "\n";
        std::cout << "> ";
        std::getline(std::cin, input);
        try {
            int c = std::stoi(trim(input));
            if (c >= 1 && c <= (int)encoders.size()) {
                g_config.encoder = encoders[c - 1].id;

                // Si la etiqueta contenía el nodo [dev/dri/renderD...], extraerlo
                size_t start_bracket = encoders[c - 1].label.find("[/dev/");
                if (start_bracket != std::string::npos) {
                    size_t end_bracket = encoders[c - 1].label.find("]", start_bracket);
                    if (end_bracket != std::string::npos) {
                        g_config.hwaccel_device = encoders[c - 1].label.substr(start_bracket + 1, end_bracket - start_bracket - 1);
                    }
                }

                std::cout << (g_config.language == "es" ? "Seleccionado: " : "Selected: ")
                << encoders[c - 1].label << "\n";
                break;
            }
        } catch (...) {}
        std::cout << (g_config.language == "es" ? "Opción inválida.\n" : "Invalid option.\n");
    }

    // Perfil de Encoding
    #if !defined(__APPLE__)
    if (g_config.encoder != "hevc_videotoolbox") {
        while (true) {
            std::cout << (g_config.language == "es"
            ? "\nSelecciona el perfil de compresión:\n  1) Speed (Más rápido)\n  2) Balanced (Balanceado)\n  3) Quality (Mejor calidad)\n> "
            : "\nSelect compression profile:\n  1) Speed (Faster)\n  2) Balanced\n  3) Quality (Better quality)\n> ");
            std::getline(std::cin, input);
            std::string t_in = trim(input);
            if (t_in == "1") { g_config.encoder_profile = "speed"; break; }
            if (t_in == "2") { g_config.encoder_profile = "balanced"; break; }
            if (t_in == "3") { g_config.encoder_profile = "quality"; break; }
        }
    }
    #endif

    // Capacidades 10bit
    verifyHardwareCapabilities();

    // Calidad Q
    while (true) {
        #ifdef __APPLE__
        std::cout << (g_config.language == "es"
        ? "Introduce el nivel de calidad (1 a 100, mayor es mejor calidad, recomendado 60-80): "
        : "Enter quality level (1 to 100, higher is better quality, suggested 60-80): ");
        #else
        std::cout << t("q_prompt");
        #endif

        std::getline(std::cin, input);
        try {
            int q = std::stoi(trim(input));

            #ifdef __APPLE__
            if (q >= 1 && q <= 100) { g_config.quality_q = q; break; }
            std::cout << (g_config.language == "es"
            ? "Valor fuera de rango. Ingresa un número entre 1 y 100.\n"
            : "Out of range. Enter a number between 1 and 100.\n");
            #else
            if (q >= 0 && q <= 51) { g_config.quality_q = q; break; }
            std::cout << (g_config.language == "es"
            ? "Valor fuera de rango. Ingresa un número entre 0 y 51.\n"
            : "Out of range. Enter a number between 0 and 51.\n");
            #endif

        } catch (...) {
            std::cout << (g_config.language == "es" ? "Valor inválido.\n" : "Invalid value.\n");
        }
    }

    // Bitrate Opus
    while (true) {
        std::cout << t("bitrate_prompt");
        std::getline(std::cin, input);
        std::string digits;
        for (char c : input) if (std::isdigit(c)) digits += c;
        if (!digits.empty()) {
            int br = std::stoi(digits);
            if (br >= 32 && br <= 512) {
                g_config.opus_bitrate = digits;
                break;
            }
            std::cout << (g_config.language == "es"
            ? "Bitrate fuera de rango. Usa entre 32 y 512.\n"
            : "Bitrate out of range. Use between 32 and 512.\n");
        } else {
            std::cout << (g_config.language == "es" ? "Valor inválido.\n" : "Invalid value.\n");
        }
    }

    saveConfig();
    std::cout << "\n" << t("cfg_saved") << "\n";
}

// Muestra streams disponibles y devuelve los índices seleccionados por el usuario
// Retorna false si el archivo no es válido o no tiene video
bool selectStreams(const MediaInfo& info,
                   std::vector<int>& out_audios,
                   std::vector<int>& out_subs)
{
    if (info.video_stream_index < 0) {
        std::cerr << "[ERROR] El archivo no contiene stream de video.\n";
        return false;
    }

    int idx;
    std::string in;

    // Audio
    std::cout << "\n─── Audio ───────────────────────\n";
    if (info.audio_streams.empty()) {
        std::cout << (g_config.language == "es" ? "  (sin streams de audio)\n" : "  (no audio streams)\n");
    } else {
        for (const auto& a : info.audio_streams) {
            std::cout << "  " << a.index << ": [" << a.language << "] "
            << a.codec_name;
            if (!a.title.empty()) std::cout << " - " << a.title;
            std::cout << "\n";
        }
    }
    std::cout << t("audio_prompt");
    std::getline(std::cin, in);
    std::stringstream ss1(in);
    while (ss1 >> idx) out_audios.push_back(idx);

    // Subtítulos
    std::cout << "\n─── Subtítulos ──────────────────\n";
    if (info.subtitle_streams.empty()) {
        std::cout << (g_config.language == "es" ? "  (sin subtítulos)\n" : "  (no subtitles)\n");
    } else {
        for (const auto& s : info.subtitle_streams) {
            std::cout << "  " << s.index << ": [" << s.language << "] "
            << s.codec_name;
            if (!s.title.empty()) std::cout << " - " << s.title;
            std::cout << "\n";
        }
    }
    std::cout << t("sub_prompt");
    std::getline(std::cin, in);
    std::stringstream ss2(in);
    while (ss2 >> idx) out_subs.push_back(idx);

    return true;
}

bool processSingleFile(const fs::path& p) {
    if (!fs::exists(p) || !fs::is_regular_file(p)) {
        std::cerr << "[ERROR] Archivo no encontrado: " << p << "\n";
        return false;
    }

    MediaInfo info = analyzeMedia(p.string());
    std::vector<int> audios, subs;

    if (!selectStreams(info, audios, subs)) return false;

    std::string out = (p.parent_path() / (p.stem().string() + "_H265_Opus.mkv")).string();
    std::string cmd = buildFfmpegCommand(p.string(), out, info, audios, subs);
    if (cmd.empty()) return false;

    std::cout << "\n" << t("start_encode") << "\n";
    uintmax_t old_sz = fs::file_size(p);

    bool ok = runCommand(cmd);

    if (ok && fs::exists(out))
        printSavings(old_sz, fs::file_size(out));
    else if (!ok)
        std::cerr << "[ERROR] Falló la codificación de: " << p.filename() << "\n";

    return ok;
}

void processBatchFolder(const fs::path& dp) {
    if (!fs::exists(dp) || !fs::is_directory(dp)) {
        std::cerr << "[ERROR] Carpeta no encontrada: " << dp << "\n";
        return;
    }

    // Extensiones soportadas
    const std::vector<std::string> valid_exts = {".mkv", ".mp4", ".avi", ".mov", ".ts"};

    // Recolectar archivos válidos
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dp)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        // Lowercase la extensión para comparar
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(valid_exts.begin(), valid_exts.end(), ext) != valid_exts.end())
            files.push_back(entry.path());
    }

    if (files.empty()) {
        std::cout << (g_config.language == "es"
        ? "[!] No se encontraron archivos de video en la carpeta.\n"
        : "[!] No video files found in folder.\n");
        return;
    }

    // Ordenar alfabéticamente
    std::sort(files.begin(), files.end());

    // Analizar SOLO el primer archivo para obtener streams
    std::cout << (g_config.language == "es"
    ? "\nAnalizando streams del primer archivo: "
    : "\nAnalyzing streams from first file: ")
    << files[0].filename() << "\n";

    MediaInfo ref_info = analyzeMedia(files[0].string());
    std::vector<int> audios, subs;

    if (!selectStreams(ref_info, audios, subs)) return;

    // Confirmar antes de procesar
    int total = static_cast<int>(files.size());
    std::cout << "\n" << (g_config.language == "es"
    ? "Se procesarán " : "Will process ")
    << total
    << (g_config.language == "es" ? " archivos. ¿Continuar? (s/n): " : " files. Continue? (y/n): ");

    std::string confirm;
    std::getline(std::cin, confirm);
    confirm = trim(confirm);
    if (confirm != "s" && confirm != "y") {
        std::cout << (g_config.language == "es" ? "Cancelado.\n" : "Cancelled.\n");
        return;
    }

    // Procesar cada archivo
    int ok_count   = 0;
    int fail_count = 0;
    uintmax_t total_original = 0;
    uintmax_t total_encoded  = 0;

    for (int i = 0; i < total; ++i) {
        if (g_interrupted) break;

        const fs::path& p = files[i];
        std::cout << "\n[" << i + 1 << "/" << total << "] " << p.filename().string() << "\n";

        // Skip si ya existe el output
        std::string out = (p.parent_path() / (p.stem().string() + "_H265_Opus.mkv")).string();
        if (fs::exists(out)) {
            std::cout << (g_config.language == "es"
            ? "  [SKIP] El archivo de salida ya existe.\n"
            : "  [SKIP] Output file already exists.\n");
            continue;
        }

        std::string cmd = buildFfmpegCommand(p.string(), out, ref_info, audios, subs);
        if (cmd.empty()) { ++fail_count; continue; }

        std::cout << t("start_encode") << "\n";
        uintmax_t old_sz = fs::file_size(p);

        bool ok = runCommand(cmd);

        if (ok && fs::exists(out)) {
            uintmax_t new_sz = fs::file_size(out);
            printSavings(old_sz, new_sz, i + 1, total);
            total_original += old_sz;
            total_encoded  += new_sz;
            ++ok_count;
        } else {
            std::cerr << "  [ERROR] Falló: " << p.filename() << "\n";
            ++fail_count;
        }
    }

    // Resumen final
    std::cout << "\n═════════════════════════════════\n";
    std::cout << (g_config.language == "es" ? "  RESUMEN BATCH\n" : "  BATCH SUMMARY\n");
    std::cout << "═════════════════════════════════\n";
    std::cout << (g_config.language == "es" ? "  Completados : " : "  Completed  : ") << ok_count   << "\n";
    std::cout << (g_config.language == "es" ? "  Fallidos    : " : "  Failed     : ") << fail_count << "\n";
    if (total_original > 0)
        printSavings(total_original, total_encoded);
    std::cout << "═════════════════════════════════\n";
}

int main() {
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    #else
    std::setlocale(LC_ALL, "");
    #endif

    if (!loadConfig()) {
        setupConfig();
    } else {
        std::cout << (g_config.language == "es"
        ? "Configuración cargada.\n"
        : "Config loaded.\n");
        verifyBinaries();
    }

    while (true) {
        if (g_interrupted) break;

        std::cout << "\n" << t("mode_prompt")
        << (g_config.language == "es"
        ? "\n  1) Archivo individual\n  2) Carpeta / Batch\n  3) Reconfigurar\n> "
        : "\n  1) Single file\n  2) Folder / Batch\n  3) Reconfigure\n> ");

        std::string mode;
        std::getline(std::cin, mode);
        mode = trim(mode);

        if (mode == "1") {
            std::cout << t("file_prompt");
            std::string path_in;
            std::getline(std::cin, path_in);
            path_in.erase(std::remove(path_in.begin(), path_in.end(), '\"'), path_in.end());
            fs::path p = fs::path(trim(path_in)).lexically_normal();
            processSingleFile(p);

        } else if (mode == "2") {
            std::cout << t("dir_prompt");
            std::string dir_in;
            std::getline(std::cin, dir_in);
            dir_in.erase(std::remove(dir_in.begin(), dir_in.end(), '\"'), dir_in.end());
            fs::path dp = fs::path(trim(dir_in)).lexically_normal();
            processBatchFolder(dp);

        } else if (mode == "3") {
            setupConfig();
            continue;
        } else {
            std::cout << (g_config.language == "es" ? "Opción inválida.\n" : "Invalid option.\n");
            continue;
        }

        std::cout << "\n" << t("another_prompt");
        std::string ans;
        std::getline(std::cin, ans);
        if (trim(ans) != "s" && trim(ans) != "y") break;
    }

    std::cout << "\n" << t("finished") << "\n";
    return 0;
}
