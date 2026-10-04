#include "MatrixRain.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    std::string color = "green";

    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        const std::string prefix = "--color=";

        if (argument.rfind(prefix, 0) == 0)
        {
            color = argument.substr(prefix.size());
        }
        else if (argument == "--color")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --color requires a value.\n";
                return 1;
            }

            color = argv[++i];
        }
        else if (argument == "--help" || argument == "-h")
        {
            std::cout
                << "Matrix Monitor\n\n"
                << "Usage:\n"
                << "  MatrixMonitor\n"
                << "  MatrixMonitor --color=green\n"
                << "  MatrixMonitor --color=purple\n\n"
                << "Colors: green, blue, purple, red, cyan, white, amber\n\n"
                << "Controls:\n"
                << "  SPACE      Hide/show monitor panel + tiles\n"
                << "  BACKSPACE  Hide/show falling metric tiles\n"
                << "  Q / ESC    Exit\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << argument << "\n";
            return 1;
        }
    }

    MatrixRain matrix(color);
    matrix.run();
    return 0;
}
