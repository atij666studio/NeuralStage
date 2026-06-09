#pragma once

namespace ns::UI
{
    constexpr int kAppWidth   = 1400;
    constexpr int kAppHeight  = 940;

    constexpr int kUnit       = 8;
    constexpr int kPad        = 16;

    constexpr int kTopH        = 180;
    constexpr int kRightRailW  = 180;
    constexpr int kLeftRailW   = 180;

    constexpr int kRailKnobCellH = 150;   // uniform knob cell height in side rails
    constexpr int kRailTitleH    = 22;
    constexpr int kRailBrandH    = 30;    // NEURALSTAGE label area at bottom of left rail
    constexpr int kTunerH        = 170;   // tuner inside left rail (roughly = a rail knob cell)

    constexpr int kChainH      = 64;
    constexpr int kChainBlockW = 100;
    constexpr int kChainBlockH = 40;
    constexpr int kChainSpacing = 10;

    constexpr int kBottomH     = 100;

    constexpr int kKnobSize       = 80;
    constexpr int kKnobNameH      = 18;
    constexpr int kKnobValueH     = 16;
    constexpr int kKnobCellH      = kKnobNameH + kKnobSize + kKnobValueH + 4;

    constexpr int kPanelRadius = 16;

    constexpr int kMeterW = 200;
    constexpr int kMeterH = 18;

    constexpr int kSceneBtnW   = 110;
    constexpr int kSceneBtnH   = 56;
    constexpr int kSceneSpacing = 12;
    constexpr int kSceneRadius = 12;
}
