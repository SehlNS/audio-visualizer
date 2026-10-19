#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <driver/i2s.h>
#include <cmath>

namespace {

constexpr size_t FftSize = 256;
constexpr size_t SpectrumBars = 24;
constexpr size_t BarCount = 24;
constexpr float SampleRate = 44100.0f;
constexpr float MicrophoneSensitivity = 350.0f;
constexpr i2s_port_t AudioPort = I2S_NUM_0;
constexpr float AudioColorThreshold = 0.05f;

constexpr int PinDisplaySck = 18;
constexpr int PinDisplayMosi = 23;
constexpr int PinDisplayCs = 13;
constexpr int PinDisplayDc = 21;
constexpr int PinDisplayReset = 22;
constexpr int PinMicrophoneBclk = 26;
constexpr int PinMicrophoneWs = 25;
constexpr int PinMicrophoneData = 34;
constexpr int PinPotWarp = 32;
constexpr int PinPotWave = 33;
constexpr int PinPotTheme = 35;

enum class DisplayStyle {
    Waveform,
    Bars,
    PixelSpectrum
};

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct Palette {
    const char* name;
    DisplayStyle style;
    Rgb background;
    Rgb phosphor;
    Rgb threshold;
    Rgb detail;
    Rgb needle;
};

constexpr Palette Palettes[] = {
    {"WALNUT", DisplayStyle::Waveform, {18, 13, 10}, {224, 177, 112}, {255, 118, 48}, {202, 169, 126}, {242, 220, 180}},
    {"MOSS", DisplayStyle::Waveform, {12, 17, 12}, {176, 190, 128}, {222, 197, 67}, {155, 174, 126}, {230, 220, 177}},
    {"COPPER", DisplayStyle::Waveform, {20, 11, 9}, {222, 145, 95}, {232, 88, 100}, {194, 119, 85}, {240, 196, 151}},
    {"SEPIA", DisplayStyle::Waveform, {22, 16, 11}, {218, 190, 139}, {251, 213, 146}, {170, 145, 109}, {238, 220, 180}},
    {"OLIVE", DisplayStyle::Waveform, {13, 16, 10}, {184, 184, 116}, {191, 205, 86}, {151, 163, 112}, {226, 215, 168}},
    {"CLAY", DisplayStyle::Waveform, {22, 13, 10}, {211, 145, 105}, {245, 152, 75}, {178, 125, 98}, {238, 197, 158}},
    {"NEON", DisplayStyle::Waveform, {7, 12, 27}, {44, 238, 225}, {255, 62, 155}, {72, 160, 185}, {255, 231, 130}},
    {"SUNSET", DisplayStyle::Waveform, {28, 10, 25}, {255, 110, 105}, {255, 225, 76}, {207, 94, 116}, {255, 202, 128}},
    {"ARCADE", DisplayStyle::Waveform, {8, 18, 19}, {163, 255, 61}, {245, 72, 220}, {59, 226, 181}, {255, 244, 118}},
    {"NEON BARS", DisplayStyle::Bars, {7, 12, 25}, {76, 221, 255}, {255, 64, 172}, {75, 147, 189}, {255, 235, 143}},
    {"EMBER BARS", DisplayStyle::Bars, {23, 10, 8}, {255, 132, 52}, {255, 220, 73}, {174, 83, 54}, {255, 224, 163}},
    {"PIXEL SPECTRUM", DisplayStyle::PixelSpectrum, {5, 7, 15}, {255, 91, 34}, {238, 67, 181}, {255, 190, 42}, {151, 73, 213}},
    {"PIXEL CYBER", DisplayStyle::PixelSpectrum, {5, 7, 15}, {42, 240, 205}, {255, 56, 181}, {49, 139, 255}, {255, 235, 82}},
    {"PIXEL ICE", DisplayStyle::PixelSpectrum, {5, 8, 19}, {50, 128, 255}, {145, 90, 255}, {48, 224, 255}, {238, 249, 255}},
    {"PIXEL FIRE", DisplayStyle::PixelSpectrum, {15, 5, 5}, {238, 35, 43}, {255, 168, 22}, {255, 78, 24}, {255, 239, 120}}
};
constexpr size_t PaletteCount = sizeof(Palettes) / sizeof(Palettes[0]);
Palette DisplayPalette = Palettes[0];

Arduino_DataBus* displayBus = new Arduino_ESP32SPI(
    PinDisplayDc, PinDisplayCs, PinDisplaySck, PinDisplayMosi,
    GFX_NOT_DEFINED, VSPI);
Arduino_GFX* display = new Arduino_ILI9488_18bit(
    displayBus, PinDisplayReset, 1, false);

float samples[FftSize]{};
float realPart[FftSize]{};
float imaginaryPart[FftSize]{};
float spectrum[SpectrumBars]{};
size_t sampleWriteIndex = 0;
uint64_t syntheticSampleIndex = 0;
uint32_t lastFrameTime = 0;
size_t paletteIndex = 0;
int waveType = 0;
float warpGain = 2.0f;
volatile bool audioReady = false;
bool syntheticMode = false;
float audioRms = 0.0f;
SemaphoreHandle_t audioDataMutex = nullptr;
float displayedWaveform[FftSize]{};
float displayedBars[BarCount]{};
float vuLevel = 0.0f;
float vuPeak = 0.0f;
float vuPeakHold = 0.0f;

uint16_t ToRgb565(Rgb color) {
    return static_cast<uint16_t>(((color.r & 0xF8) << 8)
        | ((color.g & 0xFC) << 3) | (color.b >> 3));
}

Rgb LerpRgb(Rgb from, Rgb to, float amount) {
    amount = fminf(fmaxf(amount, 0.0f), 1.0f);
    return {
        static_cast<uint8_t>(from.r + (to.r - from.r) * amount),
        static_cast<uint8_t>(from.g + (to.g - from.g) * amount),
        static_cast<uint8_t>(from.b + (to.b - from.b) * amount)
    };
}

float GetWaveShape(int shape, float phase) {
    constexpr float tau = 6.28318530718f;
    switch (shape) {
        case 1: return phase < 0.5f ? 1.0f : -1.0f;
        case 2: return phase * 2.0f - 1.0f;
        default: return sinf(phase * tau);
    }
}

int ReadPotentiometer(int pin) {
    uint32_t total = 0;
    for (int sample = 0; sample < 8; ++sample) {
        total += static_cast<uint32_t>(analogRead(pin));
        delayMicroseconds(150);
    }
    return static_cast<int>(total / 8);
}

void UpdateControls() {
    int warpReading = ReadPotentiometer(PinPotWarp);
    warpGain = 0.5f + (static_cast<float>(warpReading) / 4095.0f) * 4.5f;

    int waveReading = ReadPotentiometer(PinPotWave);
    int candidateWaveType = (waveReading * 3) / 4096;
    if (candidateWaveType > 2) candidateWaveType = 2;
    if (candidateWaveType != waveType) {
        int boundary = candidateWaveType > waveType
            ? ((waveType + 1) * 4096) / 3
            : (waveType * 4096) / 3;
        constexpr int WaveHysteresis = 48;
        if ((candidateWaveType > waveType && waveReading > boundary + WaveHysteresis)
            || (candidateWaveType < waveType && waveReading < boundary - WaveHysteresis)) {
            waveType = candidateWaveType;
        }
    }

    int themeReading = ReadPotentiometer(PinPotTheme);
    int candidate = (themeReading * static_cast<int>(PaletteCount)) / 4096;
    if (candidate >= static_cast<int>(PaletteCount)) candidate = PaletteCount - 1;
    if (candidate != static_cast<int>(paletteIndex)) {
        int boundary = candidate > static_cast<int>(paletteIndex)
            ? ((static_cast<int>(paletteIndex) + 1) * 4096) / PaletteCount
            : (static_cast<int>(paletteIndex) * 4096) / PaletteCount;
        constexpr int ThemeHysteresis = 72;
        if ((candidate > static_cast<int>(paletteIndex)
                && themeReading > boundary + ThemeHysteresis)
            || (candidate < static_cast<int>(paletteIndex)
                && themeReading < boundary - ThemeHysteresis)) {
            paletteIndex = static_cast<size_t>(candidate);
        }
    }
}

void SetupMicrophone() {
    i2s_config_t config{};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = static_cast<int>(SampleRate);
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 128;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    esp_err_t result = i2s_driver_install(AudioPort, &config, 0, nullptr);
    if (result != ESP_OK) {
        Serial.printf("I2S driver install failed: %s\n", esp_err_to_name(result));
        return;
    }

    i2s_pin_config_t pins{};
    pins.bck_io_num = PinMicrophoneBclk;
    pins.ws_io_num = PinMicrophoneWs;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = PinMicrophoneData;
    result = i2s_set_pin(AudioPort, &pins);
    if (result != ESP_OK) {
        Serial.printf("I2S pin setup failed: %s\n", esp_err_to_name(result));
        i2s_driver_uninstall(AudioPort);
        return;
    }

    result = i2s_set_clk(AudioPort, static_cast<uint32_t>(SampleRate),
        I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
    if (result != ESP_OK) {
        Serial.printf("I2S clock setup failed: %s\n", esp_err_to_name(result));
        i2s_driver_uninstall(AudioPort);
        return;
    }
    audioReady = true;
}

void RunFft() {
    float mean = 0.0f;
    for (float sample : samples) mean += sample;
    mean /= FftSize;
    float sumSquares = 0.0f;

    for (size_t i = 0; i < FftSize; ++i) {
        float centered = samples[(sampleWriteIndex + i) % FftSize] - mean;
        sumSquares += centered * centered;
        float window = 0.5f - 0.5f * cosf(
            6.28318530718f * static_cast<float>(i) / (FftSize - 1));
        realPart[i] = centered * window;
        imaginaryPart[i] = 0.0f;
    }
    audioRms = sqrtf(sumSquares / FftSize);

    for (size_t i = 1, j = 0; i < FftSize; ++i) {
        size_t bit = FftSize >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float realSwap = realPart[i];
            realPart[i] = realPart[j];
            realPart[j] = realSwap;
            float imaginarySwap = imaginaryPart[i];
            imaginaryPart[i] = imaginaryPart[j];
            imaginaryPart[j] = imaginarySwap;
        }
    }

    for (size_t length = 2; length <= FftSize; length <<= 1) {
        float angle = -6.28318530718f / static_cast<float>(length);
        float stepReal = cosf(angle);
        float stepImaginary = sinf(angle);
        for (size_t start = 0; start < FftSize; start += length) {
            float twiddleReal = 1.0f;
            float twiddleImaginary = 0.0f;
            size_t half = length >> 1;
            for (size_t offset = 0; offset < half; ++offset) {
                size_t even = start + offset;
                size_t odd = even + half;
                float oddReal = realPart[odd] * twiddleReal
                    - imaginaryPart[odd] * twiddleImaginary;
                float oddImaginary = realPart[odd] * twiddleImaginary
                    + imaginaryPart[odd] * twiddleReal;
                realPart[odd] = realPart[even] - oddReal;
                imaginaryPart[odd] = imaginaryPart[even] - oddImaginary;
                realPart[even] += oddReal;
                imaginaryPart[even] += oddImaginary;
                float nextReal = twiddleReal * stepReal
                    - twiddleImaginary * stepImaginary;
                twiddleImaginary = twiddleReal * stepImaginary
                    + twiddleImaginary * stepReal;
                twiddleReal = nextReal;
            }
        }
    }

    for (size_t bar = 0; bar < SpectrumBars; ++bar) {
        size_t firstBin = 1 + (bar * bar * 120) / (SpectrumBars * SpectrumBars);
        size_t lastBin = 1 + ((bar + 1) * (bar + 1) * 120)
            / (SpectrumBars * SpectrumBars);
        if (lastBin <= firstBin) lastBin = firstBin + 1;
        float magnitude = 0.0f;
        for (size_t bin = firstBin; bin < lastBin && bin < FftSize / 2; ++bin) {
            float binMagnitude = sqrtf(realPart[bin] * realPart[bin]
                + imaginaryPart[bin] * imaginaryPart[bin]) * (2.0f / FftSize);
            if (binMagnitude > magnitude) magnitude = binMagnitude;
        }
        float target = log1pf(magnitude * 200.0f) / logf(201.0f);
        if (target > 1.0f) target = 1.0f;
        float response = target > spectrum[bar] ? 0.52f : 0.16f;
        spectrum[bar] += (target - spectrum[bar]) * response;
    }
}

void CaptureAudioTask(void*) {
    int32_t input[128];
    while (audioReady) {
        size_t bytesRead = 0;
        esp_err_t result = i2s_read(AudioPort, input, sizeof(input),
            &bytesRead, portMAX_DELAY);
        if (result != ESP_OK) {
            Serial.printf("I2S read failed: %s\n", esp_err_to_name(result));
            i2s_driver_uninstall(AudioPort);
            audioReady = false;
            break;
        }
        size_t count = bytesRead / sizeof(input[0]);
        if (count == 0) continue;
        xSemaphoreTake(audioDataMutex, portMAX_DELAY);
        for (size_t i = 0; i < count; ++i) {
            samples[sampleWriteIndex] = static_cast<float>(input[i])
                / 2147483648.0f;
            sampleWriteIndex = (sampleWriteIndex + 1) % FftSize;
        }
        RunFft();
        xSemaphoreGive(audioDataMutex);
    }
    vTaskDelete(nullptr);
}

void SynthesizeAudio(uint32_t now) {
    uint64_t targetSampleIndex = static_cast<uint64_t>(now) * SampleRate / 1000;
    if (targetSampleIndex <= syntheticSampleIndex) return;
    uint64_t firstSampleIndex = targetSampleIndex > FftSize
        ? targetSampleIndex - FftSize
        : syntheticSampleIndex;
    if (firstSampleIndex > syntheticSampleIndex) {
        syntheticSampleIndex = firstSampleIndex;
    }
    for (; syntheticSampleIndex < targetSampleIndex; ++syntheticSampleIndex) {
        float time = static_cast<float>(syntheticSampleIndex) / SampleRate;
        samples[sampleWriteIndex] = 0.28f * sinf(6.28318530718f * 110.0f * time)
            + 0.12f * sinf(6.28318530718f * 330.0f * time);
        sampleWriteIndex = (sampleWriteIndex + 1) % FftSize;
    }
    RunFft();
}

void DrawWaveform(const Palette& palette, const float* frameSamples,
                  float frameRms, Rgb traceColor, float dt, uint32_t now) {
    constexpr int left = 18;
    constexpr int width = 310;
    constexpr float center = 160.0f;
    constexpr float deadband = 0.009f;
    float crossingThreshold = fmaxf(0.001f, frameRms * 0.08f);
    size_t latestCrossing = FftSize / 2;
    size_t previousCrossing = 0;
    float periodTotal = 0.0f;
    int periodCount = 0;
    bool foundCrossing = false;
    float mean = 0.0f;
    for (size_t i = 0; i < FftSize; ++i) mean += frameSamples[i];
    mean /= FftSize;
    for (size_t i = 1; i < FftSize; ++i) {
        float before = frameSamples[i - 1] - mean;
        float after = frameSamples[i] - mean;
        if (before <= -crossingThreshold && after >= crossingThreshold) {
            if (foundCrossing) {
                size_t interval = i - previousCrossing;
                if (interval >= 8) {
                    periodTotal += interval;
                    ++periodCount;
                }
            }
            previousCrossing = i;
            latestCrossing = i;
            foundCrossing = true;
        }
    }
    float period = periodCount > 0 ? periodTotal / periodCount : FftSize / 2.0f;
    float amplitude = frameRms >= deadband
        ? fminf(frameRms * MicrophoneSensitivity * 1.5f * warpGain * 1.4142f, 105.0f)
        : 0.0f;
    float warpAmount = (warpGain - 0.5f) / 4.5f;
    Rgb halo = LerpRgb(palette.background, traceColor, 0.22f);
    Rgb glow = LerpRgb(palette.background, traceColor, 0.55f);
    float smoothing = waveType == 1 ? 0.7f : 0.38f;

    int previousX = left;
    int previousY = static_cast<int>(center);
    for (size_t point = 0; point < FftSize; ++point) {
        float phase = (static_cast<float>(point) - latestCrossing) / period;
        phase -= floorf(phase);
        float targetY = center - GetWaveShape(waveType, phase) * amplitude;
        targetY = fmaxf(55.0f, fminf(265.0f, targetY));
        displayedWaveform[point] += (targetY - displayedWaveform[point]) * smoothing;
        float xPosition = left + static_cast<float>(point) * width / (FftSize - 1);
        int x = static_cast<int>(xPosition
            + sinf(now * 0.0014f + point * 0.045f) * warpAmount * 7.0f);
        int y = static_cast<int>(displayedWaveform[point]
            + sinf(now * 0.0009f + point * 0.025f) * warpAmount * 12.0f);
        if (point > 0) {
            display->drawLine(previousX, previousY - 1, x, y - 1, ToRgb565(halo));
            display->drawLine(previousX, previousY, x, y, ToRgb565(glow));
            display->drawLine(previousX, previousY, x, y, ToRgb565(traceColor));
        }
        previousX = x;
        previousY = y;
    }
}

void DrawSpectrum(const Palette& palette, const float* frameSpectrum,
                  Rgb activeColor, bool pixelStyle) {
    const int left = pixelStyle ? 20 : 18;
    const int baseline = pixelStyle ? 157 : 260;
    const float stepX = pixelStyle ? 12.5f : 310.0f / SpectrumBars;
    const int barWidth = pixelStyle ? 9 : 10;
    const float maxHeight = pixelStyle ? 8.0f : 125.0f;
    const float barStep = pixelStyle ? 11.0f : 1.0f;
    display->drawFastHLine(10, baseline, 326, ToRgb565(palette.detail));

    for (size_t bar = 0; bar < SpectrumBars; ++bar) {
        float level = fminf(frameSpectrum[bar], 1.0f);
        int count = static_cast<int>(level * maxHeight + 0.5f);
        float x = left + bar * stepX;
        Rgb barColor = LerpRgb(activeColor, palette.threshold, level);
        if (pixelStyle) {
            for (int pixel = 0; pixel < count; ++pixel) {
                float y = baseline - pixel * barStep;
                display->fillRect(static_cast<int>(x), static_cast<int>(y),
                    barWidth, static_cast<int>(barStep - 2), ToRgb565(barColor));
                display->fillRect(static_cast<int>(x),
                    static_cast<int>(baseline + pixel * barStep), barWidth,
                    static_cast<int>(barStep - 2), ToRgb565(barColor));
            }
        } else if (count > 0) {
            display->fillRect(static_cast<int>(x), baseline - count, barWidth,
                count, ToRgb565(barColor));
            display->fillRect(static_cast<int>(x), baseline - count - 2,
                barWidth, 2, ToRgb565(palette.needle));
        }
    }
}

void DrawAudioBars(const Palette& palette, Rgb activeColor) {
    constexpr float left = 18.0f;
    constexpr float baseline = 260.0f;
    constexpr float maxHeight = 125.0f;
    constexpr float gap = 3.0f;
    constexpr float barWidth = (310.0f - gap * (BarCount - 1)) / BarCount;
    for (size_t bar = 0; bar < BarCount; ++bar) {
        float height = fmaxf(2.0f, displayedBars[bar]);
        float x = left + bar * (barWidth + gap);
        float y = baseline - height;
        float levelBlend = fminf(height / maxHeight, 1.0f) * 0.75f;
        Rgb barColor = LerpRgb(activeColor, palette.threshold, levelBlend);
        display->fillRect(static_cast<int>(x) - 1, static_cast<int>(y) - 2,
            static_cast<int>(barWidth) + 2, static_cast<int>(height) + 4,
            ToRgb565(LerpRgb(palette.background, barColor, 0.25f)));
        display->fillRect(static_cast<int>(x), static_cast<int>(y),
            static_cast<int>(barWidth), static_cast<int>(height), ToRgb565(barColor));
        display->fillRect(static_cast<int>(x), static_cast<int>(y),
            static_cast<int>(barWidth), 2, ToRgb565(palette.needle));
    }
}

void DrawVuMeter(const Palette& palette, Rgb activeColor, float rms, float dt) {
    constexpr float fullScale = 0.12f;
    constexpr float pi = 3.14159265359f;
    constexpr float radius = 46.0f;
    constexpr float centerX = 402.0f;
    constexpr float centerY = 184.0f;
    float target = fminf(rms / fullScale, 1.0f);
    float response = target > vuLevel ? 0.45f : 0.10f;
    vuLevel += (target - vuLevel) * response;
    if (vuLevel >= vuPeak) {
        vuPeak = vuLevel;
        vuPeakHold = 0.75f;
    } else if (vuPeakHold > 0.0f) {
        vuPeakHold = fmaxf(0.0f, vuPeakHold - dt);
    } else {
        vuPeak = fmaxf(vuLevel, vuPeak - dt * 0.35f);
    }

    display->setTextColor(ToRgb565(activeColor));
    display->setCursor(374, 48);
    display->print("VU / PEAK");
    for (int tick = 0; tick <= 24; ++tick) {
        float level = tick / 24.0f;
        float angle = pi + level * pi;
        float innerRadius = tick % 4 == 0 ? radius - 10.0f : radius - 5.0f;
        int x1 = static_cast<int>(centerX + cosf(angle) * innerRadius);
        int y1 = static_cast<int>(centerY + sinf(angle) * innerRadius);
        int x2 = static_cast<int>(centerX + cosf(angle) * radius);
        int y2 = static_cast<int>(centerY + sinf(angle) * radius);
        Rgb tickColor = level >= AudioColorThreshold / fullScale
            ? palette.threshold : activeColor;
        display->drawLine(x1, y1, x2, y2, ToRgb565(tickColor));
    }

    float thresholdAngle = pi + fminf(AudioColorThreshold / fullScale, 1.0f) * pi;
    display->drawLine(
        static_cast<int>(centerX + cosf(thresholdAngle) * (radius - 13.0f)),
        static_cast<int>(centerY + sinf(thresholdAngle) * (radius - 13.0f)),
        static_cast<int>(centerX + cosf(thresholdAngle) * (radius + 2.0f)),
        static_cast<int>(centerY + sinf(thresholdAngle) * (radius + 2.0f)),
        ToRgb565(palette.threshold));

    float peakAngle = pi + vuPeak * pi;
    float needleAngle = pi + vuLevel * pi;
    display->drawLine(
        static_cast<int>(centerX + cosf(peakAngle) * (radius - 3.0f)),
        static_cast<int>(centerY + sinf(peakAngle) * (radius - 3.0f)),
        static_cast<int>(centerX + cosf(peakAngle) * (radius + 4.0f)),
        static_cast<int>(centerY + sinf(peakAngle) * (radius + 4.0f)),
        ToRgb565(palette.needle));
    display->drawLine(static_cast<int>(centerX), static_cast<int>(centerY),
        static_cast<int>(centerX + cosf(needleAngle) * (radius - 14.0f)),
        static_cast<int>(centerY + sinf(needleAngle) * (radius - 14.0f)),
        ToRgb565(palette.needle));
    display->fillCircle(static_cast<int>(centerX), static_cast<int>(centerY),
        3, ToRgb565(palette.threshold));
}

void DrawFrame(float dt, uint32_t now) {
    const Palette& targetPalette = Palettes[paletteIndex];
    float themeEase = 1.0f - expf(-7.0f * dt);
    DisplayPalette.name = targetPalette.name;
    DisplayPalette.style = targetPalette.style;
    DisplayPalette.background = LerpRgb(DisplayPalette.background,
        targetPalette.background, themeEase);
    DisplayPalette.phosphor = LerpRgb(DisplayPalette.phosphor,
        targetPalette.phosphor, themeEase);
    DisplayPalette.threshold = LerpRgb(DisplayPalette.threshold,
        targetPalette.threshold, themeEase);
    DisplayPalette.detail = LerpRgb(DisplayPalette.detail,
        targetPalette.detail, themeEase);
    DisplayPalette.needle = LerpRgb(DisplayPalette.needle,
        targetPalette.needle, themeEase);
    const Palette& palette = DisplayPalette;
    float frameSpectrum[SpectrumBars];
    float frameSamples[FftSize];
    float frameRms;
    xSemaphoreTake(audioDataMutex, portMAX_DELAY);
    for (size_t i = 0; i < SpectrumBars; ++i) frameSpectrum[i] = spectrum[i];
    for (size_t i = 0; i < FftSize; ++i) {
        frameSamples[i] = samples[(sampleWriteIndex + i) % FftSize];
    }
    frameRms = audioRms;
    xSemaphoreGive(audioDataMutex);

    if (Palettes[paletteIndex].style == DisplayStyle::Bars) {
        constexpr float deadband = 0.009f;
        for (size_t bar = 0; bar < BarCount; ++bar) {
            size_t first = (bar * FftSize) / BarCount;
            size_t last = ((bar + 1) * FftSize) / BarCount;
            float mean = 0.0f;
            for (size_t i = first; i < last; ++i) mean += frameSamples[i];
            mean /= static_cast<float>(last - first);
            float sumSquares = 0.0f;
            for (size_t i = first; i < last; ++i) {
                float sample = frameSamples[i] - mean;
                sumSquares += sample * sample;
            }
            float barRms = sqrtf(sumSquares / static_cast<float>(last - first));
            float target = barRms > deadband
                ? fminf(barRms * MicrophoneSensitivity * 1.5f * warpGain * 1.4142f,
                        125.0f)
                : 0.0f;
            float response = target > displayedBars[bar] ? 0.5f : 0.16f;
            displayedBars[bar] += (target - displayedBars[bar]) * response;
        }
    }

    static float colorBlend = 0.0f;
    float fadeStart = AudioColorThreshold * 0.25f;
    float targetBlend = fmaxf(0.0f, fminf(1.0f,
        (frameRms - fadeStart) / (AudioColorThreshold - fadeStart)));
    colorBlend += (targetBlend - colorBlend) * (1.0f - expf(-8.0f * dt));
    Rgb activeColor = LerpRgb(palette.phosphor, palette.threshold, colorBlend);

    display->fillScreen(ToRgb565(palette.background));
    display->drawRect(10, 42, 326, 230, ToRgb565(palette.detail));
    for (int y = 50; y < 270; y += 12) {
        for (int x = 19; x < 328; x += 12) {
            display->drawPixel(x, y, ToRgb565(LerpRgb(palette.background,
                palette.detail, 0.35f)));
        }
    }

    display->setTextSize(1);
    display->setTextColor(ToRgb565(activeColor));
    display->setCursor(12, 5);
    display->printf("WARP %.1f  WAVE %s  %s", warpGain,
        waveType == 0 ? "SINE" : (waveType == 1 ? "SQUARE" : "SAW"), palette.name);
    display->setTextColor(ToRgb565(palette.detail));
    display->setCursor(12, 22);
    display->printf("LEVEL %.3f / %.3f  %s", frameRms, AudioColorThreshold,
        audioReady ? "LIVE MIC" : (syntheticMode ? "SYNTH" : "NO AUDIO"));

    if (palette.style == DisplayStyle::Waveform) {
        DrawWaveform(palette, frameSamples, frameRms, activeColor, dt, now);
    } else if (palette.style == DisplayStyle::Bars) {
        DrawAudioBars(palette, activeColor);
    } else {
        DrawSpectrum(palette, frameSpectrum, activeColor, true);
    }
    DrawVuMeter(palette, activeColor, frameRms, dt);
    display->drawFastHLine(18, 286, 444, ToRgb565(palette.detail));
    display->setCursor(12, 300);
    display->setTextColor(ToRgb565(palette.needle));
    display->print(palette.style == DisplayStyle::Waveform ? "LIVE WAVEFORM"
        : (palette.style == DisplayStyle::Bars ? "AUDIO BARS" : "PIXEL SPECTRUM"));
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    analogReadResolution(12);
    analogSetPinAttenuation(PinPotWarp, ADC_11db);
    analogSetPinAttenuation(PinPotWave, ADC_11db);
    analogSetPinAttenuation(PinPotTheme, ADC_11db);

    if (!display->begin(40000000)) {
        Serial.println("ILI9488 initialization failed; check display wiring.");
        while (true) delay(1000);
    }
    display->setRotation(1);
    display->fillScreen(ToRgb565({0, 0, 0}));
    SetupMicrophone();
    syntheticMode = !audioReady;
    audioDataMutex = xSemaphoreCreateMutex();
    if (audioDataMutex == nullptr) {
        Serial.println("Audio data mutex creation failed.");
        while (true) delay(1000);
    }
    if (audioReady && xTaskCreatePinnedToCore(CaptureAudioTask, "audio-capture",
            6144, nullptr, 2, nullptr, 0) != pdPASS) {
        Serial.println("Audio capture task creation failed; using synthetic audio.");
        i2s_driver_uninstall(AudioPort);
        audioReady = false;
        syntheticMode = true;
    }
    if (syntheticMode) {
        Serial.println("Microphone unavailable; running synthesized waveform.");
    }
    UpdateControls();
    lastFrameTime = millis();
    DrawFrame(0.1f, lastFrameTime);
}

void loop() {
    uint32_t now = millis();
    static uint32_t lastControlTime = 0;
    if (now - lastControlTime >= 40) {
        UpdateControls();
        lastControlTime = now;
    }

    if (!audioReady && !syntheticMode) {
        syntheticMode = true;
        Serial.println("I2S stopped; switching to synthesized signal.");
    }
    if (syntheticMode) SynthesizeAudio(now);

    if (now - lastFrameTime >= 100) {
        float dt = (now - lastFrameTime) / 1000.0f;
        lastFrameTime = now;
        DrawFrame(dt, now);
    }
}
