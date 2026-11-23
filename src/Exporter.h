// Exporta el proyecto a MP4 (video MPEG-4 + audio AAC mezclado) usando FFmpeg.
#pragma once
#include <atomic>
#include <functional>
#include <string>

#include "AudioMix.h"
#include "Project.h"

// Se ejecuta en un hilo aparte. onProgress recibe 0..100.
bool exportProject(const Project& p, const AudioBank& bank, const std::string& utf8Path, std::atomic<bool>& cancel,
                   const std::function<void(int)>& onProgress, std::string& err);
