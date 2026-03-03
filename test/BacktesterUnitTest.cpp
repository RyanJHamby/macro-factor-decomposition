//
//  BacktesterUnitTest.cpp
//  InvertedYieldCurveTrader
//
//  Unit tests for Backtester
//
//  Created by Ryan Hamby on 1/15/26.
//

#include <gtest/gtest.h>
#include "../src/DataProcessors/Backtester.hpp"
#include <cmath>
#include <numeric>

class BacktesterTest : public ::testing::Test {};

TEST_F(BacktesterTest, SharpeCalculationPositive) {
    // Consistent positive returns should give positive Sharpe
    std::vector<double> returns(252, 0.001);  // 0.1% per day
    double sharpe = Backtester::computeSharpe(returns, 0.04);
    EXPECT_GT(sharpe, 0.0);
}

TEST_F(BacktesterTest, SharpeCalculationNegative) {
    // Consistent negative returns should give negative Sharpe
    std::vector<double> returns(252, -0.001);
    double sharpe = Backtester::computeSharpe(returns, 0.04);
    EXPECT_LT(sharpe, 0.0);
}

TEST_F(BacktesterTest, SharpeEmptyReturns) {
    std::vector<double> returns;
    double sharpe = Backtester::computeSharpe(returns, 0.04);
    EXPECT_NEAR(sharpe, 0.0, 0.001);
}

TEST_F(BacktesterTest, MaxDrawdownBasic) {
    std::vector<double> values = {100, 110, 105, 115, 90, 95, 120};
    double maxDD = Backtester::computeMaxDrawdown(values);

    // Peak = 115, trough = 90, DD = 25/115 = 21.7%
    EXPECT_NEAR(maxDD, 25.0 / 115.0, 0.01);
}

TEST_F(BacktesterTest, MaxDrawdownNoDrawdown) {
    std::vector<double> values = {100, 105, 110, 115, 120};
    double maxDD = Backtester::computeMaxDrawdown(values);
    EXPECT_NEAR(maxDD, 0.0, 0.001);
}

TEST_F(BacktesterTest, MaxDrawdownEmpty) {
    std::vector<double> values;
    double maxDD = Backtester::computeMaxDrawdown(values);
    EXPECT_NEAR(maxDD, 0.0, 0.001);
}

TEST_F(BacktesterTest, SyntheticHistoryLength) {
    auto history = Backtester::generateSyntheticHistory(17, 252);
    EXPECT_EQ(static_cast<int>(history.size()), 17 * 252);
}

TEST_F(BacktesterTest, SyntheticHistoryHasRegimes) {
    auto history = Backtester::generateSyntheticHistory(5, 252);

    bool hasRiskOn = false;
    bool hasRiskOff = false;

    for (const auto& point : history) {
        if (point.regime.riskLabel == "Risk-On") hasRiskOn = true;
        if (point.regime.riskLabel == "Risk-Off") hasRiskOff = true;
    }

    // 5-year dataset should contain both regimes
    EXPECT_TRUE(hasRiskOn);
}

TEST_F(BacktesterTest, RunBacktestProducesResults) {
    auto history = Backtester::generateSyntheticHistory(3, 252);

    BacktestConfig config;
    config.initialCapital = 10000000.0;
    config.baseNotional = 5000000.0;

    BacktestResult result = Backtester::run(config, history);

    EXPECT_GT(result.numTradingDays, 0);
    EXPECT_GT(result.numTrades, 0);
    EXPECT_FALSE(result.dailyPnls.empty());
}

TEST_F(BacktesterTest, RunBacktestReturnsValid) {
    auto history = Backtester::generateSyntheticHistory(5, 252);

    BacktestConfig config;
    config.initialCapital = 10000000.0;
    config.baseNotional = 5000000.0;

    BacktestResult result = Backtester::run(config, history);

    // Sharpe should be finite
    EXPECT_TRUE(std::isfinite(result.sharpeRatio));

    // Max drawdown should be between 0 and 1
    EXPECT_GE(result.maxDrawdown, 0.0);
    EXPECT_LE(result.maxDrawdown, 1.0);

    // Win rate should be between 0 and 1
    EXPECT_GE(result.winRate, 0.0);
    EXPECT_LE(result.winRate, 1.0);
}

TEST_F(BacktesterTest, BacktestDrawdownBounded) {
    auto history = Backtester::generateSyntheticHistory(10, 252);

    BacktestConfig config;
    config.initialCapital = 10000000.0;
    config.baseNotional = 5000000.0;

    BacktestResult result = Backtester::run(config, history);

    // With risk management, drawdown should be bounded
    EXPECT_LT(result.maxDrawdown, 0.5);  // Less than 50% drawdown
}

TEST_F(BacktesterTest, EmptyHistoryReturnsEmpty) {
    std::vector<HistoricalDataPoint> empty;
    BacktestConfig config;

    BacktestResult result = Backtester::run(config, empty);

    EXPECT_EQ(result.numTradingDays, 0);
    EXPECT_TRUE(result.dailyPnls.empty());
}
