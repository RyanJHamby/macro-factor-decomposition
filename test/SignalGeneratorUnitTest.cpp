//
//  SignalGeneratorUnitTest.cpp
//  InvertedYieldCurveTrader
//
//  Unit tests for SignalGenerator
//
//  Created by Ryan Hamby on 1/15/26.
//

#include <gtest/gtest.h>
#include "../src/DataProcessors/SignalGenerator.hpp"

class SignalGeneratorTest : public ::testing::Test {
protected:
    static MacroRegime createRiskOnRegime() {
        MacroRegime regime;
        regime.vixLevel = 12.0;
        regime.moveIndex = 85.0;
        regime.creditSpread = 90.0;
        regime.yieldCurveSlope = 120.0;
        regime.putCallRatio = 0.85;
        regime.riskLabel = "Risk-On";
        regime.inflationLabel = "Growth-Sensitive";
        regime.fragileLabel = "Stable";
        regime.confidence = 0.85;
        regime.volatilityMultiplier = 1.0;
        return regime;
    }

    static MacroRegime createRiskOffRegime() {
        MacroRegime regime;
        regime.vixLevel = 40.0;
        regime.moveIndex = 140.0;
        regime.creditSpread = 300.0;
        regime.yieldCurveSlope = -30.0;
        regime.putCallRatio = 1.3;
        regime.riskLabel = "Risk-Off";
        regime.inflationLabel = "Inflation-Sensitive";
        regime.fragileLabel = "Fragile";
        regime.confidence = 0.90;
        regime.volatilityMultiplier = 2.5;
        return regime;
    }

    static MacroRegime createNeutralRegime() {
        MacroRegime regime;
        regime.vixLevel = 22.0;
        regime.moveIndex = 100.0;
        regime.creditSpread = 150.0;
        regime.yieldCurveSlope = 40.0;
        regime.putCallRatio = 1.0;
        regime.riskLabel = "Neutral";
        regime.inflationLabel = "Balanced";
        regime.fragileLabel = "Stable";
        regime.confidence = 0.65;
        regime.volatilityMultiplier = 1.3;
        return regime;
    }
};

TEST_F(SignalGeneratorTest, LongSignalInRiskOn) {
    auto regime = createRiskOnRegime();

    TradeSignal signal = SignalGenerator::generateSignalFromObservables(
        regime.vixLevel, regime.yieldCurveSlope,
        0.8,   // positive growth
        -0.3,  // low vol factor
        regime
    );

    EXPECT_EQ(signal.direction, SignalDirection::LONG);
    EXPECT_GT(signal.conviction, 0.3);
    EXPECT_FALSE(signal.factorsContributing.empty());
}

TEST_F(SignalGeneratorTest, ShortSignalInRiskOff) {
    auto regime = createRiskOffRegime();

    TradeSignal signal = SignalGenerator::generateSignalFromObservables(
        regime.vixLevel, regime.yieldCurveSlope,
        -1.0,  // negative growth
        1.5,   // high vol factor
        regime
    );

    EXPECT_EQ(signal.direction, SignalDirection::SHORT);
    EXPECT_GT(signal.conviction, 0.3);
}

TEST_F(SignalGeneratorTest, FlatSignalInNeutral) {
    auto regime = createNeutralRegime();

    TradeSignal signal = SignalGenerator::generateSignalFromObservables(
        regime.vixLevel, regime.yieldCurveSlope,
        0.1,   // weak growth
        0.1,   // low vol
        regime
    );

    EXPECT_EQ(signal.direction, SignalDirection::FLAT);
}

TEST_F(SignalGeneratorTest, ConvictionBounded) {
    auto regime = createRiskOnRegime();

    double conviction = SignalGenerator::computeConviction(1.0, 1.0, 1.0, 1.0, 1.0);
    EXPECT_LE(conviction, 1.0);
    EXPECT_GE(conviction, 0.0);

    conviction = SignalGenerator::computeConviction(0.0, 0.0, 0.0, 0.0, 0.0);
    EXPECT_GE(conviction, 0.0);
}

TEST_F(SignalGeneratorTest, ConvictionScalesWithStrength) {
    double weak = SignalGenerator::computeConviction(0.5, 0.2, 0.1, 0.1, 0.1);
    double strong = SignalGenerator::computeConviction(1.0, 1.0, 1.0, 1.0, 1.0);

    EXPECT_GT(strong, weak);
}

TEST_F(SignalGeneratorTest, SignalContainsRegime) {
    auto regime = createRiskOnRegime();

    TradeSignal signal = SignalGenerator::generateSignalFromObservables(
        regime.vixLevel, regime.yieldCurveSlope,
        0.5, -0.2, regime
    );

    EXPECT_EQ(signal.regime.riskLabel, "Risk-On");
}
