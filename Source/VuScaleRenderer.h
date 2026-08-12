#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

class VuScaleRenderer
{
public:
    enum class Mode
    {
        level,
        gainReduction
    };

    struct Mark
    {
        float db;
        float degrees;
        const char* label;
    };

    static constexpr std::array<Mark, 9> marks {{
        { -20.0f, 202.00f, "-20" },
        { -10.0f, 239.00f, "-10" },
        {  -7.0f, 254.00f, "-7" },
        {  -5.0f, 266.00f, "-5" },
        {  -3.0f, 281.00f, "-3" },
        {   0.0f, 301.00f, "0" },
        {   1.0f, 313.00f, "+1" },
        {   2.0f, 325.00f, "+2" },
        {   3.0f, 338.00f, "+3" }
    }};

    static constexpr std::array<Mark, 8> gainReductionMarks {{
        { -20.0f, 202.0f, "20" },
        { -10.0f, 239.0f, "10" },
        {  -7.0f, 254.0f, "7" },
        {  -5.0f, 270.0f, "5" },
        {  -3.0f, 292.0f, "3" },
        {  -2.0f, 306.0f, "2" },
        {  -1.0f, 321.0f, "1" },
        {   0.0f, 338.0f, "0" }
    }};

    static float angleForVu(float db)
    {
        // Values below the lowest printed mark continue into the unlabelled
        // left-hand rest area, so downstream gain changes remain visible.
        if (db < marks.front().db)
            return juce::degreesToRadians(juce::jmap(juce::jlimit(-60.0f, marks.front().db, db),
                                                     -60.0f, marks.front().db, 190.0f, marks.front().degrees));

        const float clamped = juce::jlimit(marks.front().db, marks.back().db, db);
        for (size_t i = 0; i + 1 < marks.size(); ++i)
        {
            if (clamped <= marks[i + 1].db)
            {
                const float t = juce::jmap(clamped, marks[i].db, marks[i + 1].db, 0.0f, 1.0f);
                return juce::degreesToRadians(juce::jmap(t, marks[i].degrees, marks[i + 1].degrees));
            }
        }
        return juce::degreesToRadians(marks.back().degrees);
    }

    static float angleForGainReduction(float db)
    {
        const float clamped = juce::jlimit(gainReductionMarks.front().db, gainReductionMarks.back().db, db);
        for (size_t i = 0; i + 1 < gainReductionMarks.size(); ++i)
        {
            if (clamped <= gainReductionMarks[i + 1].db)
            {
                const float t = juce::jmap(clamped, gainReductionMarks[i].db, gainReductionMarks[i + 1].db, 0.0f, 1.0f);
                return juce::degreesToRadians(juce::jmap(t, gainReductionMarks[i].degrees,
                                                          gainReductionMarks[i + 1].degrees));
            }
        }

        return juce::degreesToRadians(gainReductionMarks.back().degrees);
    }

    static void draw(juce::Graphics& g, juce::Rectangle<float> bounds, Mode mode = Mode::level)
    {
        constexpr float designWidth = 330.0f;
        constexpr float designHeight = 168.0f;
        const float sx = bounds.getWidth() / designWidth;
        const float sy = bounds.getHeight() / designHeight;
        const auto p = [=](float x, float y) { return juce::Point<float>(bounds.getX() + x * sx, bounds.getY() + y * sy); };

        struct Stroke { float x1, y1, x2, y2; bool red; };
        static constexpr Stroke minor[] = {
            { 53.44f, 52.56f, 45.45f, 45.74f, false }, { 111.21f, 24.14f, 107.03f, 14.51f, false },
            { 142.18f, 18.43f, 140.36f, 8.09f, false }, { 172.98f, 17.37f, 173.62f, 6.89f, false },
            { 213.04f, 22.70f, 216.80f, 12.89f, false }, { 249.63f, 35.53f, 255.95f, 27.15f, true },
            { 275.55f, 51.74f, 283.48f, 44.85f, true }, { 297.65f, 75.82f, 306.88f, 70.80f, true }
        };
        static constexpr Stroke major[] = {
            { 31.58f, 93.93f, 14.89f, 87.19f, false }, { 97.86f, 36.10f, 88.59f, 20.67f, false },
            { 130.51f, 27.56f, 125.55f, 10.26f, false }, { 156.40f, 24.89f, 155.15f, 6.94f, false },
            { 188.67f, 26.04f, 192.11f, 8.38f, false }, { 232.14f, 36.10f, 241.41f, 20.67f, true },
            { 257.80f, 48.32f, 270.08f, 35.16f, true }, { 280.94f, 66.66f, 295.68f, 56.33f, true },
            { 298.42f, 93.93f, 315.11f, 87.19f, true }
        };

        const auto drawStrokes = [&](const auto& strokes, float width)
        {
            for (const auto& s : strokes)
            {
                g.setColour(juce::Colour::fromRGB(s.red ? 190 : 28, s.red ? 35 : 28, s.red ? 38 : 28));
                g.drawLine({ p(s.x1, s.y1), p(s.x2, s.y2) }, width * juce::jmin(sx, sy));
            }
        };

        const auto grRayPoint = [=](float degrees, float inset)
        {
            const juce::Point<float> needleCentre { 165.0f, designHeight * 0.88f };
            const juce::Point<float> scaleCentre { 165.0f, designHeight * 0.68f };
            const float radiusX = designWidth * 0.47f;
            const float radiusY = designHeight * 0.64f;
            const float angle = juce::degreesToRadians(degrees);
            const juce::Point<float> direction { std::cos(angle), std::sin(angle) };
            const auto origin = needleCentre - scaleCentre;
            const float rx2 = radiusX * radiusX;
            const float ry2 = radiusY * radiusY;
            const float a = direction.x * direction.x / rx2 + direction.y * direction.y / ry2;
            const float b = 2.0f * (origin.x * direction.x / rx2 + origin.y * direction.y / ry2);
            const float c = origin.x * origin.x / rx2 + origin.y * origin.y / ry2 - 1.0f;
            const float outerDistance = (-b + std::sqrt(juce::jmax(0.0f, b * b - 4.0f * a * c)))
                                      / (2.0f * a);
            const float distance = juce::jmax(0.0f, outerDistance - inset);
            return p(needleCentre.x + direction.x * distance,
                     needleCentre.y + direction.y * distance);
        };

        if (mode == Mode::level)
        {
            drawStrokes(minor, 1.25f);
            drawStrokes(major, 2.0f);
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(28, 28, 28));
            for (const auto& mark : gainReductionMarks)
            {
                const auto outer = grRayPoint(mark.degrees, 0.0f);
                const auto inner = grRayPoint(mark.degrees, 15.0f);
                g.drawLine({ inner, outer }, 2.0f * juce::jmin(sx, sy));
            }
        }

        static constexpr juce::Point<float> labelPositions[] = {
            { 53.83f, 102.92f }, { 110.22f, 56.67f }, { 137.13f, 50.63f },
            { 158.08f, 48.83f }, { 184.10f, 49.60f }, { 219.78f, 56.67f },
            { 241.44f, 65.87f }, { 261.28f, 80.42f }, { 276.17f, 102.92f }
        };
        g.setFont(12.0f * juce::jmin(sx, sy));
        if (mode == Mode::level)
        {
            for (size_t i = 0; i < marks.size(); ++i)
            {
                g.setColour(juce::Colour::fromRGB(i >= 5 ? 174 : 32, i >= 5 ? 28 : 32, i >= 5 ? 32 : 32));
                const auto centre = p(labelPositions[i].x, labelPositions[i].y);
                g.drawText(marks[i].label, juce::Rectangle<float>(centre.x - 18.0f * sx, centre.y - 8.0f * sy,
                                                                  36.0f * sx, 16.0f * sy),
                           juce::Justification::centred, false);
            }
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(32, 32, 32));
            for (const auto& mark : gainReductionMarks)
            {
                const auto centre = grRayPoint(mark.degrees, 30.0f);
                g.drawText(mark.label, juce::Rectangle<float>(centre.x - 18.0f * sx, centre.y - 8.0f * sy,
                                                               36.0f * sx, 16.0f * sy),
                           juce::Justification::centred, false);
            }
        }
    }
};
