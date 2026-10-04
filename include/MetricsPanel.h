#pragma once

#include <string>
#include "SystemMetrics.h"

class MetricsPanel
{
public:
    void render(
        std::string &frame,
        const SystemMetrics::Snapshot &metrics,
        double fps,
        int terminalWidth,
        int terminalHeight,
        const std::string &theme);

private:
    void appendAt(
        std::string &frame,
        int row,
        int column,
        const std::string &text);

    void drawBox(
        std::string &frame,
        int top,
        int left,
        int width,
        int height,
        const std::string &color,
        const std::string &title = "");

    void drawCard(
        std::string &frame,
        int top,
        int left,
        const std::string &title,
        const std::string &value,
        double percent,
        const std::string &color,
        const std::string &dimColor);

    std::string makeBar(
        double value,
        int width);

    std::string color(
        int r,
        int g,
        int b);

    std::string dimColor(
        int r,
        int g,
        int b);

    std::string boldColor(
        int r,
        int g,
        int b);
};