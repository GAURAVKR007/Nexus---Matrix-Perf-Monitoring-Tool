#include "MatrixRain.h"
#include "SystemMetrics.h"

#define NOMINMAX
#include <windows.h>
#include <conio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
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
        float speed = 15.0f;

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

        float speed = 5.0f;
        float phase = 0.0f;

        int width = 25;
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
            return {220, 130, 255};
        case MatrixColor::Blue:
            return {100, 190, 255};
        case MatrixColor::Cyan:
            return {90, 255, 235};
        case MatrixColor::Red:
            return {255, 120, 130};
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
            return {95, 220, 255};
        case MatrixColor::Blue:
            return {190, 120, 255};
        case MatrixColor::Cyan:
            return {210, 150, 255};
        case MatrixColor::Red:
            return {255, 205, 100};
        case MatrixColor::White:
            return {130, 220, 255};
        case MatrixColor::Amber:
            return {90, 220, 190};
        default:
            return {100, 220, 255};
        }
    }

    void matrixColor(
        MatrixColor color,
        int brightness,
        int &r,
        int &g,
        int &b)
    {
        brightness =
            std::clamp(
                brightness,
                0,
                255);

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
            static_cast<int>(
                sizeof(characters) - 2));

        return characters[distribution(rng)];
    }

    void initializeStream(
        Stream &stream,
        std::mt19937 &rng)
    {
        stream.characters.resize(
            static_cast<size_t>(
                stream.length));

        for (char &character :
             stream.characters)
        {
            character =
                randomCharacter(rng);
        }
    }

    void mutateStream(
        Stream &stream,
        std::mt19937 &rng)
    {
        if (stream.characters.empty())
            return;

        std::uniform_int_distribution<int>
            distribution(
                0,
                static_cast<int>(
                    stream.characters.size()) -
                    1);

        stream.characters[static_cast<size_t>(
            distribution(rng))] =
            randomCharacter(rng);
    }

    bool terminalSize(
        int &width,
        int &height)
    {
        HANDLE output =
            GetStdHandle(
                STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE)
            return false;

        CONSOLE_SCREEN_BUFFER_INFO info{};

        if (!GetConsoleScreenBufferInfo(
                output,
                &info))
        {
            return false;
        }

        width =
            info.srWindow.Right -
            info.srWindow.Left +
            1;

        height =
            info.srWindow.Bottom -
            info.srWindow.Top +
            1;

        return width > 0 && height > 0;
    }

    bool enableVT()
    {
        HANDLE output =
            GetStdHandle(
                STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE)
            return false;

        DWORD mode = 0;

        if (!GetConsoleMode(
                output,
                &mode))
        {
            return false;
        }

        mode |=
            ENABLE_VIRTUAL_TERMINAL_PROCESSING;

        return SetConsoleMode(
                   output,
                   mode) != FALSE;
    }

    void configureInput()
    {
        HANDLE input =
            GetStdHandle(
                STD_INPUT_HANDLE);

        if (input == INVALID_HANDLE_VALUE)
            return;

        DWORD mode = 0;

        if (!GetConsoleMode(
                input,
                &mode))
        {
            return;
        }

        g_originalInputMode = mode;
        g_inputModeSaved = true;

        mode &= ~ENABLE_LINE_INPUT;
        mode &= ~ENABLE_ECHO_INPUT;

        SetConsoleMode(
            input,
            mode);
    }

    void restoreInput()
    {
        if (!g_inputModeSaved)
            return;

        HANDLE input =
            GetStdHandle(
                STD_INPUT_HANDLE);

        if (input != INVALID_HANDLE_VALUE)
        {
            SetConsoleMode(
                input,
                g_originalInputMode);
        }

        g_inputModeSaved = false;
    }

    void writeFrame(
        const std::string &frame)
    {
        HANDLE output =
            GetStdHandle(
                STD_OUTPUT_HANDLE);

        if (output == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;

        WriteConsoleA(
            output,
            frame.data(),
            static_cast<DWORD>(
                frame.size()),
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
        t = std::clamp(
            t,
            0.0f,
            1.0f);

        return t * t * (3.0f - 2.0f * t);
    }

    float edgeFade(
        int row,
        int height)
    {
        if (height <= 1)
            return 1.0f;

        const float position =
            static_cast<float>(row) /
            static_cast<float>(height - 1);

        constexpr float edge = 0.13f;
        constexpr float minimum = 0.14f;

        if (position < edge)
        {
            const float t =
                smoothStep(
                    position / edge);

            return minimum +
                   (1.0f - minimum) * t;
        }

        if (position > 1.0f - edge)
        {
            const float t =
                smoothStep(
                    (1.0f - position) / edge);

            return minimum +
                   (1.0f - minimum) * t;
        }

        return 1.0f;
    }

    std::string ansi(
        RGB color,
        float alpha = 1.0f)
    {
        alpha =
            std::clamp(
                alpha,
                0.0f,
                1.0f);

        return "\x1b[38;2;" +
               std::to_string(
                   static_cast<int>(
                       color.r * alpha)) +
               ";" +
               std::to_string(
                   static_cast<int>(
                       color.g * alpha)) +
               ";" +
               std::to_string(
                   static_cast<int>(
                       color.b * alpha)) +
               "m";
    }

    std::string fit(
        const std::string &text,
        int width)
    {
        if (width <= 0)
            return {};

        if (static_cast<int>(
                text.size()) > width)
        {
            return text.substr(
                0,
                static_cast<size_t>(
                    width));
        }

        return text +
               std::string(
                   static_cast<size_t>(
                       width - text.size()),
                   ' ');
    }

    std::string fitEllipsis(
        const std::string &text,
        int width)
    {
        if (width <= 0)
            return {};

        if (static_cast<int>(text.size()) <= width)
            return text;

        if (width <= 3)
            return text.substr(
                0,
                static_cast<size_t>(width));

        return text.substr(
                   0,
                   static_cast<size_t>(width - 3)) +
               "...";
    }

    int dashboardPanelWidth(int terminalWidth)
    {
        if (terminalWidth <= 0)
            return 30;

        const int maximum =
            std::max(30, terminalWidth - 4);

        const int preferred =
            static_cast<int>(
                std::round(terminalWidth * 0.29));

        return std::clamp(
            preferred,
            30,
            std::min(50, maximum));
    }

    void at(
        std::string &frame,
        int row,
        int column,
        const std::string &text)
    {
        if (row < 1 || column < 1)
            return;

        frame +=
            "\x1b[" +
            std::to_string(row) +
            ";" +
            std::to_string(column) +
            "H" +
            text;
    }

    std::string bar(
        double value,
        int width)
    {
        value =
            std::clamp(
                value,
                0.0,
                100.0);

        width =
            std::max(
                width,
                1);

        const int filled =
            static_cast<int>(
                std::round(
                    (value / 100.0) *
                    width));

        return "[" +
               std::string(
                   static_cast<size_t>(filled),
                   '#') +
               std::string(
                   static_cast<size_t>(
                       width - filled),
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

        const int x =
            static_cast<int>(
                std::round(tile.x));

        const int y =
            static_cast<int>(
                std::round(tile.y));

        const int tileWidth =
            tile.width;

        if (x < 2 ||
            x + tileWidth >= width - 1 ||
            y > height ||
            y + 2 < 1)
        {
            return;
        }

        const std::string top =
            "+" +
            std::string(
                static_cast<size_t>(
                    tileWidth - 2),
                '-') +
            "+";

        const std::string middle =
            "| " +
            fit(
                tile.label +
                    "  " +
                    tile.value,
                tileWidth - 4) +
            " |";

        const std::string bottom =
            "+" +
            std::string(
                static_cast<size_t>(
                    tileWidth - 2),
                '-') +
            "+";

        at(
            frame,
            y,
            x,
            ansi(
                secondary,
                alpha * 0.55f) +
                top +
                "\x1b[0m");

        at(
            frame,
            y + 1,
            x,
            ansi(
                accent,
                alpha) +
                middle +
                "\x1b[0m");

        at(
            frame,
            y + 2,
            x,
            ansi(
                secondary,
                alpha * 0.35f) +
                bottom +
                "\x1b[0m");
    }

    void glitch(
        std::string &frame,
        int width,
        int height,
        RGB color,
        std::mt19937 &rng,
        float strength)
    {
        if (strength <= 0.01f ||
            width < 30 ||
            height < 8)
        {
            return;
        }

        static constexpr char characters[] =
            "01X#<>/\\|+*";

        std::uniform_int_distribution<int>
            rows(2, height - 2);

        std::uniform_int_distribution<int>
            columns(
                2,
                std::max(
                    2,
                    width - 18));

        std::uniform_int_distribution<int>
            lengths(2, 12);

        std::uniform_int_distribution<int>
            glyph(
                0,
                static_cast<int>(
                    sizeof(characters) - 2));

        const int count =
            static_cast<int>(
                8 +
                strength * 26.0f);

        for (int i = 0;
             i < count;
             ++i)
        {
            std::string text;

            const int length =
                lengths(rng);

            for (int j = 0;
                 j < length;
                 ++j)
            {
                text +=
                    characters[glyph(rng)];
            }

            at(
                frame,
                rows(rng),
                columns(rng),
                ansi(
                    color,
                    0.25f +
                        strength * 0.70f) +
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
        if (strength <= 0.01f ||
            width < 30 ||
            height < 8)
        {
            return;
        }

        const int zoneWidth =
            std::max(
                20,
                static_cast<int>(
                    width * 0.70f));

        std::uniform_int_distribution<int>
            rowDist(2, std::max(2, height - 2));

        std::uniform_int_distribution<int>
            colDist(2, std::max(2, zoneWidth - 12));

        std::uniform_int_distribution<int>
            lengthDist(3, 16);

        static constexpr char glyphs[] =
            "01X#<>/\\|+*";

        std::uniform_int_distribution<int>
            glyphDist(
                0,
                static_cast<int>(sizeof(glyphs) - 2));

        const int count =
            10 + static_cast<int>(strength * 34.0f);

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
                ansi(
                    color,
                    0.15f + strength * 0.80f) +
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
                    std::to_string(
                        static_cast<int>(
                            std::round(
                                metrics.cpuUsage))) +
                    "%";
            }
            else if (tile.label == "RAM")
            {
                tile.value =
                    std::to_string(
                        static_cast<int>(
                            std::round(
                                metrics.memoryUsage))) +
                    "%";
            }
            else if (tile.label == "IP")
            {
                tile.value =
                    metrics.localIP;
            }
            else if (tile.label == "HOST")
            {
                tile.value =
                    metrics.hostName;
            }
            else if (tile.label == "UPTIME")
            {
                tile.value =
                    metrics.uptime;
            }
            else if (tile.label == "POWER")
            {
                tile.value =
                    metrics.powerState;
            }
            else if (tile.label == "BATTERY")
            {
                if (metrics.batteryPercent >= 0)
                {
                    tile.value =
                        std::to_string(
                            metrics.batteryPercent) +
                        "%";
                }
                else
                {
                    tile.value.clear();
                }
            }
            else if (tile.label == "STORAGE")
            {
                tile.value =
                    std::to_string(
                        static_cast<int>(
                            std::round(metrics.storageUsedGB))) +
                    "/" +
                    std::to_string(
                        static_cast<int>(
                            std::round(metrics.storageTotalGB))) +
                    " GB";
            }
            else if (tile.label == "GPU")
            {
                tile.value =
                    metrics.gpuName;
            }
            else if (tile.label == "PUBLIC IP")
            {
                tile.value =
                    metrics.publicIP;
            }
        }
    }
}

MatrixRain::MatrixRain(
    const std::string &color)
    : colorName(color)
{
}

void MatrixRain::run()
{
    g_running.store(true);

    const MatrixColor currentColor =
        parseColor(colorName);

    if (!SetConsoleCtrlHandler(
            consoleHandler,
            TRUE))
    {
        return;
    }

    if (!enableVT())
    {
        SetConsoleCtrlHandler(
            consoleHandler,
            FALSE);
        return;
    }

    configureInput();
    enterScreen();

    std::random_device randomDevice;
    std::mt19937 rng(
        randomDevice());

    std::uniform_int_distribution<int>
        lengthDistribution(8, 19);

    std::uniform_real_distribution<float>
        speedDistribution(12.0f, 23.0f);

    std::uniform_real_distribution<double>
        mutationDistribution(0.045, 0.085);

    int width = 0;
    int height = 0;

    if (!terminalSize(
            width,
            height))
    {
        leaveScreen();
        restoreInput();

        SetConsoleCtrlHandler(
            consoleHandler,
            FALSE);

        return;
    }

    std::vector<Stream> streams;

    auto createStream =
        [&](int column)
    {
        Stream stream;

        stream.column = column;

        stream.head =
            -static_cast<float>(
                rng() %
                static_cast<unsigned>(
                    std::max(
                        1,
                        height)));

        stream.length =
            lengthDistribution(rng);

        stream.speed =
            speedDistribution(rng);

        stream.mutationInterval =
            mutationDistribution(rng);

        initializeStream(
            stream,
            rng);

        streams.push_back(
            std::move(stream));
    };

    for (int column = 0;
         column < width;
         column += 2)
    {
        createStream(column);
    }

    // =============================================================
    // Live metrics
    // =============================================================

    SystemMetrics systemMetrics;

    systemMetrics.update();

    auto metrics =
        systemMetrics.getSnapshot();

    std::vector<Tile> tiles =
        {
            {"CPU", "", 8.0f, -5.0f, 4.2f, 0.2f, 25},
            {"RAM", "", 31.0f, -17.0f, 5.0f, 1.1f, 25},
            {"STORAGE", "", 57.0f, -29.0f, 4.5f, 2.1f, 29},
            {"GPU", "", 88.0f, -41.0f, 3.8f, 3.0f, 32},
            {"PUBLIC IP", "", 119.0f, -53.0f, 4.6f, 4.2f, 29},
            {"UPTIME", "", 150.0f, -65.0f, 4.5f, 5.0f, 27},
            {"HOST", "", 181.0f, -77.0f, 4.1f, 5.8f, 29},
            {"POWER", "", 212.0f, -89.0f, 5.0f, 6.6f, 27},
            {"DEVELOPED BY:", "Gaurav", 243.0f, -101.0f, 4.7f, 7.4f, 29}};

    updateTileValues(
        tiles,
        metrics);

    bool dashboardVisible = true;
    float dashboardAlpha = 1.0f;

    constexpr float glitchDuration = 0.48f;
    float glitchTimer = 0.0f;

    constexpr float tileBurstDuration = 0.42f;
    float tileBurstTimer = 0.0f;
    bool tilesVisible = true;
    float tilesAlpha = 1.0f;

    using Clock =
        std::chrono::steady_clock;

    constexpr double targetFPS = 165.0;
    constexpr double targetFrameTime =
        1.0 / targetFPS;

    auto previousTime =
        Clock::now();

    auto lastMetricsUpdate =
        Clock::now();

    std::string frame;

    while (g_running.load())
    {
        const auto frameStart =
            Clock::now();

        std::chrono::duration<double>
            elapsed =
                frameStart -
                previousTime;

        previousTime =
            frameStart;

        double deltaTime =
            elapsed.count();

        if (deltaTime > 0.1)
            deltaTime = 0.1;

        // =========================================================
        // Keyboard
        // =========================================================

        while (_kbhit())
        {
            const int key =
                _getch();

            if (key == ' ')
            {
                dashboardVisible =
                    !dashboardVisible;

                glitchTimer =
                    glitchDuration;
            }
            else if (key == 8)
            {
                tilesVisible =
                    !tilesVisible;

                tileBurstTimer =
                    tileBurstDuration;
            }
            else if (
                key == 'q' ||
                key == 'Q' ||
                key == 27)
            {
                g_running.store(false);
            }
        }

        // =========================================================
        // Dashboard transition
        // =========================================================

        const float transitionSpeed =
            1.0f / 0.34f;

        if (dashboardVisible)
        {
            dashboardAlpha =
                std::min(
                    1.0f,
                    dashboardAlpha +
                        static_cast<float>(
                            deltaTime) *
                            transitionSpeed);
        }
        else
        {
            dashboardAlpha =
                std::max(
                    0.0f,
                    dashboardAlpha -
                        static_cast<float>(
                            deltaTime) *
                            transitionSpeed);
        }

        glitchTimer =
            std::max(
                0.0f,
                glitchTimer -
                    static_cast<float>(
                        deltaTime));

        tileBurstTimer =
            std::max(
                0.0f,
                tileBurstTimer -
                    static_cast<float>(
                        deltaTime));

        const float tileTransitionSpeed =
            1.0f / 0.28f;

        if (tilesVisible)
        {
            tilesAlpha =
                std::min(
                    1.0f,
                    tilesAlpha +
                        static_cast<float>(deltaTime) *
                            tileTransitionSpeed);
        }
        else
        {
            tilesAlpha =
                std::max(
                    0.0f,
                    tilesAlpha -
                        static_cast<float>(deltaTime) *
                            tileTransitionSpeed);
        }

        // =========================================================
        // Live metrics
        // =========================================================

        const auto now =
            Clock::now();

        if (std::chrono::duration<double>(
                now -
                lastMetricsUpdate)
                .count() >= 0.5)
        {
            systemMetrics.update();

            metrics =
                systemMetrics.getSnapshot();

            updateTileValues(
                tiles,
                metrics);

            lastMetricsUpdate =
                now;
        }

        // =========================================================
        // Resize
        // =========================================================

        int newWidth = 0;
        int newHeight = 0;

        if (terminalSize(
                newWidth,
                newHeight) &&
            (newWidth != width ||
             newHeight != height))
        {
            const int oldWidth =
                width;

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

            int firstNewColumn =
                std::max(
                    0,
                    oldWidth);

            if (firstNewColumn % 2 != 0)
                ++firstNewColumn;

            for (int column =
                     firstNewColumn;
                 column < width;
                 column += 2)
            {
                createStream(column);
            }

            for (auto &stream :
                 streams)
            {
                if (stream.head -
                        static_cast<float>(
                            stream.length) >
                    height)
                {
                    stream.head =
                        -static_cast<float>(
                            rng() %
                            static_cast<unsigned>(
                                std::max(
                                    1,
                                    height)));
                }
            }
        }

        // =========================================================
        // Matrix streams
        // =========================================================

        for (auto &stream :
             streams)
        {
            stream.head +=
                stream.speed *
                static_cast<float>(
                    deltaTime);

            stream.mutationTimer +=
                deltaTime;

            if (stream.mutationTimer >=
                stream.mutationInterval)
            {
                mutateStream(
                    stream,
                    rng);

                stream.mutationTimer = 0.0;

                stream.mutationInterval =
                    mutationDistribution(rng);
            }

            if (stream.head -
                    static_cast<float>(
                        stream.length) >
                height)
            {
                stream.head =
                    -static_cast<float>(
                        rng() %
                        static_cast<unsigned>(
                            std::max(
                                1,
                                height)));

                stream.length =
                    lengthDistribution(rng);

                stream.speed =
                    speedDistribution(rng);

                stream.mutationInterval =
                    mutationDistribution(rng);

                initializeStream(
                    stream,
                    rng);
            }
        }

        // =========================================================
        // Falling tiles
        // =========================================================

        const int panelWidthForLayout =
            dashboardPanelWidth(width);

        const int panelLeftForLayout =
            width -
            panelWidthForLayout -
            2;

        float tileZone =
            width * 0.70f;

        if (dashboardVisible)
        {
            // Keep falling tiles out of the right-side panel.
            tileZone =
                std::min(
                    tileZone,
                    static_cast<float>(
                        std::max(24,
                                 panelLeftForLayout - 3)));
        }

        const float maximumTileZone =
            dashboardVisible
                ? static_cast<float>(
                      std::max(10,
                               panelLeftForLayout - 3))
                : static_cast<float>(width - 3);

        tileZone =
            std::clamp(
                tileZone,
                10.0f,
                std::max(10.0f,
                         maximumTileZone));

        for (auto &tile :
             tiles)
        {
            tile.y +=
                tile.speed *
                static_cast<float>(
                    deltaTime);

            tile.phase +=
                static_cast<float>(
                    deltaTime) *
                1.6f;

            tile.x +=
                std::sin(
                    tile.phase) *
                0.18f *
                static_cast<float>(
                    deltaTime) *
                10.0f;

            tile.x =
                std::clamp(
                    tile.x,
                    2.0f,
                    std::max(
                        2.0f,
                        tileZone -
                            static_cast<float>(
                                tile.width) -
                            2.0f));

            if (tile.y >
                height + 4)
            {
                tile.y =
                    -static_cast<float>(
                        4 +
                        rng() % 18);

                const int maxX =
                    std::max(
                        8,
                        static_cast<int>(
                            tileZone -
                            tile.width -
                            3));

                tile.x =
                    static_cast<float>(
                        3 +
                        rng() %
                            static_cast<unsigned>(
                                maxX));
            }
        }

        // =========================================================
        // Matrix buffer
        // =========================================================

        const int matrixHeight =
            std::max(
                1,
                height);

        std::vector<Cell> matrix(
            static_cast<size_t>(
                width) *
            static_cast<size_t>(
                matrixHeight));

        for (const auto &stream :
             streams)
        {
            if (stream.column < 0 ||
                stream.column >= width ||
                stream.characters.empty())
            {
                continue;
            }

            const int headRow =
                static_cast<int>(
                    std::floor(
                        stream.head));

            for (int i = 0;
                 i < stream.length;
                 ++i)
            {
                const int row =
                    headRow - i;

                if (row < 0 ||
                    row >= matrixHeight)
                {
                    continue;
                }

                const float streamFade =
                    0.12f +
                    (1.0f -
                     static_cast<float>(i) /
                         static_cast<float>(
                             std::max(
                                 1,
                                 stream.length - 1))) *
                        0.88f;

                const float finalBrightness =
                    streamFade *
                    edgeFade(
                        row,
                        matrixHeight);

                Cell &cell =
                    matrix[static_cast<size_t>(
                        row * width +
                        stream.column)];

                cell.character =
                    stream.characters[static_cast<size_t>(
                        i %
                        static_cast<int>(
                            stream.characters.size()))];

                cell.brightness =
                    static_cast<unsigned char>(
                        std::clamp(
                            finalBrightness *
                                255.0f,
                            0.0f,
                            255.0f));
            }
        }

        // =========================================================
        // Render
        // =========================================================

        frame.clear();
        frame += "\x1b[H";

        for (int row = 0;
             row < matrixHeight;
             ++row)
        {
            for (int column = 0;
                 column < width;
                 ++column)
            {
                const Cell &cell =
                    matrix[static_cast<size_t>(
                        row * width +
                        column)];

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
                    static_cast<int>(
                        cell.brightness),
                    red,
                    green,
                    blue);

                frame +=
                    "\x1b[38;2;" +
                    std::to_string(red) +
                    ";" +
                    std::to_string(green) +
                    ";" +
                    std::to_string(blue) +
                    "m";

                frame += cell.character;
                frame += "\x1b[0m";
            }

            frame += '\n';
        }

        // =========================================================
        // Responsive Dashboard
        // =========================================================

        const RGB accent =
            accentColor(
                currentColor);

        const RGB secondary =
            secondaryColor(
                currentColor);

        if (dashboardAlpha > 0.01f)
        {
            const int panelWidth =
                dashboardPanelWidth(width);

            const int left =
                width -
                panelWidth -
                2;

            // -----------------------------------------------------
            // Choose a layout tier from the actual terminal size.
            // Nothing is scaled down: lower-priority information is
            // removed as the terminal becomes smaller.
            // -----------------------------------------------------

            const bool compact =
                width < 92 ||
                height < 25 ||
                panelWidth < 35;

            const bool medium =
                !compact &&
                (width < 120 ||
                 height < 35 ||
                 panelWidth < 43);

            const int panelHeight =
                compact ? 16 : medium ? 24
                                      : 33;

            if (left >= 2 &&
                panelWidth >= 28 &&
                height >= panelHeight + 2)
            {
                const int innerWidth =
                    panelWidth - 2;

                const std::string border =
                    "+" +
                    std::string(
                        static_cast<size_t>(
                            innerWidth),
                        '-') +
                    "+";

                auto panelLine =
                    [&](int row,
                        const std::string &text,
                        float alpha)
                {
                    if (row < 0 ||
                        row > panelHeight - 2)
                    {
                        return;
                    }

                    at(
                        frame,
                        2 + row,
                        left,
                        ansi(
                            accent,
                            alpha) +
                            "|" +
                            fit(
                                " " + text,
                                innerWidth) +
                            "|" +
                            "\x1b[0m");
                };

                auto drawBottomBorder =
                    [&]()
                {
                    at(
                        frame,
                        2 + panelHeight - 1,
                        left,
                        ansi(
                            secondary,
                            dashboardAlpha * 0.60f) +
                            border +
                            "\x1b[0m");
                };

                // -------------------------------------------------
                // Outer frame + title
                // -------------------------------------------------

                at(
                    frame,
                    2,
                    left,
                    ansi(
                        accent,
                        dashboardAlpha) +
                        border +
                        "\x1b[0m");

                panelLine(
                    1,
                    compact
                        ? "NEXUS // CORE"
                        : "NEXUS // SYSTEM CORE",
                    dashboardAlpha);

                panelLine(
                    2,
                    "----------------------------------------------",
                    dashboardAlpha * 0.55f);

                if (compact)
                {
                    // ---------------------------------------------
                    // COMPACT
                    // ---------------------------------------------
                    // Always keep the most useful live information.

                    panelLine(
                        4,
                        "CPU        " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.cpuUsage))) +
                            "%",
                        dashboardAlpha);

                    panelLine(
                        5,
                        "RAM        " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.memoryUsage))) +
                            "%",
                        dashboardAlpha);

                    panelLine(
                        7,
                        "STORAGE    " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.storageUsedGB))) +
                            "/" +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.storageTotalGB))) +
                            " GB",
                        dashboardAlpha);

                    panelLine(
                        8,
                        "GPU        " +
                            fitEllipsis(
                                metrics.gpuName,
                                std::max(
                                    8,
                                    innerWidth - 5)),
                        dashboardAlpha);

                    panelLine(
                        9,
                        "LOCAL IP   " +
                            fitEllipsis(
                                metrics.localIP,
                                std::max(
                                    8,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    panelLine(
                        11,
                        "HOST       " +
                            fitEllipsis(
                                metrics.hostName,
                                std::max(
                                    8,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    std::string power =
                        "POWER      " +
                        metrics.powerState;

                    if (metrics.batteryPercent >= 0)
                    {
                        power +=
                            " " +
                            std::to_string(
                                metrics.batteryPercent) +
                            "%";
                    }

                    panelLine(
                        12,
                        fitEllipsis(
                            power,
                            innerWidth - 1),
                        dashboardAlpha);

                    panelLine(
                        14,
                        "Q / ESC    EXIT",
                        dashboardAlpha * 0.70f);
                }
                else if (medium)
                {
                    // ---------------------------------------------
                    // MEDIUM
                    // ---------------------------------------------
                    // More information, but no unnecessary section
                    // decoration or controls that consume height.

                    panelLine(
                        4,
                        "CPU        " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.cpuUsage))) +
                            "%",
                        dashboardAlpha);

                    panelLine(
                        5,
                        " " +
                            bar(
                                metrics.cpuUsage,
                                std::max(
                                    10,
                                    innerWidth - 2)),
                        dashboardAlpha * 0.70f);

                    panelLine(
                        7,
                        "RAM        " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.memoryUsage))) +
                            "%",
                        dashboardAlpha);

                    panelLine(
                        8,
                        " " +
                            bar(
                                metrics.memoryUsage,
                                std::max(
                                    10,
                                    innerWidth - 2)),
                        dashboardAlpha * 0.70f);

                    panelLine(
                        10,
                        "STORAGE    " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.storageUsedGB))) +
                            " / " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.storageTotalGB))) +
                            " GB",
                        dashboardAlpha);

                    panelLine(
                        11,
                        " " +
                            bar(
                                metrics.storageUsage,
                                std::max(
                                    10,
                                    innerWidth - 2)),
                        dashboardAlpha * 0.70f);

                    panelLine(
                        13,
                        "GPU        " +
                            fitEllipsis(
                                metrics.gpuName,
                                std::max(
                                    10,
                                    innerWidth - 5)),
                        dashboardAlpha);

                    panelLine(
                        14,
                        "VRAM       " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.gpuMemoryGB))) +
                            " GB",
                        dashboardAlpha);

                    panelLine(
                        16,
                        "LOCAL IP   " +
                            fitEllipsis(
                                metrics.localIP,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    panelLine(
                        17,
                        "HOST       " +
                            fitEllipsis(
                                metrics.hostName,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    panelLine(
                        18,
                        "UPTIME     " +
                            fitEllipsis(
                                metrics.uptime,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    std::string power =
                        "POWER      " +
                        metrics.powerState;

                    if (metrics.batteryPercent >= 0)
                    {
                        power +=
                            " " +
                            std::to_string(
                                metrics.batteryPercent) +
                            "%";
                    }

                    panelLine(
                        20,
                        fitEllipsis(
                            power,
                            innerWidth - 1),
                        dashboardAlpha);

                    panelLine(
                        22,
                        "SPACE HIDE/SHOW   Q EXIT",
                        dashboardAlpha * 0.70f);
                }
                else
                {
                    // ---------------------------------------------
                    // FULL
                    // ---------------------------------------------
                    // Everything useful is visible when there is
                    // enough terminal real estate.

                    panelLine(
                        4,
                        "SYSTEM STATUS",
                        dashboardAlpha);

                    panelLine(
                        5,
                        "STATUS     ONLINE",
                        dashboardAlpha);

                    panelLine(
                        7,
                        "CPU        " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.cpuUsage))) +
                            "%",
                        dashboardAlpha);

                    panelLine(
                        8,
                        " " +
                            bar(
                                metrics.cpuUsage,
                                innerWidth - 2),
                        dashboardAlpha * 0.75f);

                    panelLine(
                        10,
                        "RAM        " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.memoryUsage))) +
                            "%",
                        dashboardAlpha);

                    panelLine(
                        11,
                        " " +
                            bar(
                                metrics.memoryUsage,
                                innerWidth - 2),
                        dashboardAlpha * 0.75f);

                    panelLine(
                        13,
                        "STORAGE    " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.storageUsedGB))) +
                            " / " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.storageTotalGB))) +
                            " GB",
                        dashboardAlpha);

                    panelLine(
                        14,
                        " " +
                            bar(
                                metrics.storageUsage,
                                innerWidth - 2),
                        dashboardAlpha * 0.75f);

                    panelLine(
                        16,
                        "GRAPHICS",
                        dashboardAlpha);

                    panelLine(
                        17,
                        "GPU        " +
                            fitEllipsis(
                                metrics.gpuName,
                                std::max(
                                    10,
                                    innerWidth - 5)),
                        dashboardAlpha);

                    panelLine(
                        18,
                        "VRAM       " +
                            std::to_string(
                                static_cast<int>(
                                    std::round(
                                        metrics.gpuMemoryGB))) +
                            " GB",
                        dashboardAlpha);

                    panelLine(
                        20,
                        "NETWORK",
                        dashboardAlpha);

                    panelLine(
                        21,
                        "LOCAL IP   " +
                            fitEllipsis(
                                metrics.localIP,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    panelLine(
                        22,
                        "PUBLIC IP  " +
                            fitEllipsis(
                                metrics.publicIP,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    panelLine(
                        24,
                        "SYSTEM",
                        dashboardAlpha);

                    panelLine(
                        25,
                        "HOST       " +
                            fitEllipsis(
                                metrics.hostName,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    panelLine(
                        26,
                        "UPTIME     " +
                            fitEllipsis(
                                metrics.uptime,
                                std::max(
                                    10,
                                    innerWidth - 12)),
                        dashboardAlpha);

                    std::string power =
                        "POWER      " +
                        metrics.powerState;

                    if (metrics.batteryPercent >= 0)
                    {
                        power +=
                            " " +
                            std::to_string(
                                metrics.batteryPercent) +
                            "%";
                    }

                    panelLine(
                        27,
                        fitEllipsis(
                            power,
                            innerWidth - 1),
                        dashboardAlpha);

                    panelLine(
                        29,
                        "SPACE      HIDE / SHOW",
                        dashboardAlpha * 0.80f);

                    panelLine(
                        30,
                        "BACKSPACE  TILES ON / OFF",
                        dashboardAlpha * 0.80f);

                    panelLine(
                        31,
                        "Q / ESC    EXIT",
                        dashboardAlpha * 0.65f);
                }

                drawBottomBorder();
            }

            // -----------------------------------------------------
            // Falling tiles
            // -----------------------------------------------------

            for (const auto &tile :
                 tiles)
            {
                drawTile(
                    frame,
                    tile,
                    width,
                    height,
                    accent,
                    secondary,
                    dashboardAlpha * tilesAlpha);
            }
        }

        // ---------------------------------------------------------
        // Tile burst transition
        // ---------------------------------------------------------

        if (tileBurstTimer > 0.0f &&
            dashboardAlpha > 0.01f)
        {
            tileBurst(
                frame,
                width,
                height,
                secondary,
                rng,
                tileBurstTimer / tileBurstDuration);
        }

        // ---------------------------------------------------------
        // Glitch transition
        // ---------------------------------------------------------

        if (glitchTimer > 0.0f)
        {
            glitch(
                frame,
                width,
                height,
                secondary,
                rng,
                glitchTimer /
                    glitchDuration);
        }

        writeFrame(frame);

        // =========================================================
        // Frame pacing
        // =========================================================

        const auto frameEnd =
            Clock::now();

        const double renderTime =
            std::chrono::duration<double>(
                frameEnd -
                frameStart)
                .count();

        const double remaining =
            targetFrameTime -
            renderTime;

        if (remaining > 0.0)
        {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(
                    remaining));
        }
    }

    leaveScreen();
    restoreInput();

    SetConsoleCtrlHandler(
        consoleHandler,
        FALSE);
}
