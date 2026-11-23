// Scopes de video: forma de onda, parade RGB, histograma y vectorscopio.
#pragma once
#include "Renderer.h"

enum ScopeMode { SC_WAVE = 0, SC_PARADE, SC_HIST, SC_VECTOR };

// Analiza un frame BGRA (sw x sh) y dibuja el scope en 'out' (con su graticula).
void renderScope(int mode, const uint8_t* bgra, int sw, int sh, Canvas& out);
