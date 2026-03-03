//
//  KellyCriterionUnitTest.cpp
//  InvertedYieldCurveTrader
//
//  Unit tests for Kelly criterion position sizing
//
//  Created by Ryan Hamby on 1/15/26.
//

#include <gtest/gtest.h>
#include "../src/DataProcessors/PositionSizer.hpp"
#include <cmath>

class KellyCriterionTest : public ::testing::Test {};

// ===== Kelly Fraction Tests =====

TEST_F(KellyCriterionTest, BasicKellyFormula) {
    // 60% win rate, avg win = $2, avg loss = $1
    // R = 2/1 = 2
    // f* = (0.6 * 2 - 0.4) / 2 = 0.4
    // half-Kelly = 0.2
    double fraction = PositionSizer::computeKellyFraction(0.6, 2.0, 1.0);
    EXPECT_NEAR(fraction, 0.2, 0.01);
}

TEST_F(KellyCriterionTest, FiftyFiftyWithEqualWinLoss) {
    // 50% win rate, equal avg win/loss = no edge
    // f* = (0.5 * 1 - 0.5) / 1 = 0
    double fraction = PositionSizer::computeKellyFraction(0.5, 1.0, 1.0);
    EXPECT_NEAR(fraction, 0.0, 0.01);
}

TEST_F(KellyCriterionTest, NegativeEdgeReturnsZero) {
    // 30% win rate, equal win/loss = negative edge
    double fraction = PositionSizer::computeKellyFraction(0.3, 1.0, 1.0);
    EXPECT_NEAR(fraction, 0.0, 0.01);
}

TEST_F(KellyCriterionTest, HalfKellyApplied) {
    // High-edge scenario
    // 70% win rate, R = 3
    // f* = (0.7 * 3 - 0.3) / 3 = 0.6
    // half-Kelly = 0.3 (but capped at 0.25)
    double fraction = PositionSizer::computeKellyFraction(0.7, 3.0, 1.0);
    EXPECT_LE(fraction, 0.25);  // Capped at 25%
    EXPECT_GT(fraction, 0.0);
}

TEST_F(KellyCriterionTest, CappedAtTwentyFivePercent) {
    // Very high edge should still be capped
    double fraction = PositionSizer::computeKellyFraction(0.9, 10.0, 1.0);
    EXPECT_LE(fraction, 0.25);
}

TEST_F(KellyCriterionTest, InvalidWinRate) {
    EXPECT_NEAR(PositionSizer::computeKellyFraction(0.0, 1.0, 1.0), 0.0, 0.01);
    EXPECT_NEAR(PositionSizer::computeKellyFraction(1.0, 1.0, 1.0), 0.0, 0.01);
    EXPECT_NEAR(PositionSizer::computeKellyFraction(-0.5, 1.0, 1.0), 0.0, 0.01);
}

TEST_F(KellyCriterionTest, InvalidWinLossAmounts) {
    EXPECT_NEAR(PositionSizer::computeKellyFraction(0.6, 0.0, 1.0), 0.0, 0.01);
    EXPECT_NEAR(PositionSizer::computeKellyFraction(0.6, 1.0, 0.0), 0.0, 0.01);
    EXPECT_NEAR(PositionSizer::computeKellyFraction(0.6, -1.0, 1.0), 0.0, 0.01);
}

// ===== Win Rate Estimation Tests =====

TEST_F(KellyCriterionTest, EstimateWinRateBasic) {
    std::vector<double> trades = {100, -50, 200, -30, 150, -80, 50};

    double winRate, avgWin, avgLoss;
    PositionSizer::estimateWinRate(trades, winRate, avgWin, avgLoss);

    // 4 wins, 3 losses
    EXPECT_NEAR(winRate, 4.0 / 7.0, 0.01);
    EXPECT_NEAR(avgWin, (100 + 200 + 150 + 50) / 4.0, 0.01);
    EXPECT_NEAR(avgLoss, (50 + 30 + 80) / 3.0, 0.01);
}

TEST_F(KellyCriterionTest, EstimateWinRateAllWins) {
    std::vector<double> trades = {100, 200, 150};

    double winRate, avgWin, avgLoss;
    PositionSizer::estimateWinRate(trades, winRate, avgWin, avgLoss);

    EXPECT_NEAR(winRate, 1.0, 0.01);
    EXPECT_GT(avgWin, 0.0);
    EXPECT_NEAR(avgLoss, 0.0, 0.01);
}

TEST_F(KellyCriterionTest, EstimateWinRateAllLosses) {
    std::vector<double> trades = {-100, -200, -150};

    double winRate, avgWin, avgLoss;
    PositionSizer::estimateWinRate(trades, winRate, avgWin, avgLoss);

    EXPECT_NEAR(winRate, 0.0, 0.01);
    EXPECT_NEAR(avgWin, 0.0, 0.01);
    EXPECT_GT(avgLoss, 0.0);
}

TEST_F(KellyCriterionTest, EstimateWinRateEmpty) {
    std::vector<double> trades;

    double winRate, avgWin, avgLoss;
    PositionSizer::estimateWinRate(trades, winRate, avgWin, avgLoss);

    EXPECT_NEAR(winRate, 0.0, 0.01);
    EXPECT_NEAR(avgWin, 0.0, 0.01);
    EXPECT_NEAR(avgLoss, 0.0, 0.01);
}

// ===== Kelly Position Sizing Integration Tests =====

TEST_F(KellyCriterionTest, KellyPositionWithHistory) {
    // Create trade history with positive edge
    std::vector<double> tradeHistory;
    for (int i = 0; i < 50; i++) {
        if (i % 3 == 0) {
            tradeHistory.push_back(-0.005);  // Loss
        } else {
            tradeHistory.push_back(0.008);   // Win
        }
    }

    RiskDecomposition risk;
    risk.numFactors = 3;
    risk.factorLabels = {"Growth", "Inflation", "Volatility"};
    risk.factorSensitivities = {0.6, -0.2, -0.8};
    risk.factorRiskContributions = {0.25, 0.08, 0.15};
    risk.componentContributions = {0.52, 0.17, 0.31};
    risk.totalRisk = 0.012;
    risk.totalVariance = 0.000144;

    MacroRegime regime;
    regime.vixLevel = 15.0;
    regime.moveIndex = 90.0;
    regime.creditSpread = 100.0;
    regime.yieldCurveSlope = 120.0;
    regime.putCallRatio = 0.9;
    regime.riskLabel = "Neutral";
    regime.inflationLabel = "Balanced";
    regime.fragileLabel = "Stable";
    regime.confidence = 0.70;
    regime.volatilityMultiplier = 1.1;

    PositionConstraint constraints;
    constraints.maxNotional = 10000000.0;
    constraints.maxLeverageMultiple = 2.0;
    constraints.maxDailyLoss = 100000.0;
    constraints.maxDrawdownFromPeak = 500000.0;

    PositionSizing sizing = PositionSizer::computeKellyPositionSize(
        5000000.0, risk, regime, constraints, tradeHistory
    );

    EXPECT_GT(sizing.recommendedNotional, 0.0);
    EXPECT_LE(sizing.recommendedNotional, constraints.maxNotional);
}

TEST_F(KellyCriterionTest, KellyFallsBackWithoutHistory) {
    std::vector<double> shortHistory = {0.01, -0.005};  // Too short

    RiskDecomposition risk;
    risk.numFactors = 3;
    risk.factorLabels = {"Growth", "Inflation", "Volatility"};
    risk.factorSensitivities = {0.6, -0.2, -0.8};
    risk.factorRiskContributions = {0.25, 0.08, 0.15};
    risk.componentContributions = {0.52, 0.17, 0.31};
    risk.totalRisk = 0.012;
    risk.totalVariance = 0.000144;

    MacroRegime regime;
    regime.vixLevel = 15.0;
    regime.moveIndex = 90.0;
    regime.creditSpread = 100.0;
    regime.yieldCurveSlope = 120.0;
    regime.putCallRatio = 0.9;
    regime.riskLabel = "Neutral";
    regime.inflationLabel = "Balanced";
    regime.fragileLabel = "Stable";
    regime.confidence = 0.70;
    regime.volatilityMultiplier = 1.1;

    PositionConstraint constraints;
    constraints.maxNotional = 10000000.0;
    constraints.maxLeverageMultiple = 2.0;
    constraints.maxDailyLoss = 100000.0;
    constraints.maxDrawdownFromPeak = 500000.0;

    // Should fall back to vol-scaling (same as computePositionSize)
    PositionSizing kellySizing = PositionSizer::computeKellyPositionSize(
        5000000.0, risk, regime, constraints, shortHistory
    );
    PositionSizing volSizing = PositionSizer::computePositionSize(
        5000000.0, risk, regime, constraints
    );

    EXPECT_NEAR(kellySizing.recommendedNotional, volSizing.recommendedNotional, 1.0);
}
