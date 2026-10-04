#include "MatrixRain.h"
#include "SystemMetrics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <conio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::atomic<bool> g_running{true};

    DWORD g_originalInputMode = 0;
    bool g_inputModeSaved = false;

    enum class MatrixColor
    {
        Green,
        Blue,
        Purple,
        Red,
        Cyan,
        White,
        Amber
    };

    struct RGB
    {
        int r;
        int g;
        int b;
    };

    struct Stream
    {
        int column = 0;
        float head = 0.0f;
        int length = 12;
        float speed = 16.0f;
        std::vector<char> characters;
        double mutationTimer = 0.0;
        double mutationInterval = 0.07;
    };

    struct Cell
    {
        char character = ' ';
        unsigned char brightness = 0;
    };

    struct Tile
    {
        std::string label;
        std::string value;
        float x = 0.0f;
        float y = -5.0f;
        float speed = 6.0f;
        float phase = 0.0f;
        int width = 29;
    };

    BOOL WINAPI consoleHandler(DWORD signal)
    {
        switch (signal)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false);
            return TRUE;
        default:
            return FALSE;
        }
    }

    MatrixColor parseColor(const std::string &value)
    {
        if (value == "blue")
            return MatrixColor::Blue;
        if (value == "purple")
            return MatrixColor::Purple;
        if (value == "red")
            return MatrixColor::Red;
        if (value == "cyan")
            return MatrixColor::Cyan;
        if (value == "white")
            return MatrixColor::White;
        if (value == "amber")
            return MatrixColor::Amber;
        return MatrixColor::Green;
    }

    RGB accentColor(MatrixColor color)
    {
        switch (color)
        {
        case MatrixColor::Purple:
            return {220, 135, 255};
        case MatrixColor::Blue:
            return {105, 195, 255};
        case MatrixColor::Cyan:
            return {90, 255, 235};
        case MatrixColor::Red:
            return {255, 125, 145};
        case MatrixColor::White:
            return {235, 240, 255};
        case MatrixColor::Amber:
            return {255, 195, 90};
        default:
            return {100, 255, 145};
        }
    }

    RGB secondaryColor(MatrixColor color)
    {
        switch (color)
        {
        case MatrixColor::Purple:
            return {115, 80, 180};
        case MatrixColor::Blue:
            return {70, 125, 190};
        case MatrixColor::Cyan:
            return {55, 145, 155};
        case MatrixColor::Red:
            return {165, 75, 90};
        case MatrixColor::White:
            return {115, 125, 150};
        case MatrixColor::Amber:
            return {150, 105, 55};
        default:
            return {45, 145, 80};
        }
    }

    void matrixColor(
        MatrixColor color,
        int brightness,
        int &r,
        int &g,
        int &b)
    {
        brightness = std::clamp(brightness, 0, 255);

        switch (color)
        {
        case MatrixColor::Green:
            r = 0;
            g = brightness;
            b = static_cast<int>(brightness * 0.12f);
            break;
        case MatrixColor::Blue:
            r = static_cast<int>(brightness * 0.04f);
            g = static_cast<int>(brightness * 0.20f);
            b = brightness;
            break;
        case MatrixColor::Purple:
            r = static_cast<int>(brightness * 0.70f);
            g = static_cast<int>(brightness * 0.04f);
            b = brightness;
            break;
        case MatrixColor::Red:
            r = brightness;
            g = static_cast<int>(brightness * 0.04f);
            b = static_cast<int>(brightness * 0.04f);
            break;
        case MatrixColor::Cyan:
            r = 0;
            g = brightness;
            b = brightness;
            break;
        case MatrixColor::White:
            r = g = b = brightness;
            break;
        case MatrixColor::Amber:
            r = brightness;
            g = static_cast<int>(brightness * 0.55f);
            b = static_cast<int>(brightness * 0.05f);
            break;
        }

        if (brightness > 220)
        {
            switch (color)
            {
            case MatrixColor::Green:
                r = 70;
                g = 255;
                b = 130;
                break;
            case MatrixColor::Blue:
                r = 80;
                g = 200;
                b = 255;
                break;
            case MatrixColor::Purple:
                r = 255;
                g = 110;
                b = 255;
                break;
            case MatrixColor::Red:
                r = 255;
                g = 100;
                b = 100;
                break;
            case MatrixColor::Cyan:
                r = 100;
                g = 255;
                b = 255;
                break;
            case MatrixColor::White:
                r = g = b = 255;
                break;
            case MatrixColor::Amber:
                r = 255;
                g = 205;
                b = 100;
                break;
            }
        }
    }

    char randomCharacter(std::mt19937 &rng)
    {
        static constexpr char characters[] =
            "01ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "<>/\\|+*.:;-";

        std::uniform_int_distribution<int> distribution(
            0,
            static_cast<int>(sizeof(characters) - 2));

        return characters[distribution(rng)];
    }

    void initializeStream(Stream &stream, std::mt19937 &rng)
    {
        stream.characters.resize(static_cast<size_t>(stream.length));

        for (char &character : stream.characters)
            character = randomCharacter(rng);
    }

    void mutateStream(Stream &stream, std::mt19937 &rng)
    {
        if (stream.characters.empty())
            return;

        std::uniform_int_distribution<int> distribution(
            0,
            static_cast<int>(stream.characters.size()) - 1);

        stream.characters[static_cast<size_t>(distribution(rng))] =
            randomCharacter(rng);
    }

    bool terminalSize(int &width, int &height)
    {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE)
            return false;

        CONSOLE_SCREEN_BUFFER_INFO info{};

        if (!GetConsoleScreenBufferInfo(output, &info))
            return false;

        width = info.srWindow.Right - info.srWindow.Left + 1;
        height = info.srWindow.Bottom - info.srWindow.Top + 1;

        return width > 0 && height > 0;
    }

    bool enableVT()
    {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE)
            return false;

        DWORD mode = 0;

        if (!GetConsoleMode(output, &mode))
            return false;

        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        return SetConsoleMode(output, mode) != FALSE;
    }

    void configureInput()
    {
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

        if (input == INVALID_HANDLE_VALUE)
            return;

        DWORD mode = 0;

        if (!GetConsoleMode(input, &mode))
            return;

        g_originalInputMode = mode;
        g_inputModeSaved = true;

        mode &= ~ENABLE_LINE_INPUT;
        mode &= ~ENABLE_ECHO_INPUT;

        SetConsoleMode(input, mode);
    }

    void restoreInput()
    {
        if (!g_inputModeSaved)
            return;

        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

        if (input != INVALID_HANDLE_VALUE)
            SetConsoleMode(input, g_originalInputMode);

        g_inputModeSaved = false;
    }

    void writeFrame(const std::string &frame)
    {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        WriteConsoleA(
            output,
            frame.data(),
            static_cast<DWORD>(frame.size()),
            &written,
            nullptr);
    }

    void enterScreen()
    {
        writeFrame(
            "\x1b[?1049h"
            "\x1b[2J"
            "\x1b[H"
            "\x1b[?25l");
    }

    void leaveScreen()
    {
        writeFrame(
            "\x1b[?25h"
            "\x1b[0m"
            "\x1b[?1049l");
    }

    float smoothStep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float edgeFade(int row, int height)
    {
        if (height <= 1)
            return 1.0f;

        const float position =
            static_cast<float>(row) /
            static_cast<float>(height - 1);

        constexpr float edge = 0.12f;
        constexpr float minimum = 0.12f;

        if (position < edge)
        {
            const float t = smoothStep(position / edge);
            return minimum + (1.0f - minimum) * t;
        }

        if (position > 1.0f - edge)
        {
            const float t = smoothStep((1.0f - position) / edge);
            return minimum + (1.0f - minimum) * t;
        }

        return 1.0f;
    }

    std::string ansi(RGB color, float alpha = 1.0f)
    {
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        return "\x1b[38;2;" +
               std::to_string(static_cast<int>(color.r * alpha)) + ";" +
               std::to_string(static_cast<int>(color.g * alpha)) + ";" +
               std::to_string(static_cast<int>(color.b * alpha)) + "m";
    }

    std::string fit(const std::string &text, int width)
    {
        if (width <= 0)
            return {};

        if (static_cast<int>(text.size()) >= width)
            return text.substr(0, static_cast<size_t>(width));

        return text + std::string(
                          static_cast<size_t>(width - text.size()),
                          ' ');
    }

    void at(
        std::string &frame,
        int row,
        int column,
        const std::string &text)
    {
        if (row < 1 || column < 1)
            return;

        frame += "\x1b[" +
                 std::to_string(row) + ";" +
                 std::to_string(column) + "H" +
                 text;
    }

    std::string bar(double value, int width)
    {
        value = std::clamp(value, 0.0, 100.0);
        width = std::max(width, 1);

        const int filled = static_cast<int>(
            std::round((value / 100.0) * width));

        return "[" +
               std::string(static_cast<size_t>(filled), '#') +
               std::string(
                   static_cast<size_t>(width - filled),
                   '.') +
               "]";
    }

    void drawTile(
        std::string &frame,
        const Tile &tile,
        int width,
        int height,
        RGB accent,
        RGB secondary,
        float alpha)
    {
        if (alpha < 0.02f)
            return;

        const int x = static_cast<int>(std::round(tile.x));
        const int y = static_cast<int>(std::round(tile.y));
        const int tileWidth = tile.width;

        if (tileWidth < 12 ||
            x < 2 ||
            x + tileWidth >= width - 1 ||
            y > height ||
            y + 2 < 1)
        {
            return;
        }

        const std::string top =
            "+" + std::string(static_cast<size_t>(tileWidth - 2), '-') + "+";

        const std::string middle =
            "| " +
            fit(tile.label + "  " + tile.value, tileWidth - 4) +
            " |";

        const std::string bottom =
            "+" + std::string(static_cast<size_t>(tileWidth - 2), '-') + "+";

        at(frame, y, x,
           ansi(secondary, alpha * 0.55f) + top + "\x1b[0m");
        at(frame, y + 1, x,
           ansi(accent, alpha) + middle + "\x1b[0m");
        at(frame, y + 2, x,
           ansi(secondary, alpha * 0.35f) + bottom + "\x1b[0m");
    }

    void glitch(
        std::string &frame,
        int width,
        int height,
        RGB color,
        std::mt19937 &rng,
        float strength)
    {
        if (strength <= 0.01f || width < 30 || height < 8)
            return;

        static constexpr char glyphs[] = "01X#<>/\\|+*";

        std::uniform_int_distribution<int> rowDist(
            1, std::max(1, height));
        std::uniform_int_distribution<int> colDist(
            1, std::max(1, width - 12));
        std::uniform_int_distribution<int> lengthDist(2, 12);
        std::uniform_int_distribution<int> glyphDist(
            0, static_cast<int>(sizeof(glyphs) - 2));

        const int count = 8 + static_cast<int>(strength * 32.0f);

        for (int i = 0; i < count; ++i)
        {
            std::string text;
            const int length = lengthDist(rng);

            for (int j = 0; j < length; ++j)
                text += glyphs[glyphDist(rng)];

            at(
                frame,
                rowDist(rng),
                colDist(rng),
                ansi(color, 0.18f + strength * 0.72f) +
                    text +
                    "\x1b[0m");
        }
    }

    void tileBurst(
        std::string &frame,
        int width,
        int height,
        RGB color,
        std::mt19937 &rng,
        float strength)
    {
        if (strength <= 0.01f || width < 30 || height < 8)
            return;

        static constexpr char glyphs[] = "01X#<>/\\|+*";

        std::uniform_int_distribution<int> rowDist(
            1, std::max(1, height));
        std::uniform_int_distribution<int> colDist(
            1, std::max(1, width - 16));
        std::uniform_int_distribution<int> lengthDist(2, 14);
        std::uniform_int_distribution<int> glyphDist(
            0, static_cast<int>(sizeof(glyphs) - 2));

        const int count = 12 + static_cast<int>(strength * 42.0f);

        for (int i = 0; i < count; ++i)
        {
            std::string text;
            const int length = lengthDist(rng);

            for (int j = 0; j < length; ++j)
                text += glyphs[glyphDist(rng)];

            at(
                frame,
                rowDist(rng),
                colDist(rng),
                ansi(color, 0.10f + strength * 0.80f) +
                    text +
                    "\x1b[0m");
        }
    }

    void updateTileValues(
        std::vector<Tile> &tiles,
        const SystemMetrics::Snapshot &metrics)
    {
        for (auto &tile : tiles)
        {
            if (tile.label == "CPU")
            {
                tile.value =
                    std::to_string(static_cast<int>(std::round(metrics.cpuUsage))) + "%";
            }
            else if (tile.label == "RAM")
            {
                tile.value =
                    std::to_string(static_cast<int>(std::round(metrics.memoryUsage))) + "%";
            }
            else if (tile.label == "STORAGE")
            {
                tile.value =
                    std::to_string(static_cast<int>(std::round(metrics.storageUsedGB))) +
                    "/" +
                    std::to_string(static_cast<int>(std::round(metrics.storageTotalGB))) +
                    " GB";
            }
            else if (tile.label == "GPU")
            {
                tile.value = metrics.gpuName;
            }
            else if (tile.label == "IP")
            {
                tile.value = metrics.localIP;
            }
            else if (tile.label == "PUBLIC IP")
            {
                tile.value = metrics.publicIP;
            }
            else if (tile.label == "UPTIME")
            {
                tile.value = metrics.uptime;
            }
            else if (tile.label == "POWER")
            {
                tile.value = metrics.powerState;
            }
            else if (tile.label == "MADE BY")
            {
                tile.value = "Gaurav";
            }
        }
    }

    void resetTilePositions(
        std::vector<Tile> &tiles,
        int width,
        int panelLeft,
        std::mt19937 &rng)
    {
        const int right =
            panelLeft > 0
                ? panelLeft - 2
                : width - 2;

        const int left = 3;
        const int usable = std::max(12, right - left);

        for (size_t i = 0; i < tiles.size(); ++i)
        {
            Tile &tile = tiles[i];

            const float fraction =
                static_cast<float>((i * 37) % 100) / 100.0f;

            tile.x =
                static_cast<float>(left) +
                fraction * static_cast<float>(
                               std::max(1, usable - tile.width));

            tile.y =
                -static_cast<float>(4 + static_cast<int>(i) * 11);

            tile.phase =
                static_cast<float>(i) * 0.85f;

            std::uniform_real_distribution<float> speedDist(4.5f, 8.0f);
            tile.speed = speedDist(rng);
        }
    }

    void clampTilePositions(
        std::vector<Tile> &tiles,
        int width,
        int panelLeft)
    {
        const int right =
            panelLeft > 0
                ? panelLeft - 2
                : width - 2;

        for (auto &tile : tiles)
        {
            const float maxX = static_cast<float>(
                std::max(3, right - tile.width));

            tile.x = std::clamp(tile.x, 3.0f, maxX);
        }
    }

    int panelWidthFor(int width)
    {
        if (width >= 112)
            return 42;
        if (width >= 90)
            return 36;
        return 0;
    }

    void drawPanel(
        std::string &frame,
        int width,
        int height,
        const SystemMetrics::Snapshot &metrics,
        RGB accent,
        RGB secondary,
        float alpha)
    {
        const int panelWidth = panelWidthFor(width);

        if (panelWidth == 0 || alpha < 0.02f)
            return;

        const int left = width - panelWidth - 2;
        const bool compact =
            panelWidth < 42 || height < 30;

        if (left < 2 || height < 20)
            return;

        const int top = 2;
        const int panelHeight = compact ? 21 : 31;

        if (top + panelHeight > height)
            return;

        const std::string border =
            "+" +
            std::string(static_cast<size_t>(panelWidth - 2), '-') +
            "+";

        auto line =
            [&](int row, const std::string &text, float lineAlpha)
        {
            if (row < 0 || top + row >= height)
                return;

            at(
                frame,
                top + row,
                left,
                ansi(accent, lineAlpha) +
                    "|" +
                    fit(" " + text, panelWidth - 2) +
                    "|" +
                    "\x1b[0m");
        };

        at(frame, top, left,
           ansi(accent, alpha) + border + "\x1b[0m");

        line(1, compact ? "NEXUS // CORE" : "NEXUS // SYSTEM CORE", alpha);
        line(2, std::string(panelWidth - 2, '-'), alpha * 0.45f);

        line(4, "SYSTEM STATUS", alpha);
        line(5, "STATUS     ONLINE", alpha);

        line(7,
             "CPU        " +
                 std::to_string(static_cast<int>(std::round(metrics.cpuUsage))) + "%",
             alpha);
        line(8, " " + bar(metrics.cpuUsage, panelWidth - 8), alpha * 0.75f);

        line(10,
             "RAM        " +
                 std::to_string(static_cast<int>(std::round(metrics.memoryUsage))) + "%",
             alpha);
        line(11, " " + bar(metrics.memoryUsage, panelWidth - 8), alpha * 0.75f);

        line(13,
             "STORAGE    " +
                 std::to_string(static_cast<int>(std::round(metrics.storageUsedGB))) +
                 " / " +
                 std::to_string(static_cast<int>(std::round(metrics.storageTotalGB))) +
                 " GB",
             alpha);

        if (!compact)
        {
            line(14,
                 " " + bar(metrics.storageUsage, panelWidth - 8),
                 alpha * 0.75f);

            line(16, "GRAPHICS", alpha);
            line(17, "GPU        " + metrics.gpuName, alpha);
            line(18,
                 "VRAM       " +
                     std::to_string(static_cast<int>(std::round(metrics.gpuMemoryGB))) +
                     " GB",
                 alpha);

            line(20, "NETWORK", alpha);
            line(21, "LOCAL IP   " + metrics.localIP, alpha);
            line(22, "PUBLIC IP  " + metrics.publicIP, alpha);

            line(24, "SYSTEM", alpha);
            line(25, "HOST       " + metrics.hostName, alpha);
            line(26, "UPTIME     " + metrics.uptime, alpha);

            std::string power = "POWER      " + metrics.powerState;
            if (metrics.batteryPercent >= 0)
                power += "  " + std::to_string(metrics.batteryPercent) + "%";

            line(27, power, alpha);
            line(29, "SPACE      HIDE / SHOW", alpha * 0.78f);
            line(30, "BACKSPACE  TILES ON / OFF", alpha * 0.78f);
        }
        else
        {
            line(15, "GPU        " + metrics.gpuName, alpha);
            line(16, "VRAM       " + std::to_string(static_cast<int>(std::round(metrics.gpuMemoryGB))) + " GB",
                 alpha);
            line(18, "IP         " + metrics.localIP, alpha);
            line(19, "SPACE / BS HIDE / TILES", alpha * 0.78f);
        }

        const int bottom = top + panelHeight - 1;
        if (bottom < height)
        {
            at(frame, bottom, left,
               ansi(secondary, alpha * 0.65f) +
                   border +
                   "\x1b[0m");
        }
    }
}

MatrixRain::MatrixRain(const std::string &color)
    : colorName(color)
{
}

void MatrixRain::run()
{
    g_running.store(true);

    const MatrixColor currentColor = parseColor(colorName);

    if (!SetConsoleCtrlHandler(consoleHandler, TRUE))
        return;

    if (!enableVT())
    {
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        return;
    }

    configureInput();
    enterScreen();

    std::random_device randomDevice;
    std::mt19937 rng(randomDevice());

    std::uniform_int_distribution<int> lengthDistribution(8, 20);
    std::uniform_real_distribution<float> speedDistribution(12.0f, 24.0f);
    std::uniform_real_distribution<double> mutationDistribution(0.045, 0.085);

    int width = 0;
    int height = 0;

    if (!terminalSize(width, height))
    {
        leaveScreen();
        restoreInput();
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        return;
    }

    std::vector<Stream> streams;

    auto createStream = [&](int column)
    {
        Stream stream;
        stream.column = column;
        stream.head = -static_cast<float>(
            rng() % static_cast<unsigned>(std::max(1, height)));
        stream.length = lengthDistribution(rng);
        stream.speed = speedDistribution(rng);
        stream.mutationInterval = mutationDistribution(rng);
        initializeStream(stream, rng);
        streams.push_back(std::move(stream));
    };

    for (int column = 0; column < width; column += 2)
        createStream(column);

    SystemMetrics systemMetrics;
    systemMetrics.update();
    SystemMetrics::Snapshot metrics = systemMetrics.getSnapshot();

    std::vector<Tile> tiles =
        {
            {"CPU", "", 0, -4, 6, 0.0f, 29},
            {"RAM", "", 0, -15, 5, 0.8f, 29},
            {"STORAGE", "", 0, -26, 6.5f, 1.6f, 31},
            {"GPU", "", 0, -37, 5.5f, 2.4f, 33},
            {"IP", "", 0, -48, 6.2f, 3.2f, 29},
            {"PUBLIC IP", "", 0, -59, 5.2f, 4.0f, 31},
            {"UPTIME", "", 0, -70, 5.8f, 4.8f, 29},
            {"POWER", "", 0, -81, 6.6f, 5.6f, 29},
            {"MADE BY", "Gaurav", 0, -92, 5.0f, 6.4f, 29}};

    updateTileValues(tiles, metrics);

    bool dashboardVisible = true;
    bool tilesVisible = true;

    float dashboardAlpha = 1.0f;
    float tilesAlpha = 1.0f;

    constexpr float glitchDuration = 0.48f;
    constexpr float tileBurstDuration = 0.42f;

    float glitchTimer = 0.0f;
    float tileBurstTimer = 0.0f;

    using Clock = std::chrono::steady_clock;

    // This is only the renderer's pacing target. It is NOT displayed as
    // a fake "screen refresh rate" or FPS metric.
    constexpr double targetFPS = 120.0;
    constexpr double targetFrameTime = 1.0 / targetFPS;

    auto previousTime = Clock::now();
    auto lastMetricsUpdate = Clock::now();

    std::string frame;

    auto panelLeftForCurrentSize = [&]() -> int
    {
        const int panelWidth = panelWidthFor(width);
        return panelWidth > 0 ? width - panelWidth - 2 : -1;
    };

    resetTilePositions(
        tiles,
        width,
        panelLeftForCurrentSize(),
        rng);

    while (g_running.load())
    {
        const auto frameStart = Clock::now();

        std::chrono::duration<double> elapsed =
            frameStart - previousTime;

        previousTime = frameStart;

        double deltaTime = elapsed.count();
        if (deltaTime > 0.1)
            deltaTime = 0.1;

        // ---------------------------------------------------------
        // Input
        // ---------------------------------------------------------
        while (_kbhit())
        {
            const int key = _getch();

            if (key == ' ')
            {
                dashboardVisible = !dashboardVisible;
                glitchTimer = glitchDuration;
            }
            else if (key == 8)
            {
                tilesVisible = !tilesVisible;
                tileBurstTimer = tileBurstDuration;
            }
            else if (key == 'q' || key == 'Q' || key == 27)
            {
                g_running.store(false);
            }
        }

        // ---------------------------------------------------------
        // Smooth UI transitions
        // ---------------------------------------------------------
        const float dashboardSpeed = 1.0f / 0.30f;
        const float tilesSpeed = 1.0f / 0.24f;

        if (dashboardVisible)
            dashboardAlpha = std::min(
                1.0f,
                dashboardAlpha + static_cast<float>(deltaTime) * dashboardSpeed);
        else
            dashboardAlpha = std::max(
                0.0f,
                dashboardAlpha - static_cast<float>(deltaTime) * dashboardSpeed);

        if (tilesVisible)
            tilesAlpha = std::min(
                1.0f,
                tilesAlpha + static_cast<float>(deltaTime) * tilesSpeed);
        else
            tilesAlpha = std::max(
                0.0f,
                tilesAlpha - static_cast<float>(deltaTime) * tilesSpeed);

        glitchTimer = std::max(
            0.0f,
            glitchTimer - static_cast<float>(deltaTime));

        tileBurstTimer = std::max(
            0.0f,
            tileBurstTimer - static_cast<float>(deltaTime));

        // ---------------------------------------------------------
        // Live telemetry
        // ---------------------------------------------------------
        const auto now = Clock::now();

        if (std::chrono::duration<double>(now - lastMetricsUpdate).count() >= 0.5)
        {
            systemMetrics.update();
            metrics = systemMetrics.getSnapshot();
            updateTileValues(tiles, metrics);
            lastMetricsUpdate = now;
        }

        // ---------------------------------------------------------
        // Resize without restarting animation
        // ---------------------------------------------------------
        int newWidth = 0;
        int newHeight = 0;

        if (terminalSize(newWidth, newHeight) &&
            (newWidth != width || newHeight != height))
        {
            const int oldWidth = width;
            const int oldHeight = height;

            width = newWidth;
            height = newHeight;

            streams.erase(
                std::remove_if(
                    streams.begin(),
                    streams.end(),
                    [&](const Stream &stream)
                    {
                        return stream.column >= width;
                    }),
                streams.end());

            int firstNewColumn = std::max(0, oldWidth);
            if (firstNewColumn % 2 != 0)
                ++firstNewColumn;

            for (int column = firstNewColumn;
                 column < width;
                 column += 2)
            {
                createStream(column);
            }

            for (auto &stream : streams)
            {
                if (stream.head - static_cast<float>(stream.length) > height)
                {
                    stream.head = -static_cast<float>(
                        rng() % static_cast<unsigned>(std::max(1, height)));
                }
            }

            clampTilePositions(
                tiles,
                width,
                panelLeftForCurrentSize());
        }

        // ---------------------------------------------------------
        // Matrix streams
        // ---------------------------------------------------------
        for (auto &stream : streams)
        {
            stream.head += stream.speed * static_cast<float>(deltaTime);
            stream.mutationTimer += deltaTime;

            if (stream.mutationTimer >= stream.mutationInterval)
            {
                mutateStream(stream, rng);
                stream.mutationTimer = 0.0;
                stream.mutationInterval = mutationDistribution(rng);
            }

            if (stream.head - static_cast<float>(stream.length) > height)
            {
                stream.head = -static_cast<float>(
                    rng() % static_cast<unsigned>(std::max(1, height)));
                stream.length = lengthDistribution(rng);
                stream.speed = speedDistribution(rng);
                stream.mutationInterval = mutationDistribution(rng);
                initializeStream(stream, rng);
            }
        }

        // ---------------------------------------------------------
        // Falling metric tiles
        // ---------------------------------------------------------
        const int panelLeft = panelLeftForCurrentSize();
        const int tileRight = panelLeft > 0 ? panelLeft - 2 : width - 2;

        for (auto &tile : tiles)
        {
            tile.y += tile.speed * static_cast<float>(deltaTime);
            tile.phase += static_cast<float>(deltaTime) * 1.6f;

            tile.x +=
                std::sin(tile.phase) *
                0.20f *
                static_cast<float>(deltaTime) *
                10.0f;

            const float maxX = static_cast<float>(
                std::max(3, tileRight - tile.width));

            tile.x = std::clamp(tile.x, 3.0f, maxX);

            if (tile.y > height + 4)
            {
                tile.y = -static_cast<float>(4 + rng() % 18);

                const int spawnMax = std::max(
                    1,
                    tileRight - tile.width - 3);

                tile.x = static_cast<float>(
                    3 + rng() % static_cast<unsigned>(spawnMax));
            }
        }

        // ---------------------------------------------------------
        // Matrix buffer
        // ---------------------------------------------------------
        const int matrixHeight = std::max(1, height);

        std::vector<Cell> matrix(
            static_cast<size_t>(width) *
            static_cast<size_t>(matrixHeight));

        for (const auto &stream : streams)
        {
            if (stream.column < 0 ||
                stream.column >= width ||
                stream.characters.empty())
            {
                continue;
            }

            const int headRow = static_cast<int>(std::floor(stream.head));

            for (int i = 0; i < stream.length; ++i)
            {
                const int row = headRow - i;

                if (row < 0 || row >= matrixHeight)
                    continue;

                const float streamFade =
                    0.10f +
                    (1.0f -
                     static_cast<float>(i) /
                         static_cast<float>(std::max(1, stream.length - 1))) *
                        0.90f;

                const float finalBrightness =
                    streamFade * edgeFade(row, matrixHeight);

                Cell &cell = matrix[static_cast<size_t>(row * width + stream.column)];

                cell.character = stream.characters[static_cast<size_t>(i % static_cast<int>(stream.characters.size()))];

                cell.brightness = static_cast<unsigned char>(
                    std::clamp(
                        finalBrightness * 255.0f,
                        0.0f,
                        255.0f));
            }
        }

        // ---------------------------------------------------------
        // Render matrix
        // ---------------------------------------------------------
        frame.clear();
        frame += "\x1b[H";

        for (int row = 0; row < matrixHeight; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                const Cell &cell = matrix[static_cast<size_t>(row * width + column)];

                if (cell.character == ' ')
                {
                    frame += ' ';
                    continue;
                }

                int red = 0;
                int green = 0;
                int blue = 0;

                matrixColor(
                    currentColor,
                    static_cast<int>(cell.brightness),
                    red,
                    green,
                    blue);

                frame +=
                    "\x1b[38;2;" +
                    std::to_string(red) + ";" +
                    std::to_string(green) + ";" +
                    std::to_string(blue) + "m";

                frame += cell.character;
                frame += "\x1b[0m";
            }

            frame += '\n';
        }

        // ---------------------------------------------------------
        // Monitor panel + tiles
        // ---------------------------------------------------------
        const RGB accent = accentColor(currentColor);
        const RGB secondary = secondaryColor(currentColor);

        drawPanel(
            frame,
            width,
            height,
            metrics,
            accent,
            secondary,
            dashboardAlpha);

        if (dashboardAlpha > 0.01f)
        {
            for (const auto &tile : tiles)
            {
                drawTile(
                    frame,
                    tile,
                    width,
                    height,
                    accent,
                    secondary,
                    tilesAlpha);
            }
        }

        if (tileBurstTimer > 0.0f)
        {
            tileBurst(
                frame,
                width,
                height,
                secondary,
                rng,
                tileBurstTimer / tileBurstDuration);
        }

        if (glitchTimer > 0.0f)
        {
            glitch(
                frame,
                width,
                height,
                secondary,
                rng,
                glitchTimer / glitchDuration);
        }

        writeFrame(frame);

        // ---------------------------------------------------------
        // Frame pacing
        // ---------------------------------------------------------
        const auto frameEnd = Clock::now();
        const double renderTime =
            std::chrono::duration<double>(frameEnd - frameStart).count();

        const double remaining = targetFrameTime - renderTime;

        if (remaining > 0.0)
        {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(remaining));
        }
    }

    leaveScreen();
    restoreInput();
    SetConsoleCtrlHandler(consoleHandler, FALSE);
}
