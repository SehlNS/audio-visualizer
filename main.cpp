#include "raylib.h"
#include <cmath>
#include <portaudio.h>
#include <mutex>
#include <cstdio>

constexpr size_t AudioBufferSize = 4096;
constexpr size_t DisplayPoints = 320;
constexpr size_t SamplesPerPoint = 4;
constexpr size_t CapturedHistory = 2048;
constexpr size_t BarCount = 24;
constexpr float MicrophoneSensitivity = 350.0f;
constexpr float WaveformDeadband = 0.009f;
float audioBuffer[AudioBufferSize]{};
size_t audioWriteIndex = 0;
std::mutex audioMutex;

enum class DisplayStyle {
    Waveform,
    Bars,
    PixelSpectrum
};

struct PhosphorTheme {
    const char* name;
    DisplayStyle style;
    Color background;
    Color phosphor;
    Color threshold;
    Color detail;
    Color needle;
};

int AudioCaptureCallback(const void* input, void*, unsigned long frameCount,
                         const PaStreamCallbackTimeInfo*,
                         PaStreamCallbackFlags, void*) {
    if (input == nullptr || !audioMutex.try_lock()) return paContinue;

    const float* samples = static_cast<const float*>(input);
    for (unsigned long i = 0; i < frameCount; ++i) {
        audioBuffer[audioWriteIndex] = samples[i];
        audioWriteIndex = (audioWriteIndex + 1) % AudioBufferSize;
    }

    audioMutex.unlock();
    return paContinue;
}

// Smoothly interpolate between two colors
Color ColorLerp(Color c1, Color c2, float t) {
    return (Color){
        (unsigned char)(c1.r + (c2.r - c1.r) * t),
        (unsigned char)(c1.g + (c2.g - c1.g) * t),
        (unsigned char)(c1.b + (c2.b - c1.b) * t),
        255
    };
}

float GetWaveShape(int waveType, float phase) {
    constexpr float tau = 6.28318530718f;
    switch (waveType) {
        case 1: return phase < 0.5f ? 1.0f : -1.0f;
        case 2: return phase * 2.0f - 1.0f;
        default: return sinf(phase * tau);
    }
}

int main() {
    const int screenWidth = 480;
    const int screenHeight = 320;
    
    InitWindow(screenWidth, screenHeight, "Wild Synth Oscilloscope - Mac Simulator");
    SetTargetFPS(60);

    const PhosphorTheme themes[] = {
        {"WALNUT", DisplayStyle::Waveform, {18, 13, 10, 255}, {224, 177, 112, 255}, {255, 118, 48, 255}, {202, 169, 126, 255}, {242, 220, 180, 255}},
        {"MOSS", DisplayStyle::Waveform, {12, 17, 12, 255}, {176, 190, 128, 255}, {222, 197, 67, 255}, {155, 174, 126, 255}, {230, 220, 177, 255}},
        {"COPPER", DisplayStyle::Waveform, {20, 11, 9, 255}, {222, 145, 95, 255}, {232, 88, 100, 255}, {194, 119, 85, 255}, {240, 196, 151, 255}},
        {"SEPIA", DisplayStyle::Waveform, {22, 16, 11, 255}, {218, 190, 139, 255}, {251, 213, 146, 255}, {170, 145, 109, 255}, {238, 220, 180, 255}},
        {"OLIVE", DisplayStyle::Waveform, {13, 16, 10, 255}, {184, 184, 116, 255}, {191, 205, 86, 255}, {151, 163, 112, 255}, {226, 215, 168, 255}},
        {"CLAY", DisplayStyle::Waveform, {22, 13, 10, 255}, {211, 145, 105, 255}, {245, 152, 75, 255}, {178, 125, 98, 255}, {238, 197, 158, 255}},
        {"NEON", DisplayStyle::Waveform, {7, 12, 27, 255}, {44, 238, 225, 255}, {255, 62, 155, 255}, {72, 160, 185, 255}, {255, 231, 130, 255}},
        {"SUNSET", DisplayStyle::Waveform, {28, 10, 25, 255}, {255, 110, 105, 255}, {255, 225, 76, 255}, {207, 94, 116, 255}, {255, 202, 128, 255}},
        {"ARCADE", DisplayStyle::Waveform, {8, 18, 19, 255}, {163, 255, 61, 255}, {245, 72, 220, 255}, {59, 226, 181, 255}, {255, 244, 118, 255}},
        {"NEON BARS", DisplayStyle::Bars, {7, 12, 25, 255}, {76, 221, 255, 255}, {255, 64, 172, 255}, {75, 147, 189, 255}, {255, 235, 143, 255}},
        {"EMBER BARS", DisplayStyle::Bars, {23, 10, 8, 255}, {255, 132, 52, 255}, {255, 220, 73, 255}, {174, 83, 54, 255}, {255, 224, 163, 255}},
        {"PIXEL SPECTRUM", DisplayStyle::PixelSpectrum, {5, 7, 15, 255}, {255, 91, 34, 255}, {238, 67, 181, 255}, {255, 190, 42, 255}, {151, 73, 213, 255}},
        {"PIXEL CYBER", DisplayStyle::PixelSpectrum, {5, 7, 15, 255}, {42, 240, 205, 255}, {255, 56, 181, 255}, {49, 139, 255, 255}, {255, 235, 82, 255}},
        {"PIXEL ICE", DisplayStyle::PixelSpectrum, {5, 8, 19, 255}, {50, 128, 255, 255}, {145, 90, 255, 255}, {48, 224, 255, 255}, {238, 249, 255, 255}},
        {"PIXEL FIRE", DisplayStyle::PixelSpectrum, {15, 5, 5, 255}, {238, 35, 43, 255}, {255, 168, 22, 255}, {255, 78, 24, 255}, {255, 239, 120, 255}},
    };
    constexpr int themeCount = sizeof(themes) / sizeof(themes[0]);
    int themeIndex = 0;
    Color basePhosphor = themes[themeIndex].phosphor;
    Color thresholdPhosphor = themes[themeIndex].threshold;
    Color backgroundColor = themes[themeIndex].background;
    Color detailColor = themes[themeIndex].detail;
    Color needleColor = themes[themeIndex].needle;

    float warpGain = 2.0f;       // Trace gain (UP/DOWN)
    int waveType = 0;
    const char* waveNames[] = {"SINE", "SQUARE", "SAW"};
    float timeTracker = 0.0f;
    float audioColorThreshold = 0.05f;

    PaError audioError = Pa_Initialize();
    bool portAudioReady = audioError == paNoError;
    PaStream* micStream = nullptr;

    if (portAudioReady) {
        audioError = Pa_OpenDefaultStream(
            &micStream, 1, 0, paFloat32, 44100, 256,
            AudioCaptureCallback, nullptr
        );
    }
    if (audioError == paNoError) audioError = Pa_StartStream(micStream);

    bool micAvailable = audioError == paNoError;
    if (!micAvailable) {
        std::fprintf(stderr, "Microphone unavailable: %s\n", Pa_GetErrorText(audioError));
    }

    float displayedWaveform[DisplayPoints]{};
    const float centerY = screenHeight / 2.0f;
    float audioRms = 0.0f;
    float thresholdBlend = 0.0f;
    float vuLevel = 0.0f;
    float vuPeak = 0.0f;
    float vuPeakHold = 0.0f;
    float displayedBars[BarCount]{};
    bool waveformInitialized = false;
    bool powerOn = true;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_P)) powerOn = !powerOn;
        if (!powerOn) {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("POWER OFF", 187, 147, 16, (Color){110, 84, 58, 255});
            DrawText("PRESS P TO POWER ON", 164, 169, 10, (Color){75, 60, 45, 255});
            EndDrawing();
            continue;
        }

        float frameTime = GetFrameTime();
        timeTracker += frameTime;

        // --- 1. Handle Input (Knob 1 & Knob 2) ---
        if (IsKeyPressed(KEY_UP)) warpGain += 0.25f;
        else if (IsKeyDown(KEY_UP)) warpGain += GetFrameTime() * 0.8f;
        if (IsKeyPressed(KEY_DOWN)) warpGain -= 0.25f;
        else if (IsKeyDown(KEY_DOWN)) warpGain -= GetFrameTime() * 0.8f;
        warpGain = fminf(fmaxf(warpGain, 0.5f), 5.0f);

        if (IsKeyPressed(KEY_RIGHT)) waveType = (waveType + 1) % 3;
        if (IsKeyPressed(KEY_LEFT)) waveType = (waveType + 2) % 3;
        if (IsKeyPressed(KEY_E)) themeIndex = (themeIndex + 1) % themeCount;
        if (IsKeyPressed(KEY_Q)) themeIndex = (themeIndex + themeCount - 1) % themeCount;
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
            audioColorThreshold = fminf(audioColorThreshold + 0.005f, 0.3f);
        }
        if (IsKeyPressed(KEY_LEFT_BRACKET)) {
            audioColorThreshold = fmaxf(audioColorThreshold - 0.005f, 0.005f);
        }

        float colorFadeStart = audioColorThreshold * 0.25f;
        float targetThresholdBlend = fmaxf(0.0f, fminf(1.0f,
            (audioRms - colorFadeStart) / (audioColorThreshold - colorFadeStart)));
        thresholdBlend += (targetThresholdBlend - thresholdBlend)
            * (1.0f - expf(-8.0f * GetFrameTime()));

        float themeEase = 1.0f - expf(-7.0f * frameTime);
        basePhosphor = ColorLerp(basePhosphor, themes[themeIndex].phosphor, themeEase);
        thresholdPhosphor = ColorLerp(thresholdPhosphor, themes[themeIndex].threshold, themeEase);
        backgroundColor = ColorLerp(backgroundColor, themes[themeIndex].background, themeEase);
        detailColor = ColorLerp(detailColor, themes[themeIndex].detail, themeEase);
        needleColor = ColorLerp(needleColor, themes[themeIndex].needle, themeEase);
        Color activePhosphor = ColorLerp(basePhosphor, thresholdPhosphor, thresholdBlend);

        // --- 2. Begin Drawing Frame ---
        float warpAmount = (warpGain - 0.5f) / 4.5f;
        float hazeAlpha = 0.008f + warpAmount * 0.025f;
        float glowWidth = 2.0f + warpAmount * 3.0f;
        float glowAlpha = 0.08f + warpAmount * 0.14f;
        BeginDrawing();
        ClearBackground(backgroundColor);

        DrawCircleGradient(
            Vector2{screenWidth / 2.0f, screenHeight / 2.0f},
            320.0f,
            Fade(activePhosphor, hazeAlpha),
            Fade(activePhosphor, 0.0f)
        );

        // --- 3. Reactive Background Graticule & Scanlines ---
        // Background grid pulses and brightens based on overall warp/signal intensity
        Color gridColor = (Color){(unsigned char)(activePhosphor.r * 0.2f),
                                  (unsigned char)(activePhosphor.g * 0.2f),
                                  (unsigned char)(activePhosphor.b * 0.2f), 255};

        if (themes[themeIndex].style == DisplayStyle::PixelSpectrum) {
            DrawRectangle(10, 42, 326, 230, (Color){5, 7, 15, 255});
            DrawRectangleLines(10, 42, 326, 230, Fade(detailColor, 0.8f));
            for (int y = 52; y < 267; y += 12) {
                for (int x = 19; x < 331; x += 12) {
                    DrawRectangle(x, y, 1, 1, Fade(detailColor, 0.16f));
                }
            }
        } else {
            DrawRectangleLines(10, 42, 326, 230, gridColor);
            DrawLine(10, screenHeight / 2, 336, screenHeight / 2, Fade(gridColor, 0.7f));
            DrawLine(173, 42, 173, 272, Fade(gridColor, 0.7f));

            for (int y = 50; y < 270; y += 8) {
                DrawLine(11, y, 335, y, Fade(detailColor, 0.025f));
            }
        }

        // Draw UI Readouts
        DrawText(TextFormat("WARP %.1fx (UP/DOWN)", warpGain), 12, 5, 10, activePhosphor);
        DrawText(TextFormat("WAVE %s (LEFT/RIGHT)", waveNames[waveType]), 155, 5, 10, activePhosphor);
        DrawText(TextFormat("THEME %s (Q/E)", themes[themeIndex].name), 300, 5, 10, activePhosphor);
        DrawText(TextFormat("LEVEL %.3f  THRESH %.3f ([ / ])", audioRms, audioColorThreshold),
            12, 22, 10, Fade(activePhosphor, 0.7f));
        DrawText("P POWER OFF", 378, 22, 10, detailColor);
        DrawText(micAvailable ? "LIVE MIC" : "SYNTH (NO MIC)",
            12, screenHeight - 17, 10, Fade(activePhosphor, 0.6f));

        // --- 4. Plot the latest microphone samples from left to right ---
        float waveform[DisplayPoints]{};
        float barTargets[BarCount]{};
        if (micAvailable) {
            float history[CapturedHistory]{};
            {
                std::lock_guard<std::mutex> lock(audioMutex);
                size_t first = (audioWriteIndex + AudioBufferSize - CapturedHistory) % AudioBufferSize;
                for (size_t i = 0; i < CapturedHistory; ++i) {
                    history[i] = audioBuffer[(first + i) % AudioBufferSize];
                }
            }

            float mean = 0.0f;
            for (float sample : history) mean += sample;
            mean /= CapturedHistory;
            float sumSquares = 0.0f;
            for (float& sample : history) {
                sample -= mean;
                sumSquares += sample * sample;
            }
            audioRms = sqrtf(sumSquares / CapturedHistory);

            constexpr size_t samplesPerBar = CapturedHistory / BarCount;
            for (size_t bar = 0; bar < BarCount; ++bar) {
                float barSumSquares = 0.0f;
                size_t barStart = bar * samplesPerBar;
                for (size_t i = 0; i < samplesPerBar; ++i) {
                    float sample = history[barStart + i];
                    barSumSquares += sample * sample;
                }
                float barRms = sqrtf(barSumSquares / samplesPerBar);
                barTargets[bar] = fminf(barRms * MicrophoneSensitivity * 1.5f
                    * warpGain * 1.4142f, screenHeight * 0.58f);
            }

            float crossingThreshold = fmaxf(0.001f, audioRms * 0.08f);
            size_t latestCrossing = CapturedHistory / 2;
            size_t previousCrossing = 0;
            float periodTotal = 0.0f;
            int periodCount = 0;
            bool foundCrossing = false;
            for (size_t i = 1; i < CapturedHistory; ++i) {
                if (history[i - 1] <= -crossingThreshold && history[i] >= crossingThreshold) {
                    if (foundCrossing) {
                        size_t interval = i - previousCrossing;
                        if (interval >= 8 && interval <= 1100) {
                            periodTotal += interval;
                            ++periodCount;
                        }
                    }
                    previousCrossing = i;
                    latestCrossing = i;
                    foundCrossing = true;
                }
            }
            float estimatedPeriod = periodCount > 0 ? periodTotal / periodCount : 220.0f;

            size_t first = CapturedHistory - DisplayPoints * SamplesPerPoint;
            float amplitude = audioRms >= WaveformDeadband
                ? fminf(audioRms * MicrophoneSensitivity * 1.5f * warpGain * 1.4142f,
                        screenHeight * 0.42f)
                : 0.0f;
            for (size_t i = 0; i < DisplayPoints; ++i) {
                float samplePosition = first + i * SamplesPerPoint + SamplesPerPoint * 0.5f;
                float phase = (samplePosition - latestCrossing) / estimatedPeriod;
                phase -= floorf(phase);
                float y = centerY - GetWaveShape(waveType, phase) * amplitude;
                waveform[i] = fmaxf(55.0f, fminf(screenHeight - 55.0f, y));
            }
        } else {
            for (size_t i = 0; i < DisplayPoints; ++i) {
                float phase = fmodf(timeTracker * 2.0f + (float)i / 48.0f, 1.0f);
                waveform[i] = centerY - GetWaveShape(waveType, phase) * 35.0f * warpGain;
            }
            for (size_t bar = 0; bar < BarCount; ++bar) {
                float phase = timeTracker * 2.0f + (float)bar / BarCount * 6.2831853f;
                barTargets[bar] = (0.5f + 0.5f * sinf(phase)) * 65.0f;
            }
        }

        if (!waveformInitialized) {
            for (size_t i = 0; i < DisplayPoints; ++i) displayedWaveform[i] = waveform[i];
            waveformInitialized = true;
        } else {
            float waveformSmoothing = waveType == 1 ? 0.7f : 0.38f;
            for (size_t i = 0; i < DisplayPoints; ++i) {
                displayedWaveform[i] += (waveform[i] - displayedWaveform[i]) * waveformSmoothing;
            }
        }
        for (size_t bar = 0; bar < BarCount; ++bar) {
            float response = barTargets[bar] > displayedBars[bar] ? 0.5f : 0.16f;
            displayedBars[bar] += (barTargets[bar] - displayedBars[bar]) * response;
        }
        constexpr float VuFullScale = 0.12f;
        float targetVu = fminf(audioRms / VuFullScale, 1.0f);
        float vuResponse = targetVu > vuLevel ? 0.45f : 0.10f;
        vuLevel += (targetVu - vuLevel) * vuResponse;
        if (vuLevel >= vuPeak) {
            vuPeak = vuLevel;
            vuPeakHold = 0.75f;
        } else if (vuPeakHold > 0.0f) {
            vuPeakHold = fmaxf(0.0f, vuPeakHold - frameTime);
        } else {
            vuPeak = fmaxf(vuLevel, vuPeak - frameTime * 0.35f);
        }

        Vector2 meterCenter = {402.0f, 184.0f};
        constexpr float meterRadius = 46.0f;
        constexpr float halfPi = 1.57079632679f;
        constexpr float pi = 3.14159265359f;
        DrawText("VU / PEAK", 374, 48, 10, activePhosphor);
        for (int tick = 0; tick <= 32; ++tick) {
            float level = tick / 32.0f;
            float angle = pi + level * pi;
            float innerRadius = tick % 4 == 0 ? meterRadius - 10.0f : meterRadius - 5.0f;
            Vector2 inner = {meterCenter.x + cosf(angle) * innerRadius,
                             meterCenter.y + sinf(angle) * innerRadius};
            Vector2 outer = {meterCenter.x + cosf(angle) * meterRadius,
                             meterCenter.y + sinf(angle) * meterRadius};
            Color tickColor = level >= audioColorThreshold / VuFullScale
                ? thresholdPhosphor
                : Fade(activePhosphor, 0.85f);
            DrawLineEx(inner, outer, tick % 4 == 0 ? 1.5f : 1.0f, tickColor);
        }

        float thresholdLevel = fminf(audioColorThreshold / VuFullScale, 1.0f);
        float thresholdAngle = pi + thresholdLevel * pi;
        Vector2 thresholdInner = {meterCenter.x + cosf(thresholdAngle) * (meterRadius - 13.0f),
                                  meterCenter.y + sinf(thresholdAngle) * (meterRadius - 13.0f)};
        Vector2 thresholdOuter = {meterCenter.x + cosf(thresholdAngle) * (meterRadius + 2.0f),
                                  meterCenter.y + sinf(thresholdAngle) * (meterRadius + 2.0f)};
        DrawLineEx(thresholdInner, thresholdOuter, 2.0f, thresholdPhosphor);

        float peakAngle = pi + vuPeak * pi;
        Vector2 peakInner = {meterCenter.x + cosf(peakAngle) * (meterRadius - 3.0f),
                             meterCenter.y + sinf(peakAngle) * (meterRadius - 3.0f)};
        Vector2 peakOuter = {meterCenter.x + cosf(peakAngle) * (meterRadius + 4.0f),
                             meterCenter.y + sinf(peakAngle) * (meterRadius + 4.0f)};
        DrawLineEx(peakInner, peakOuter, 2.0f, needleColor);

        float needleAngle = pi + vuLevel * pi;
        Vector2 needleTip = {meterCenter.x + cosf(needleAngle) * (meterRadius - 14.0f),
                             meterCenter.y + sinf(needleAngle) * (meterRadius - 14.0f)};
        DrawLineEx(meterCenter, needleTip, 2.0f, needleColor);
        DrawCircleV(meterCenter, 3.0f, thresholdPhosphor);
        DrawText("0", 356, 185, 8, Fade(activePhosphor, 0.7f));
        DrawText("1", 447, 185, 8, Fade(activePhosphor, 0.7f));

        if (themes[themeIndex].style == DisplayStyle::PixelSpectrum) {
            constexpr float pixelSize = 9.0f;
            constexpr float columnStep = 12.5f;
            constexpr float pixelStep = 11.0f;
            constexpr float pixelLeft = 20.0f;
            constexpr float baseline = 157.0f;
            const Color spectrumColors[] = {
                basePhosphor,
                ColorLerp(basePhosphor, detailColor, 0.65f),
                detailColor,
                ColorLerp(detailColor, thresholdPhosphor, 0.55f),
                thresholdPhosphor,
                needleColor
            };
            for (size_t bar = 0; bar < BarCount; ++bar) {
                int activePixels = (int)(displayedBars[bar] / pixelStep);
                if (activePixels > 9) activePixels = 9;
                float x = pixelLeft + bar * columnStep;
                for (int pixel = 0; pixel < activePixels; ++pixel) {
                    size_t colorIndex = (size_t)fminf(pixel * 5.0f / 8.0f, 5.0f);
                    Color pixelColor = spectrumColors[colorIndex];
                    for (int direction : {-1, 1}) {
                        float y = baseline + direction * pixel * pixelStep;
                        DrawRectangle((int)x, (int)y, (int)pixelSize,
                            (int)pixelSize, Fade(pixelColor, 0.12f));
                        DrawRectangle((int)x + 1, (int)y + 1,
                            (int)pixelSize - 2, (int)pixelSize - 2, pixelColor);
                    }
                }
                DrawRectangle((int)x, (int)baseline, (int)pixelSize,
                    3, Fade(basePhosphor, 0.65f));
            }
        } else if (themes[themeIndex].style == DisplayStyle::Bars) {
            constexpr float barLeft = 18.0f;
            constexpr float barBottom = 260.0f;
            constexpr float barGap = 3.0f;
            constexpr float barWidth = (310.0f - barGap * (BarCount - 1)) / BarCount;
            for (size_t bar = 0; bar < BarCount; ++bar) {
                float height = fmaxf(2.0f, displayedBars[bar]);
                float x = barLeft + bar * (barWidth + barGap);
                float y = barBottom - height;
                float levelBlend = fminf(height / (screenHeight * 0.58f), 1.0f);
                Color barColor = ColorLerp(basePhosphor, thresholdPhosphor,
                    fmaxf(thresholdBlend, levelBlend * 0.75f));
                DrawRectangle((int)x - 1, (int)y - 2, (int)barWidth + 2,
                    (int)height + 4, Fade(barColor, 0.18f));
                DrawRectangle((int)x, (int)y, (int)barWidth, (int)height, barColor);
                DrawRectangle((int)x, (int)y, (int)barWidth, 2, needleColor);
            }
        } else {
            float xStep = 310.0f / DisplayPoints;

            for (size_t i = 0; i < DisplayPoints - 1; ++i) {
                float point1 = (float)i;
                float point2 = (float)(i + 1);
                float x1 = 18 + (point1 * xStep)
                    + sinf(timeTracker * 1.4f + point1 * 0.045f) * warpAmount * 7.0f;
                float x2 = 18 + (point2 * xStep)
                    + sinf(timeTracker * 1.4f + point2 * 0.045f) * warpAmount * 7.0f;
                float y1 = displayedWaveform[i]
                    + sinf(timeTracker * 0.9f + point1 * 0.025f) * warpAmount * 12.0f;
                float y2 = displayedWaveform[i + 1]
                    + sinf(timeTracker * 0.9f + point2 * 0.025f) * warpAmount * 12.0f;

                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2},
                        glowWidth, Fade(activePhosphor, glowAlpha * 0.2f));
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2},
                        glowWidth * 0.55f, Fade(activePhosphor, glowAlpha * 0.5f));
                DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2},
                        1.5f, activePhosphor);
            }
        }

        EndDrawing();
    }

    if (micStream != nullptr) {
        if (Pa_IsStreamActive(micStream) == 1) Pa_StopStream(micStream);
        Pa_CloseStream(micStream);
    }
    if (portAudioReady) Pa_Terminate();
    return 0;
}