#pragma once

#include <string>

class MatrixRain
{
public:
    explicit MatrixRain(const std::string &color = "green");
    void run();

private:
    std::string colorName;
};
