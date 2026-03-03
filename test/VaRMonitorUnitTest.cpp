//
//  VaRMonitorUnitTest.cpp
//  InvertedYieldCurveTrader
//
//  Unit tests for VaRMonitor
//
//  Created by Ryan Hamby on 1/15/26.
//

#include <gtest/gtest.h>
#include "../src/DataProcessors/VaRMonitor.hpp"
#include <cmath>

class VaRMonitorTest : public ::testing::Test {
protected:
    static RiskDecomposition createRiskDecomp() {
        RiskDecomposition risk;
        risk.numFactors = 3;
        risk.factorLabels = {"Growth", "Inflation", "Volatility"};
        risk.factorSensitivities = {0.6, -0.2, -0.8};
        risk.factorRiskContributions = {0.25, 0.08, 0.15};
        risk.componentContributions = {0.52, 0.17, 0.31};
        risk.totalRisk = 0.012;
        risk.totalVariance = 0.000144;
        return risk;
    }
};

// ===== Breach Detection Tests =====

TEST_F(VaRMonitorTest, DetectsVaRBreach) {
    // Loss exceeds VaR
    EXPECT_TRUE(VaRMonitor::checkVaRBreach(-150000.0, 100000.0));
}

TEST_F(VaRMonitorTest, NoBreachWhenLossWithinVaR) {
    EXPECT_FALSE(VaRMonitor::checkVaRBreach(-50000.0, 100000.0));
}

TEST_F(VaRMonitorTest, NoBreachOnProfit) {
    EXPECT_FALSE(VaRMonitor::checkVaRBreach(50000.0, 100000.0));
}

TEST_F(VaRMonitorTest, NoBreachWithZeroVaR) {
    EXPECT_FALSE(VaRMonitor::checkVaRBreach(-50000.0, 0.0));
}

// ===== Expected Shortfall Tests =====

TEST_F(VaRMonitorTest, ExpectedShortfallPositive) {
    auto risk = createRiskDecomp();
    double es = VaRMonitor::computeExpectedShortfall(5000000.0, risk, 0.95);

    EXPECT_GT(es, 0.0);
}

TEST_F(VaRMonitorTest, ExpectedShortfallGreaterThanVaR) {
    auto risk = createRiskDecomp();
    double position = 5000000.0;

    double var95 = PositionSizer::computeDailyVaR(position, risk, 0.95);
    double es95 = VaRMonitor::computeExpectedShortfall(position, risk, 0.95);

    // ES should always exceed VaR
    EXPECT_GT(es95, var95);
}

TEST_F(VaRMonitorTest, ExpectedShortfallScalesWithPosition) {
    auto risk = createRiskDecomp();

    double es1 = VaRMonitor::computeExpectedShortfall(1000000.0, risk, 0.95);
    double es5 = VaRMonitor::computeExpectedShortfall(5000000.0, risk, 0.95);

    EXPECT_NEAR(es5, es1 * 5.0, es1 * 0.1);
}

// ===== Monitoring History Tests =====

TEST_F(VaRMonitorTest, TrackBreachRate) {
    VaRMonitor monitor;
    auto risk = createRiskDecomp();

    // Add 100 observations, ~5 breaches
    for (int i = 0; i < 100; i++) {
        double pnl = (i % 20 == 0) ? -200000.0 : -10000.0;  // 5 breaches
        monitor.updatePosition("2024-01-" + std::to_string(i + 1),
                               5000000.0, pnl, risk);
    }

    double breachRate = monitor.getVaRBreachRate();
    EXPECT_GT(breachRate, 0.0);
    EXPECT_LT(breachRate, 1.0);
}

TEST_F(VaRMonitorTest, EmptyHistoryZeroBreachRate) {
    VaRMonitor monitor;
    EXPECT_NEAR(monitor.getVaRBreachRate(), 0.0, 0.001);
}

TEST_F(VaRMonitorTest, GenerateReport) {
    VaRMonitor monitor;
    auto risk = createRiskDecomp();

    for (int i = 0; i < 50; i++) {
        double pnl = (i % 10 == 0) ? -200000.0 : -5000.0;
        monitor.updatePosition("2024-01-" + std::to_string(i + 1),
                               5000000.0, pnl, risk);
    }

    VaRReport report = monitor.generateReport();
    EXPECT_EQ(report.totalObservations, 50);
    EXPECT_GE(report.breachRate, 0.0);
    EXPECT_LE(report.breachRate, 1.0);
}

TEST_F(VaRMonitorTest, HistoricalESComputed) {
    VaRMonitor monitor;
    auto risk = createRiskDecomp();

    // Add some breach observations
    monitor.updatePosition("2024-01-01", 5000000.0, -200000.0, risk);
    monitor.updatePosition("2024-01-02", 5000000.0, -5000.0, risk);
    monitor.updatePosition("2024-01-03", 5000000.0, -300000.0, risk);

    double es = monitor.computeHistoricalES();
    // At least some breaches should produce positive ES
    EXPECT_GE(es, 0.0);
}

TEST_F(VaRMonitorTest, ResetClearsHistory) {
    VaRMonitor monitor;
    auto risk = createRiskDecomp();

    monitor.updatePosition("2024-01-01", 5000000.0, -10000.0, risk);
    EXPECT_EQ(static_cast<int>(monitor.getHistory().size()), 1);

    monitor.reset();
    EXPECT_EQ(static_cast<int>(monitor.getHistory().size()), 0);
}
