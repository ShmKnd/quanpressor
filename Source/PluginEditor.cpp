#include "PluginEditor.h"
#include "MeterPrivacyRenderer.h"
#include "VuScaleRenderer.h"
#include "../JuceLibraryCode/BinaryData.h"
#include <cmath>

namespace
{
    constexpr int kBaseWidth = 1360;
    constexpr int kBaseHeight = 820;
    constexpr float kUiScale = 0.75f;

    const std::array<juce::Rectangle<int>, 2> kMeterPrivacyBaseBounds {{
        { 545, 75, 444, 132 },
        { 545, 245, 444, 543 }
    }};

    juce::Rectangle<int> scaledUiRectangle(juce::Rectangle<int> area)
    {
        return area.toFloat().transformedBy(juce::AffineTransform::scale(kUiScale))
                   .getSmallestIntegerContainer();
    }

    juce::Colour teal()  { return juce::Colour::fromRGB(26, 205, 184); }
    juce::Colour meterGreen() { return juce::Colour::fromRGB(38, 220, 142); }

    juce::Image imageFromBinary(const char* data, int size)
    {
        return juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
    }

    const juce::Image& bgImage()
    {
        static const auto img = imageFromBinary(BinaryData::BG_png, BinaryData::BG_pngSize);
        return img;
    }

    juce::Image createMeterPrivacySnapshot(juce::Component& content,
                                           juce::Rectangle<int> overlayBounds,
                                           float captureScale)
    {
        const int width = juce::jmax(1, juce::roundToInt(overlayBounds.getWidth() * captureScale));
        const int height = juce::jmax(1, juce::roundToInt(overlayBounds.getHeight() * captureScale));
        juce::Image result(juce::Image::ARGB, width, height, true);
        juce::Graphics g(result);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

        // The editor background is painted by the parent, while the moving
        // meters live in content. Compose both without capturing OpenGL views.
        g.drawImage(bgImage(),
                    juce::Rectangle<float>(-overlayBounds.getX() * captureScale,
                                           -overlayBounds.getY() * captureScale,
                                           kBaseWidth * kUiScale * captureScale,
                                           kBaseHeight * kUiScale * captureScale),
                    juce::RectanglePlacement::stretchToFit);

        const float componentScale = kUiScale * captureScale;
        const juce::AffineTransform contentToSnapshot(componentScale, 0.0f,
                                                       -overlayBounds.getX() * captureScale,
                                                       0.0f, componentScale,
                                                       -overlayBounds.getY() * captureScale);
        g.addTransform(contentToSnapshot);
        content.paintEntireComponent(g, true);
        return result;
    }

    juce::Image createGrMatchingForeground(juce::Rectangle<int> overlayBounds,
                                           float captureScale,
                                           bool shouldDraw)
    {
        if (!shouldDraw)
            return {};

        const int width = juce::jmax(1, juce::roundToInt(overlayBounds.getWidth() * captureScale));
        const int height = juce::jmax(1, juce::roundToInt(overlayBounds.getHeight() * captureScale));
        juce::Image result(juce::Image::ARGB, width, height, true);
        juce::Graphics g(result);

        const float graphHeight = scaledUiRectangle({ 545, 245, 444, 337 }).getHeight() * captureScale;
        const float bandHeight = 27.0f * captureScale;
        const auto matchingArea = juce::Rectangle<float>(0.0f, graphHeight * 0.5f - bandHeight * 0.5f,
                                                         static_cast<float>(width), bandHeight);
        g.setColour(juce::Colours::black.withAlpha(0.42f));
        g.fillRect(matchingArea);
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(juce::FontOptions(12.0f * captureScale, juce::Font::bold));
        g.drawText("GR Matching...", matchingArea, juce::Justification::centred, false);
        return result;
    }

    const juce::Image& knobImage(QuanpressorAudioProcessorEditor::Knob::Colourway colourway)
    {
        using Colourway = QuanpressorAudioProcessorEditor::Knob::Colourway;
        static const auto green = imageFromBinary(BinaryData::GreenKnob_png, BinaryData::GreenKnob_pngSize);
        static const auto yellow = imageFromBinary(BinaryData::YellowKnob_png, BinaryData::YellowKnob_pngSize);
        static const auto red = imageFromBinary(BinaryData::RedKnob_png, BinaryData::RedKnob_pngSize);
        static const auto blue = imageFromBinary(BinaryData::BlueKnob_png, BinaryData::BlueKnob_pngSize);
        static const auto grey = imageFromBinary(BinaryData::GrayKnob_png, BinaryData::GrayKnob_pngSize);

        switch (colourway)
        {
            case Colourway::Yellow: return yellow;
            case Colourway::Red:    return red;
            case Colourway::Blue:   return blue;
            case Colourway::Grey:   return grey;
            case Colourway::Green:
            default:                return green;
        }
    }

    const juce::Image& knobHighlightImage()
    {
        static const auto img = imageFromBinary(BinaryData::KnobHighlight_png, BinaryData::KnobHighlight_pngSize);
        return img;
    }

    const juce::Image& knobReflectImage()
    {
        static const auto img = imageFromBinary(BinaryData::KnobReflectOver_png, BinaryData::KnobReflectOver_pngSize);
        return img;
    }

    const juce::Image& buttonImage(QuanpressorAudioProcessorEditor::ImageButton::Kind kind, bool on)
    {
        using Kind = QuanpressorAudioProcessorEditor::ImageButton::Kind;
        static const auto fet = imageFromBinary(BinaryData::FET_Button_png, BinaryData::FET_Button_pngSize);
        static const auto fetOn = imageFromBinary(BinaryData::FET_Button_ON_png, BinaryData::FET_Button_ON_pngSize);
        static const auto opto = imageFromBinary(BinaryData::OPTO_Button_png, BinaryData::OPTO_Button_pngSize);
        static const auto optoOn = imageFromBinary(BinaryData::OPTO_Button_ON_png, BinaryData::OPTO_Button_ON_pngSize);
        static const auto vca = imageFromBinary(BinaryData::VCA_Button_png, BinaryData::VCA_Button_pngSize);
        static const auto vcaOn = imageFromBinary(BinaryData::VCA_Button_ON_png, BinaryData::VCA_Button_ON_pngSize);
        static const auto variMu = imageFromBinary(BinaryData::VariMu_Button_png, BinaryData::VariMu_Button_pngSize);
        static const auto variMuOn = imageFromBinary(BinaryData::VariMu_Button_ON_png, BinaryData::VariMu_Button_ON_pngSize);
        static const auto gr = imageFromBinary(BinaryData::GR_MATCH_Button_png, BinaryData::GR_MATCH_Button_pngSize);
        static const auto grOn = imageFromBinary(BinaryData::GR_MATCH_Button_ON_png, BinaryData::GR_MATCH_Button_ON_pngSize);
        static const auto autoRelease = imageFromBinary(BinaryData::Auto_Release_Button_png, BinaryData::Auto_Release_Button_pngSize);
        static const auto autoReleaseOn = imageFromBinary(BinaryData::Auto_Release_Button_ON_png, BinaryData::Auto_Release_Button_ON_pngSize);
        static const auto autoGain = imageFromBinary(BinaryData::Auto_Gain_Button_png, BinaryData::Auto_Gain_Button_pngSize);
        static const auto autoGainOn = imageFromBinary(BinaryData::Auto_Gain_Button_ON_png, BinaryData::Auto_Gain_Button_ON_pngSize);
        static const auto stChain = imageFromBinary(BinaryData::ST_Chain_png, BinaryData::ST_Chain_pngSize);
        static const auto stChainOn = imageFromBinary(BinaryData::ST_Chain_ON_png, BinaryData::ST_Chain_ON_pngSize);
        static const auto scFilter = imageFromBinary(BinaryData::SC_Filter_png, BinaryData::SC_Filter_pngSize);
        static const auto scFilterOn = imageFromBinary(BinaryData::SC_Filter_ON_png, BinaryData::SC_Filter_ON_pngSize);
        static const auto resetPhotocell = imageFromBinary(BinaryData::RESET_PHOTOCELL_png, BinaryData::RESET_PHOTOCELL_pngSize);
        static const auto resetPhotocellOn = imageFromBinary(BinaryData::RESET_PHOTOCELL_ON_png, BinaryData::RESET_PHOTOCELL_ON_pngSize);
        static const auto vuOutput = imageFromBinary(BinaryData::VU_OUTPUTmode_png, BinaryData::VU_OUTPUTmode_pngSize);
        static const auto vuOutputOn = imageFromBinary(BinaryData::VU_OUTPUTmode_ON_png, BinaryData::VU_OUTPUTmode_ON_pngSize);
        static const auto vuInput = imageFromBinary(BinaryData::VU_INPUTmode_png, BinaryData::VU_INPUTmode_pngSize);
        static const auto vuInputOn = imageFromBinary(BinaryData::VU_INPUTmode_ON_png, BinaryData::VU_INPUTmode_ON_pngSize);
        static const auto vuGr = imageFromBinary(BinaryData::VU_GRmode_png, BinaryData::VU_GRmode_pngSize);
        static const auto vuGrOn = imageFromBinary(BinaryData::VU_GRmode_ON_png, BinaryData::VU_GRmode_ON_pngSize);
        static const auto noMeter = imageFromBinary(BinaryData::NoMeterButton_png, BinaryData::NoMeterButton_pngSize);
        static const auto noMeterOn = imageFromBinary(BinaryData::NoMeterButton_ON_png, BinaryData::NoMeterButton_ON_pngSize);

        switch (kind)
        {
            case Kind::Opto:        return on ? optoOn : opto;
            case Kind::Vca:         return on ? vcaOn : vca;
            case Kind::VariMu:      return on ? variMuOn : variMu;
            case Kind::GrMatch:     return on ? grOn : gr;
            case Kind::AutoRelease: return on ? autoReleaseOn : autoRelease;
            case Kind::AutoGain:    return on ? autoGainOn : autoGain;
            case Kind::StChain:     return on ? stChainOn : stChain;
            case Kind::ScFilter:    return on ? scFilterOn : scFilter;
            case Kind::ResetPhotocell: return on ? resetPhotocellOn : resetPhotocell;
            case Kind::VuOutput:    return on ? vuOutputOn : vuOutput;
            case Kind::VuInput:     return on ? vuInputOn : vuInput;
            case Kind::VuGr:        return on ? vuGrOn : vuGr;
            case Kind::NoMeter:     return on ? noMeterOn : noMeter;
            case Kind::Fet:
            default:                return on ? fetOn : fet;
        }
    }

    const juce::Image& inOutMeterEmbosImage()
    {
        static const auto img = imageFromBinary(BinaryData::INOUT_Meter_Embos_png,
                                                BinaryData::INOUT_Meter_Embos_pngSize);
        return img;
    }

    const juce::Image& grCurveEmbosImage()
    {
        static const auto img = imageFromBinary(BinaryData::GRCurve_Embos_png,
                                                BinaryData::GRCurve_Embos_pngSize);
        return img;
    }

    const juce::Image& grHistoryEmbosImage()
    {
        static const auto img = imageFromBinary(BinaryData::GRHistory_Embos_png,
                                                BinaryData::GRHistory_Embos_pngSize);
        return img;
    }

    static constexpr float grMarks[] = { 0.0f, -3.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f };

    float grDbToY(float db, juce::Rectangle<float> area)
    {
        const float grDb = -juce::jlimit(-30.0f, 0.0f, db);
        const float upperRangeHeight = 0.70f;
        const float normalized = grDb <= 12.0f
            ? (grDb / 12.0f) * upperRangeHeight
            : upperRangeHeight + ((grDb - 12.0f) / 18.0f) * (1.0f - upperRangeHeight);
        return area.getY() + area.getHeight() * normalized;
    }

    juce::String grDbLabel(float db)
    {
        return db == 0.0f ? "0" : juce::String((int) db);
    }

    float knobStartAngle(bool timeConstantScale)
    {
        return juce::degreesToRadians(timeConstantScale ? -75.0f : -135.0f);
    }

    float knobEndAngle(bool timeConstantScale)
    {
        return juce::degreesToRadians(timeConstantScale ? 50.0f : 135.0f);
    }

    float variableMuTransferOutput(float in, float threshold)
    {
        const float overDb = juce::jmax(0.0f, in - threshold);
        return in - QuanpressorDSP::variableMuGainReductionDb(overDb);
    }

    float transferOutput(float in, float threshold, float ratio, float knee, int mode)
    {
        if (mode == QuanpressorDSP::VARIMU)
            return variableMuTransferOutput(in, threshold);

        if (mode == QuanpressorDSP::FET)
        {
            constexpr float fetKneeDb = 6.0f;
            const float overDb = in - threshold;
            if (overDb <= 0.0f)
                return in;

            const float slope = 1.0f - 1.0f / ratio;
            const float reductionDb = overDb < fetKneeDb
                ? slope * overDb * overDb / (2.0f * fetKneeDb)
                : slope * (overDb - fetKneeDb * 0.5f);
            return in - reductionDb;
        }

        const float lower = threshold - knee * 0.5f;
        const float upper = threshold + knee * 0.5f;

        if (knee > 0.001f && in > lower && in < upper)
        {
            const float x = in - lower;
            return in - (1.0f - 1.0f / ratio) * x * x / (2.0f * knee);
        }
        if (in >= upper)
            return threshold + (in - threshold) / ratio;
        return in;
    }
}

juce::Rectangle<int> QuanpressorAudioProcessorEditor::TooltipLookAndFeel::getTooltipBounds(
    const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto text = tipText.trimEnd();
    const juce::Font font { juce::FontOptions(12.0f) };
    juce::StringArray lines;
    lines.addLines(text);

    int textWidth = 0;
    for (const auto& line : lines)
        textWidth = juce::jmax(textWidth, juce::GlyphArrangement::getStringWidthInt(font, line));

    const int width = textWidth + 16;
    const int height = juce::roundToInt(font.getHeight() * juce::jmax(1, lines.size())) + 10;
    const int x = screenPos.x > parentArea.getCentreX() ? screenPos.x - width - 12 : screenPos.x + 12;
    const int y = screenPos.y > parentArea.getCentreY() ? screenPos.y - height - 6 : screenPos.y + 6;
    return juce::Rectangle<int>(x, y, width, height).constrainedWithin(parentArea);
}

void QuanpressorAudioProcessorEditor::TooltipLookAndFeel::drawTooltip(
    juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.setColour(juce::Colour::fromRGB(24, 25, 27).withAlpha(0.98f));
    g.fillRect(0, 0, width, height);
    g.setColour(juce::Colour::fromRGB(104, 108, 112));
    g.drawRect(0, 0, width, height, 1);

    g.setColour(juce::Colour::fromRGB(236, 236, 232));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(text.trimEnd(),
                     juce::Rectangle<int>(8, 5, juce::jmax(1, width - 16), juce::jmax(1, height - 10)),
                     juce::Justification::centredLeft, 3, 1.0f);
}

void QuanpressorAudioProcessorEditor::TooltipLookAndFeel::drawBubble(
    juce::Graphics& g, juce::BubbleComponent& bubble, const juce::Point<float>& tip,
    const juce::Rectangle<float>& body)
{
    juce::ignoreUnused(tip);

    g.setColour(bubble.findColour(juce::BubbleComponent::backgroundColourId));
    g.fillRect(body);
    g.setColour(bubble.findColour(juce::BubbleComponent::outlineColourId));
    g.drawRect(body, 1.0f);
}

void QuanpressorAudioProcessorEditor::Knob::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float knobDiameter = juce::jmin(65.0f, juce::jmin(area.getWidth(), area.getHeight()));
    auto knobArea = area.withSizeKeepingCentre(knobDiameter, knobDiameter);
    knobArea = knobArea.reduced(1.0f);

    const auto& base = knobImage(colourway);
    const auto& highlight = knobHighlightImage();
    const auto& reflect = knobReflectImage();
    const auto range = getRange();
    const double proportion = range.getLength() > 0.0 ? (getValue() - range.getStart()) / range.getLength() : 0.0;
    const float startAngle = knobStartAngle(useTimeConstantScale);
    const float endAngle = knobEndAngle(useTimeConstantScale);
    const float angle = juce::jmap(static_cast<float>(juce::jlimit(0.0, 1.0, proportion)), startAngle, endAngle);
    const auto centre = knobArea.getCentre();

    const bool enabled = isEnabled();
    g.setOpacity(enabled ? 1.0f : (useTimeConstantScale ? 0.58f : 0.44f));
    juce::AffineTransform transform = juce::AffineTransform::translation(-base.getWidth() * 0.5f, -base.getHeight() * 0.5f)
        .rotated(angle)
        .scaled(knobArea.getWidth() / static_cast<float>(base.getWidth()),
                knobArea.getHeight() / static_cast<float>(base.getHeight()))
        .translated(centre.x, centre.y);
    g.drawImageTransformed(base, transform, false);

    auto highlightArea = knobArea.withSizeKeepingCentre(knobArea.getWidth() * 1.04f, knobArea.getHeight() * 0.72f);
    highlightArea.setY(knobArea.getY() - knobArea.getHeight() * 0.03f);
    if (enabled)
    {
        g.drawImage(highlight, highlightArea, juce::RectanglePlacement::stretchToFit);
        g.drawImage(reflect, knobArea, juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.setOpacity(1.0f);
        g.setColour(juce::Colours::black.withAlpha(useTimeConstantScale ? 0.14f : 0.28f));
        g.fillEllipse(knobArea.reduced(2.0f));
    }
    g.setOpacity(1.0f);
}

void QuanpressorAudioProcessorEditor::Knob::mouseDown(const juce::MouseEvent& event)
{
    juce::Slider::mouseDown(event);
    movePopupCloserToKnob();
}

void QuanpressorAudioProcessorEditor::Knob::mouseDrag(const juce::MouseEvent& event)
{
    juce::Slider::mouseDrag(event);
    movePopupCloserToKnob();
}

void QuanpressorAudioProcessorEditor::Knob::movePopupCloserToKnob()
{
    auto* popup = getCurrentPopupDisplay();
    if (popup == nullptr)
        return;

    auto popupBounds = popup->getBounds();
    const auto targetBounds = getScreenBounds().transformedBy(popup->getTransform().inverted());
    const int deltaX = popupBounds.getCentreX() - targetBounds.getCentreX();
    const int deltaY = popupBounds.getCentreY() - targetBounds.getCentreY();
    constexpr int popupTargetOverlap = 13;

    if (std::abs(deltaY) >= std::abs(deltaX))
    {
        if (deltaY < 0)
            popupBounds.setBottom(targetBounds.getY() + popupTargetOverlap);
        else
            popupBounds.setY(targetBounds.getBottom() - popupTargetOverlap);
    }
    else
    {
        if (deltaX < 0)
            popupBounds.setRight(targetBounds.getX() + popupTargetOverlap);
        else
            popupBounds.setX(targetBounds.getRight() - popupTargetOverlap);
    }

    popup->setBounds(popupBounds);
}

void QuanpressorAudioProcessorEditor::ImageButton::paintButton(juce::Graphics& g,
                                                               bool highlighted,
                                                               bool down)
{
    const bool momentary = kind == Kind::ResetPhotocell;
    const bool on = momentary ? down : getToggleState();
    const auto& img = buttonImage(kind, on);
    auto area = getLocalBounds().toFloat();
    if (down)
        area.translate(0.0f, 1.0f);

    const bool enabled = isEnabled();
    const float enabledAlpha = enabled ? 1.0f : 0.34f;
    const float stateAlpha = highlighted ? 1.0f : 0.96f;
    g.setOpacity(enabledAlpha * stateAlpha);
    g.drawImage(img, area, juce::RectanglePlacement::stretchToFit);
    if (!enabled)
    {
        g.setOpacity(1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.38f));
        g.fillRect(area);
    }
    g.setOpacity(1.0f);
}

void QuanpressorAudioProcessorEditor::addKnob(Knob& k, const char* id)
{
    content.addAndMakeVisible(k);
    k.setLookAndFeel(&tooltipLookAndFeel);
    k.parameterId = id;
    k.setPopupDisplayEnabled(true, false, nullptr);
    if (auto* parameter = proc.apvts.getParameter(id))
    {
        k.textFromValueFunction = [parameter](double value)
        {
            return parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 64);
        };
    }
    sa.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, k));
}

QuanpressorAudioProcessorEditor::QuanpressorAudioProcessorEditor(QuanpressorAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p), graph(p), historyGraph(p)
{
    addAndMakeVisible(content);
    tooltipWindow.setLookAndFeel(&tooltipLookAndFeel);
    setSize(juce::roundToInt(kBaseWidth * kUiScale),
            juce::roundToInt(kBaseHeight * kUiScale));

    mode.addItemList({ "FET", "OPTO", "VCA", "VARI-MU" }, 1);
    modeButtons[0].kind = ImageButton::Kind::Fet;
    modeButtons[1].kind = ImageButton::Kind::Opto;
    modeButtons[2].kind = ImageButton::Kind::Vca;
    modeButtons[3].kind = ImageButton::Kind::VariMu;
    grMatch.kind = ImageButton::Kind::GrMatch;
    autoRelease.kind = ImageButton::Kind::AutoRelease;
    autoLevel.kind = ImageButton::Kind::AutoGain;
    stereoChain.kind = ImageButton::Kind::StChain;
    sidechainHpf.kind = ImageButton::Kind::ScFilter;
    optoReset.kind = ImageButton::Kind::ResetPhotocell;
    vuModeButtons[0].kind = ImageButton::Kind::VuOutput;
    vuModeButtons[1].kind = ImageButton::Kind::VuInput;
    vuModeButtons[2].kind = ImageButton::Kind::VuGr;
    noMeter.kind = ImageButton::Kind::NoMeter;

    for (int i = 0; i < static_cast<int>(modeButtons.size()); ++i)
    {
        modeButtons[static_cast<size_t>(i)].setClickingTogglesState(false);
        modeButtons[static_cast<size_t>(i)].onClick = [this, i] { setModeFromButton(i); };
        content.addAndMakeVisible(modeButtons[static_cast<size_t>(i)]);
    }

    for (int i = 0; i < static_cast<int>(vuModeButtons.size()); ++i)
    {
        vuModeButtons[static_cast<size_t>(i)].setClickingTogglesState(false);
        vuModeButtons[static_cast<size_t>(i)].onClick = [this, i] { setVuModeFromButton(i); };
        content.addAndMakeVisible(vuModeButtons[static_cast<size_t>(i)]);
    }

    for (auto* button : { &autoLevel, &autoRelease, &grMatch, &sidechainHpf, &stereoChain, &noMeter })
        button->setClickingTogglesState(true);
    content.addAndMakeVisible(noMeter);

    addKnob(input,     "input");
    addKnob(threshold, "threshold");
    addKnob(ratio,     "ratio");
    addKnob(drive,     "drive");
    addKnob(attack,    "attack");
    addKnob(release,   "release");
    addKnob(knee,      "knee");
    addKnob(makeup,    "makeup");
    addKnob(highFreq,  "highFreq");
    addKnob(highAmt,   "highAmt");
    addKnob(lowFreq,   "lowFreq");
    addKnob(lowAmt,    "lowAmt");
    addKnob(output,    "output");
    addKnob(mix,       "mix");
    content.addAndMakeVisible(timeConstant);
    timeConstant.setLookAndFeel(&tooltipLookAndFeel);
    timeConstant.setPopupDisplayEnabled(true, false, nullptr);
    if (auto* parameter = proc.apvts.getParameter("muSpeed"))
    {
        timeConstant.textFromValueFunction = [parameter](double value)
        {
            return parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 64);
        };
    }
    timeConstant.useIntegerGrid = true;
    timeConstant.useTimeConstantScale = true;
    sa.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "muSpeed", timeConstant));

    drive.colourway = Knob::Colourway::Yellow;
    highFreq.colourway = Knob::Colourway::Red;
    highAmt.colourway = Knob::Colourway::Red;
    lowFreq.colourway = Knob::Colourway::Blue;
    lowAmt.colourway = Knob::Colourway::Blue;
    input.colourway = Knob::Colourway::Grey;
    output.colourway = Knob::Colourway::Grey;
    makeup.colourway = Knob::Colourway::Grey;
    mix.colourway = Knob::Colourway::Grey;

    for (auto* c : { static_cast<juce::Component*>(&autoLevel), static_cast<juce::Component*>(&autoRelease),
                     static_cast<juce::Component*>(&grMatch), static_cast<juce::Component*>(&sidechainHpf),
                     static_cast<juce::Component*>(&stereoChain), static_cast<juce::Component*>(&optoReset),
                     static_cast<juce::Component*>(&inMeter), static_cast<juce::Component*>(&outMeter),
                     static_cast<juce::Component*>(&grMeter),
                     static_cast<juce::Component*>(&graph), static_cast<juce::Component*>(&historyGraph),
                     static_cast<juce::Component*>(&grCurveEmbos),
                     static_cast<juce::Component*>(&grHistoryEmbos) })
        content.addAndMakeVisible(c);
    for (auto& vuMeter : vuMeters)
        content.addAndMakeVisible(vuMeter);

    ba.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoLevel", autoLevel));
    ba.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoRelease", autoRelease));
    ba.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "grMatch", grMatch));
    ba.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "sidechainHpf", sidechainHpf));
    ba.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "stereoChain", stereoChain));
    ba.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "noMeter", noMeter));

    modeButtons[0].setTooltip("FET - Fast attack for punch and impact.");
    modeButtons[1].setTooltip("Opto - Gentle leveling that follows the performance naturally.");
    modeButtons[2].setTooltip("VCA - Accurate compression for everyday mixing.");
    modeButtons[3].setTooltip("Vari-Mu - Warm analog color with smooth dynamics.");

    optoReset.setTooltip("Resets the photocell memory for a fresh compression response.");
    noMeter.setTooltip("No Meter Mode\nHides all meters to encourage mixing by ear.");
    highFreq.setTooltip("High Preserve Frequency\nSets the high-frequency band blended back after compression.");
    highAmt.setTooltip("High Preserve Amount\nBlends the preserved high-frequency band back into the compressed signal.");
    lowFreq.setTooltip("Low Preserve Frequency\nSets the low-frequency band blended back after compression.");
    lowAmt.setTooltip("Low Preserve Amount\nBlends the preserved low-frequency band back into the compressed signal.");
    autoLevel.setTooltip("Auto Gain\nAutomatically compensates for output level changes.");
    autoRelease.setTooltip("Auto Release\nAutomatically adjusts release time based on the input signal.");
    grMatch.setTooltip("Matches the amount of gain reduction across compressor types.");
    stereoChain.setTooltip("ST LINK\nLinks the left and right channels for stereo compression.");
    sidechainHpf.setTooltip("SC FILTER\nHigh-pass filters the sidechain to reduce low-frequency triggering.");
    optoReset.onClick = [this] { proc.requestOptoReset(); };

    inMeter.peakValues = { &proc.meters.inputDb[0], &proc.meters.inputDb[1] };
    inMeter.rmsValues = { &proc.meters.inputRmsDb[0], &proc.meters.inputRmsDb[1] };
    inMeter.title = "INPUT";
    outMeter.peakValues = { &proc.meters.outputDb[0], &proc.meters.outputDb[1] };
    outMeter.rmsValues = { &proc.meters.outputRmsDb[0], &proc.meters.outputRmsDb[1] };
    outMeter.title = "OUTPUT";
    grMeter.value = &proc.meters.grDb;      grMeter.title = "GR"; grMeter.gr = true;
    for (size_t ch = 0; ch < vuMeters.size(); ++ch)
    {
        vuMeters[ch].outputValue = &proc.meters.outputVuDb[ch];
        vuMeters[ch].inputValue = &proc.meters.inputVuDb[ch];
        vuMeters[ch].grValue = &proc.meters.grVuDb[ch];
    }
    grCurveEmbos.kind = EmbosOverlay::Kind::GrCurve;

    for (auto& renderer : meterPrivacyRenderers)
    {
        renderer = createMeterPrivacyRenderer();
        if (renderer != nullptr)
        {
            addAndMakeVisible(renderer->getView());
            renderer->getView().setVisible(false);
            renderer->attach();
        }
    }

    grMatchingOverlay.matching = &proc.meters.grMatching;
    addAndMakeVisible(grMatchingOverlay);

    updateArchitectureControls();
    startTimerHz(30);
}

QuanpressorAudioProcessorEditor::~QuanpressorAudioProcessorEditor()
{
    stopTimer();
    tooltipWindow.setLookAndFeel(nullptr);
    for (auto& renderer : meterPrivacyRenderers)
        if (renderer != nullptr)
            renderer->shutdown();
}

void QuanpressorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(kUiScale));
    g.drawImage(bgImage(), juce::Rectangle<float>(0.0f, 0.0f, kBaseWidth, kBaseHeight),
                juce::RectanglePlacement::stretchToFit);
}

void QuanpressorAudioProcessorEditor::resized()
{
    content.setBounds(0, 0, kBaseWidth, kBaseHeight);
    content.setTransform(juce::AffineTransform::scale(kUiScale));

    const auto placeKnob = [](juce::Slider& slider, int x, int y)
    {
        slider.setBounds(x, y, 65, 65);
    };

    const auto placeButton = [](juce::Button& button, int x, int y, int w, int h)
    {
        button.setBounds(x - 3, y - 2, w, h);
    };

    inMeter.setBounds(10, 88, 88, 532);
    input.setBounds(22, 676, 65, 65);
    placeButton(stereoChain, 10, 752, 94, 22);
    placeButton(sidechainHpf, 10, 783, 94, 22);

    outMeter.setBounds(1258, 88, 88, 532);
    output.setBounds(1270, 676, 65, 65);

    placeButton(modeButtons[0], 133, 109, 94, 44);
    placeButton(modeButtons[1], 237, 109, 94, 44);
    placeButton(modeButtons[2], 339, 109, 94, 44);
    placeButton(modeButtons[3], 441, 109, 94, 44);

    vuModeButtons[1].setBounds(651, 212, 64, 22);
    vuModeButtons[0].setBounds(725, 212, 64, 22);
    vuModeButtons[2].setBounds(798, 212, 64, 22);
    noMeter.setBounds(949, 207, 30, 31);

    placeKnob(threshold, 146, 263);
    placeKnob(ratio, 250, 263);
    placeKnob(knee, 352, 263);
    placeKnob(drive, 452, 263);
    placeButton(grMatch, 281, 172, 94, 22);

    placeKnob(attack, 146, 437);
    placeKnob(release, 285, 437);
    placeKnob(makeup, 429, 437);
    placeButton(autoRelease, 274, 524, 94, 22);
    placeButton(autoLevel, 418, 524, 94, 22);

    timeConstant.setBounds(204, 638, 65, 65);
    placeButton(optoReset, 348, 653, 115, 31);

    vuMeters[0].setBounds(582, 93, 168, 86);
    vuMeters[1].setBounds(781, 93, 168, 86);
    graph.setBounds(545, 245, 444, 337);
    grCurveEmbos.setBounds(graph.getBounds());
    historyGraph.setBounds(545, 586, 380, 202);
    grMeter.setBounds(925, 586, 64, 202);
    grHistoryEmbos.setBounds(historyGraph.getBounds().getUnion(grMeter.getBounds()));

    for (size_t i = 0; i < meterPrivacyRenderers.size(); ++i)
        if (meterPrivacyRenderers[i] != nullptr)
            meterPrivacyRenderers[i]->getView().setBounds(scaledUiRectangle(kMeterPrivacyBaseBounds[i]));
    grMatchingOverlay.setBounds(scaledUiRectangle({ 545, 245, 444, 337 }));

    placeKnob(highFreq, 1020, 188);
    placeKnob(highAmt, 1140, 188);
    placeKnob(lowFreq, 1020, 367);
    placeKnob(lowAmt, 1140, 367);
    placeKnob(mix, 1082, 515);

    if (meterPrivacyEnabled)
        updateMeterPrivacy(true, true);
}

void QuanpressorAudioProcessorEditor::updateArchitectureControls()
{
    const int m = static_cast<int>(proc.apvts.getRawParameterValue("mode")->load());
    const bool isFet = m == QuanpressorDSP::FET;
    const bool isOpto = m == QuanpressorDSP::OPTO;
    const bool isVca = m == QuanpressorDSP::VCA;
    const bool isMu = m == QuanpressorDSP::VARIMU;
    const bool levelMatchOn = proc.apvts.getRawParameterValue("autoLevel")->load() > 0.5f;
    const bool autoReleaseOn = proc.apvts.getRawParameterValue("autoRelease")->load() > 0.5f;

    for (int i = 0; i < static_cast<int>(modeButtons.size()); ++i)
        modeButtons[static_cast<size_t>(i)].setToggleState(i == m, juce::dontSendNotification);

    const auto enableControl = [](juce::Component& control, bool enabled)
    {
        control.setEnabled(enabled);
        control.setAlpha(1.0f);
    };

    enableControl(ratio, !isMu);
    ratio.useFetRatioGrid = isFet;
    enableControl(attack, isFet || isVca);
    enableControl(release, (isFet || isVca) && !autoReleaseOn);
    enableControl(knee, isVca);
    timeConstant.setEnabled(isMu);
    timeConstant.setAlpha(1.0f);
    enableControl(makeup, !levelMatchOn);

    autoRelease.setEnabled(isFet || isVca);
    autoRelease.setAlpha(1.0f);
    optoReset.setEnabled(isOpto);
    optoReset.setAlpha(1.0f);

    ratio.setTooltip(isFet ? "Fixed FET ratios: 4:1, 8:1, 12:1 or 20:1."
                     : isOpto ? "Lower values favour compression; higher values move toward limiting."
                     : isVca ? "Continuous fixed ratio from 1.5:1 to 20:1."
                     : juce::String());
}

void QuanpressorAudioProcessorEditor::setModeFromButton(int index)
{
    if (auto* parameter = proc.apvts.getParameter("mode"))
    {
        const float normalized = static_cast<float>(juce::jlimit(0, 3, index)) / 3.0f;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(normalized);
        parameter->endChangeGesture();
    }

    updateArchitectureControls();
}

void QuanpressorAudioProcessorEditor::setVuModeFromButton(int index)
{
    if (auto* parameter = proc.apvts.getParameter("vuMode"))
    {
        const float normalized = static_cast<float>(juce::jlimit(0, 2, index)) / 2.0f;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(normalized);
        parameter->endChangeGesture();
    }
}

void QuanpressorAudioProcessorEditor::updateMeterPrivacy(bool shouldBeEnabled, bool forceCapture)
{
    if (!shouldBeEnabled)
    {
        meterPrivacyEnabled = false;
        meterPrivacyCaptureCounter = 0;
        for (auto& renderer : meterPrivacyRenderers)
            if (renderer != nullptr)
                renderer->setEnabled(false);
        return;
    }

    ++meterPrivacyCaptureCounter;
    const bool shouldCapture = forceCapture || !meterPrivacyEnabled
                            || meterPrivacyCaptureCounter >= 3;
    if (shouldCapture)
    {
        meterPrivacyCaptureCounter = 0;
        const float captureScale = juce::jlimit(1.0f, 2.0f,
            juce::Component::getApproximateScaleFactorForComponent(this));
        for (size_t i = 0; i < meterPrivacyRenderers.size(); ++i)
        {
            auto& renderer = meterPrivacyRenderers[i];
            if (renderer == nullptr)
                continue;

            auto& view = renderer->getView();
            const auto overlayBounds = scaledUiRectangle(kMeterPrivacyBaseBounds[i]);
            if (view.getBounds() != overlayBounds)
                view.setBounds(overlayBounds);

            // The renderer views are siblings of content, so this captures only
            // the native meters and never the previous blurred OpenGL frame.
            const auto source = createMeterPrivacySnapshot(content, overlayBounds, captureScale);
            MeterPrivacyRenderer::Regions localRegions {};
            localRegions[0] = juce::Rectangle<int>(0, 0, overlayBounds.getWidth(), overlayBounds.getHeight());

            const float blurRadius = (i == 0 ? 28.0f : 38.0f) * captureScale;
            const bool matchingVisible = i == 1
                && proc.meters.grMatching.load(std::memory_order_relaxed)
                && ((juce::Time::getMillisecondCounter() / 400u) % 2u == 0u);
            auto foreground = createGrMatchingForeground(overlayBounds, captureScale, matchingVisible);

            renderer->submitFrame(source, localRegions, blurRadius, std::move(foreground));
        }
    }

    meterPrivacyEnabled = true;
    for (auto& renderer : meterPrivacyRenderers)
    {
        if (renderer == nullptr)
            continue;

        renderer->setEnabled(true);
        renderer->getView().toFront(false);
    }

}

void QuanpressorAudioProcessorEditor::timerCallback()
{
    updateArchitectureControls();
    const int vuModeIndex = juce::jlimit(0, 2,
        static_cast<int>(proc.apvts.getRawParameterValue("vuMode")->load()));
    for (auto& vuMeter : vuMeters)
        vuMeter.mode = static_cast<VuMeter::Mode>(vuModeIndex);
    for (int i = 0; i < static_cast<int>(vuModeButtons.size()); ++i)
        vuModeButtons[static_cast<size_t>(i)].setToggleState(i == vuModeIndex,
                                                             juce::dontSendNotification);

    const bool hideMeters = proc.apvts.getRawParameterValue("noMeter")->load() > 0.5f;
    updateMeterPrivacy(hideMeters);
    grMatchingOverlay.setVisible(!hideMeters);
    if (!hideMeters)
        grMatchingOverlay.toFront(false);
    grMatchingOverlay.repaint();
    inMeter.repaint();
    outMeter.repaint();
    grMeter.repaint();
    for (auto& vuMeter : vuMeters)
        vuMeter.repaint();
    graph.repaint();
    historyGraph.repaint();
}

void QuanpressorAudioProcessorEditor::Meter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f));
    const float v = value ? value->load() : -100.0f;
    std::array<float, 2> peakDb {{ -100.0f, -100.0f }};
    std::array<float, 2> rmsDb {{ -100.0f, -100.0f }};
    if (!gr)
    {
        for (size_t ch = 0; ch < peakDb.size(); ++ch)
        {
            if (peakValues[ch] != nullptr)
                peakDb[ch] = peakValues[ch]->load(std::memory_order_relaxed);
            if (rmsValues[ch] != nullptr)
                rmsDb[ch] = rmsValues[ch]->load(std::memory_order_relaxed);
        }
    }
    auto bounds = getLocalBounds();
    g.setColour(juce::Colours::white);
    g.setFont(10.5f);
    g.drawText(title, bounds.removeFromTop(18), juce::Justification::centred);

    auto content = getLocalBounds().toFloat();
    if (gr)
    {
        content.removeFromTop(24.0f);
        content.removeFromBottom(18.0f);
        content.removeFromLeft(4.0f);
        content.removeFromRight(4.0f);
    }
    else
    {
        content = content.reduced(4.0f);
        content.removeFromTop(18.0f);
    }

    auto scaleArea = content.removeFromRight(18.0f);
    auto peakArea = content;

    const auto levelToY = [&](float db)
    {
        if (gr)
            return grDbToY(db, peakArea);
        return juce::jmap(juce::jlimit(-60.0f, 6.0f, db), 6.0f, -60.0f, peakArea.getY(), peakArea.getBottom());
    };

    if (gr)
    {
        const float y = levelToY(v);
        g.setColour(juce::Colours::orange);
        // 0 dB GR is an empty meter at the top; increasing reduction grows downward.
        g.fillRect(peakArea.withBottom(y));
    }
    else
    {
        const auto meterColour = meterGreen();
        const auto overloadColour = juce::Colour::fromRGB(238, 54, 56);
        const auto greenZoneColour = juce::Colour::fromRGB(9, 48, 33);
        const auto redZoneColour = juce::Colour::fromRGB(52, 18, 20);
        content.removeFromRight(2.0f);
        auto rmsPairArea = content.removeFromRight(9.0f);
        content.removeFromRight(2.0f);
        auto peakPairArea = content;

        constexpr float peakGap = 2.0f;
        const float peakWidth = (peakPairArea.getWidth() - peakGap) * 0.5f;
        const std::array<juce::Rectangle<float>, 2> peakAreas {{
            peakPairArea.withWidth(peakWidth),
            juce::Rectangle<float>(peakPairArea.getX() + peakWidth + peakGap,
                                   peakPairArea.getY(), peakWidth, peakPairArea.getHeight())
        }};

        constexpr float rmsGap = 1.0f;
        const float rmsWidth = (rmsPairArea.getWidth() - rmsGap) * 0.5f;
        const std::array<juce::Rectangle<float>, 2> rmsAreas {{
            rmsPairArea.withWidth(rmsWidth),
            juce::Rectangle<float>(rmsPairArea.getX() + rmsWidth + rmsGap,
                                   rmsPairArea.getY(), rmsWidth, rmsPairArea.getHeight())
        }};

        const auto drawLevelBar = [&](juce::Rectangle<float> area, float db,
                                      float activeAlpha, bool drawPeakCap)
        {
            const float zeroY = juce::jmap(0.0f, 6.0f, -60.0f, area.getY(), area.getBottom());
            const float y = juce::jmap(juce::jlimit(-60.0f, 6.0f, db),
                                       6.0f, -60.0f, area.getY(), area.getBottom());
            const auto redZone = area.withBottom(zeroY);
            const auto greenZone = area.withTop(zeroY);
            const auto activeArea = area.withTop(y);

            g.setColour(redZoneColour);
            g.fillRect(redZone);
            g.setColour(greenZoneColour);
            g.fillRect(greenZone);
            g.setColour(meterColour.withAlpha(activeAlpha));
            g.fillRect(activeArea.getIntersection(greenZone));
            g.setColour(overloadColour.withAlpha(activeAlpha));
            g.fillRect(activeArea.getIntersection(redZone));

            if (drawPeakCap)
            {
                g.setColour((db > 0.0f ? overloadColour.brighter(0.25f)
                                       : meterColour.brighter(0.22f)).withAlpha(0.95f));
                g.fillRect(area.getX(), juce::jlimit(area.getY(), area.getBottom() - 1.5f, y),
                           area.getWidth(), 1.5f);
            }
        };

        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const float elapsedSeconds = lastHoldUpdateMs > 0.0
            ? juce::jlimit(0.0f, 0.1f, static_cast<float>((nowMs - lastHoldUpdateMs) * 0.001))
            : 0.0f;
        lastHoldUpdateMs = nowMs;

        for (size_t ch = 0; ch < peakAreas.size(); ++ch)
        {
            drawLevelBar(peakAreas[ch], peakDb[ch], 1.0f, true);
            drawLevelBar(rmsAreas[ch], rmsDb[ch], 0.72f, false);

            if (heldPeakDb[ch] <= -99.0f || peakDb[ch] >= heldPeakDb[ch])
            {
                heldPeakDb[ch] = peakDb[ch];
                holdUntilMs[ch] = nowMs + 1400.0;
            }
            else if (nowMs > holdUntilMs[ch])
            {
                heldPeakDb[ch] = juce::jmax(peakDb[ch], heldPeakDb[ch] - 18.0f * elapsedSeconds);
            }

            if (heldPeakDb[ch] > -60.0f)
            {
                const float holdY = juce::jmap(juce::jlimit(-60.0f, 6.0f, heldPeakDb[ch]),
                                               6.0f, -60.0f,
                                               peakAreas[ch].getY(), peakAreas[ch].getBottom());
                g.setColour((heldPeakDb[ch] > 0.0f ? overloadColour.brighter(0.25f)
                                                   : meterColour.brighter(0.42f)).withAlpha(0.96f));
                g.fillRect(peakAreas[ch].getX(), holdY, peakAreas[ch].getWidth(), 1.25f);
            }
        }
    }

    g.setFont(8.5f);
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    if (gr)
    {
        for (float mark : grMarks)
        {
            const float y = levelToY(mark);
            g.drawLine(scaleArea.getX(), y, scaleArea.getX() + 3.0f, y, 1.0f);
            g.drawText(grDbLabel(mark), juce::Rectangle<float>(scaleArea.getX() + 3.0f, y - 6.0f, 15.0f, 12.0f), juce::Justification::centredLeft);
        }
    }
    else
    {
        static constexpr float marks[] = { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -30.0f, -48.0f, -60.0f };
        for (float mark : marks)
        {
            const float y = levelToY(mark);
            g.drawLine(scaleArea.getX(), y, scaleArea.getX() + 3.0f, y, 1.0f);
            const juce::String text = mark > 0.0f ? "+" + juce::String((int) mark) : juce::String((int) mark);
            g.drawText(text, juce::Rectangle<float>(scaleArea.getX() + 3.0f, y - 6.0f, 15.0f, 12.0f), juce::Justification::centredLeft);
        }
    }

    if (!gr)
        g.drawImage(inOutMeterEmbosImage(), getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

void QuanpressorAudioProcessorEditor::VuMeter::paint(juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    const bool showGainReduction = mode == Mode::gainReduction;
    VuScaleRenderer::draw(g, full, showGainReduction ? VuScaleRenderer::Mode::gainReduction
                                                      : VuScaleRenderer::Mode::level);
    const auto needleCentre = juce::Point<float>(full.getCentreX(), full.getY() + full.getHeight() * 0.88f);
    const auto scaleCentre = juce::Point<float>(full.getCentreX(), full.getY() + full.getHeight() * 0.68f);
    const float scaleRadiusX = full.getWidth() * 0.47f;
    const float scaleRadiusY = full.getHeight() * 0.64f;
    const auto outerDistanceForAngle = [needleCentre, scaleCentre, scaleRadiusX, scaleRadiusY](float angle)
    {
        const auto direction = juce::Point<float>(std::cos(angle), std::sin(angle));
        const auto origin = needleCentre - scaleCentre;
        const float rx2 = scaleRadiusX * scaleRadiusX;
        const float ry2 = scaleRadiusY * scaleRadiusY;
        const float a = direction.x * direction.x / rx2 + direction.y * direction.y / ry2;
        const float b = 2.0f * (origin.x * direction.x / rx2 + origin.y * direction.y / ry2);
        const float c = origin.x * origin.x / rx2 + origin.y * origin.y / ry2 - 1.0f;
        return (-b + std::sqrt(juce::jmax(0.0f, b * b - 4.0f * a * c))) / (2.0f * a);
    };

    float vu = -60.0f;
    if (mode == Mode::input)
        vu = inputValue != nullptr ? inputValue->load(std::memory_order_relaxed) : -60.0f;
    else if (mode == Mode::gainReduction)
        vu = grValue != nullptr ? grValue->load(std::memory_order_relaxed) : 0.0f;
    else
        vu = outputValue != nullptr ? outputValue->load(std::memory_order_relaxed) : -60.0f;

    const float angle = showGainReduction ? VuScaleRenderer::angleForGainReduction(vu)
                                          : VuScaleRenderer::angleForVu(vu);
    const auto tip = needleCentre + juce::Point<float>(std::cos(angle), std::sin(angle))
        * outerDistanceForAngle(angle);
    g.setColour(juce::Colours::black.withAlpha(0.86f));
    g.drawLine({ needleCentre, tip }, 1.9f);
    g.fillEllipse(needleCentre.x - 2.8f, needleCentre.y - 2.8f, 5.6f, 5.6f);
}

void QuanpressorAudioProcessorEditor::Graph::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(22, 24, 27));
    auto a = getLocalBounds().toFloat().reduced(18.0f);

    g.setColour(juce::Colours::grey.withAlpha(0.25f));
    for (int i = 0; i < 7; ++i)
    {
        const float x = a.getX() + a.getWidth() * i / 6.0f;
        const float y = a.getY() + a.getHeight() * i / 6.0f;
        g.drawVerticalLine(static_cast<int>(x), a.getY(), a.getBottom());
        g.drawHorizontalLine(static_cast<int>(y), a.getX(), a.getRight());
    }

    g.setFont(8.5f);
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    static constexpr float graphMarks[] = { -48.0f, -36.0f, -24.0f, -12.0f, 0.0f, 12.0f };
    for (float mark : graphMarks)
    {
        const float x = juce::jmap(mark, -48.0f, 12.0f, a.getX(), a.getRight());
        const float y = juce::jmap(mark, -48.0f, 12.0f, a.getBottom(), a.getY());
        g.drawText(juce::String((int) mark), juce::Rectangle<float>(x - 14.0f, a.getBottom() - 12.0f, 28.0f, 11.0f), juce::Justification::centred);
        g.drawText(juce::String((int) mark), juce::Rectangle<float>(a.getX(), y - 6.0f, 26.0f, 12.0f), juce::Justification::centredLeft);
    }

    const float th = p.meters.effectiveThresholdDb.load();
    float rat = p.apvts.getRawParameterValue("ratio")->load();
    const float kneeDb = p.apvts.getRawParameterValue("knee")->load();
    const int modeValue = static_cast<int>(p.apvts.getRawParameterValue("mode")->load());
    if (modeValue == QuanpressorDSP::FET)
        rat = QuanpressorDSP::fetRatioValue(rat);
    const bool kneeAvailable = modeValue == QuanpressorDSP::VCA;
    const float k = kneeAvailable ? kneeDb : (modeValue == QuanpressorDSP::FET ? 6.0f : 10.0f);

    juce::Path fullPath;
    juce::Path activePath;
    const float targetInputDb = juce::jlimit(-48.0f, 12.0f,
        p.meters.graphInputDb.load(std::memory_order_relaxed));
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (!displayStateInitialised)
    {
        displayedInputDb = targetInputDb;
        displayStateInitialised = true;
    }
    else
    {
        constexpr float displayTimeSeconds = 0.065f;
        const float elapsedSeconds = juce::jlimit(0.0f, 0.1f,
            static_cast<float>((nowMs - lastDisplayUpdateMs) * 0.001));
        const float alpha = 1.0f - std::exp(-elapsedSeconds / displayTimeSeconds);
        displayedInputDb += alpha * (targetInputDb - displayedInputDb);
    }

    lastDisplayUpdateMs = nowMs;
    const float currentInput = juce::jlimit(-48.0f, 12.0f, displayedInputDb);
    bool activeStarted = false;
    bool activeEnded = false;

    for (int i = 0; i <= 180; ++i)
    {
        const float in = -48.0f + 60.0f * static_cast<float>(i) / 180.0f;
        const float out = transferOutput(in, th, rat, k, modeValue);
        const float px = juce::jmap(in, -48.0f, 12.0f, a.getX(), a.getRight());
        const float staticY = juce::jmap(out, -48.0f, 12.0f, a.getBottom(), a.getY());
        if (i == 0) fullPath.startNewSubPath(px, staticY); else fullPath.lineTo(px, staticY);

        if (in <= currentInput)
        {
            if (!activeStarted) { activePath.startNewSubPath(px, staticY); activeStarted = true; }
            else activePath.lineTo(px, staticY);
        }
        else if (!activeEnded)
        {
            const float exactOut = transferOutput(currentInput, th, rat, k, modeValue);
            const float exactX = juce::jmap(currentInput, -48.0f, 12.0f, a.getX(), a.getRight());
            const float exactY = juce::jmap(exactOut, -48.0f, 12.0f, a.getBottom(), a.getY());
            if (!activeStarted) activePath.startNewSubPath(exactX, exactY);
            else activePath.lineTo(exactX, exactY);
            activeEnded = true;
        }
    }

    g.setColour(teal().withAlpha(0.38f));
    g.strokePath(fullPath, juce::PathStrokeType(2.0f));
    g.setColour(teal());
    g.strokePath(activePath, juce::PathStrokeType(4.0f));

    // The spot is a static operating point. Attack/release state is shown only
    // by the GR meter and history, so its Y coordinate always remains on curve.
    const float currentOutput = transferOutput(currentInput, th, rat, k, modeValue);
    const float markerX = juce::jmap(currentInput, -48.0f, 12.0f, a.getX(), a.getRight());
    const float markerY = juce::jmap(currentOutput, -48.0f, 12.0f, a.getBottom(), a.getY());
    g.setColour(juce::Colours::white);
    g.fillEllipse(markerX - 5.0f, markerY - 5.0f, 10.0f, 10.0f);

}

void QuanpressorAudioProcessorEditor::HistoryGraph::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(12, 14, 16).withAlpha(0.76f));
    auto a = getLocalBounds().toFloat();
    a.removeFromTop(24.0f);
    a.removeFromBottom(18.0f);
    a.removeFromLeft(19.0f);
    a.removeFromRight(6.0f);

    g.setFont(8.5f);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    for (float mark : grMarks)
    {
        const float y = grDbToY(mark, a);
        g.drawHorizontalLine(static_cast<int>(y), a.getX(), a.getRight());
        g.drawText(grDbLabel(mark), juce::Rectangle<float>(a.getX(), y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft);
    }

    juce::Path hp;
    const int wi = p.meters.writeIndex.load();
    for (int i = 0; i < 160; ++i)
    {
        const float v = p.meters.history[static_cast<size_t>((wi + i) % 160)].load();
        const float x = a.getX() + a.getWidth() * static_cast<float>(i) / 159.0f;
        const float y = grDbToY(v, a);
        if (i == 0) hp.startNewSubPath(x, y); else hp.lineTo(x, y);
    }
    g.setColour(juce::Colours::orange);
    g.strokePath(hp, juce::PathStrokeType(1.5f));
}

void QuanpressorAudioProcessorEditor::EmbosOverlay::paint(juce::Graphics& g)
{
    const auto destination = getLocalBounds().toFloat();
    if (kind == Kind::GrCurve)
        g.drawImage(grCurveEmbosImage(), destination, juce::RectanglePlacement::stretchToFit);
    else
        g.drawImage(grHistoryEmbosImage(), destination, juce::RectanglePlacement::stretchToFit);
}

void QuanpressorAudioProcessorEditor::GrMatchingOverlay::paint(juce::Graphics& g)
{
    if (matching == nullptr || !matching->load(std::memory_order_relaxed)
        || ((juce::Time::getMillisecondCounter() / 400u) % 2u != 0u))
        return;

    const float bandHeight = 27.0f;
    const auto matchingArea = juce::Rectangle<float>(0.0f, getHeight() * 0.5f - bandHeight * 0.5f,
                                                     static_cast<float>(getWidth()), bandHeight);
    g.setColour(juce::Colours::black.withAlpha(0.42f));
    g.fillRect(matchingArea);
    g.setColour(juce::Colours::white.withAlpha(0.96f));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("GR Matching...", matchingArea, juce::Justification::centred, false);
}
