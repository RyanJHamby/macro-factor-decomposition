//
//  VaRMonitor.cpp
//  InvertedYieldCurveTrader
//
//  Automated VaR monitoring implementation
//
//  Created by Ryan Hamby on 1/15/26.
//

#include "VaRMonitor.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

VaRMonitor::VaRMonitor() {}

void VaRMonitor::updatePosition(
    const std::string& date,
    double position,
    double actualPnL,
    const RiskDecomposition& riskDecomp,
    double confidenceLevel)
{
    double predictedVaR = PositionSizer::computeDailyVaR(
        std::abs(position), riskDecomp, confidenceLevel);

    VaRRecord record;
    record.date = date;
    record.predictedVaR = predictedVaR;
    record.actualPnL = actualPnL;
    record.breached = checkVaRBreach(actualPnL, predictedVaR);
    record.position = position;

    history_.push_back(record);

    // Trim history to max size
    while (static_cast<int>(history_.size()) > MAX_HISTORY) {
        history_.pop_front();
    }
}

bool VaRMonitor::checkVaRBreach(double actualPnL, double predictedVaR)
{
    // VaR breach occurs when loss exceeds predicted VaR
    // actualPnL is negative for losses
    return (actualPnL < 0.0 && std::abs(actualPnL) > predictedVaR && predictedVaR > 0.0);
}

double VaRMonitor::getVaRBreachRate() const
{
    if (history_.empty()) return 0.0;

    int breaches = 0;
    for (const auto& record : history_) {
        if (record.breached) breaches++;
    }

    return static_cast<double>(breaches) / history_.size();
}

double VaRMonitor::computeExpectedShortfall(
    double position,
    const RiskDecomposition& riskDecomp,
    double confidenceLevel)
{
    // Parametric ES (under normal distribution)
    // ES = position × σ × φ(z_α) / (1 - α)
    // where φ is the standard normal PDF and z_α is the VaR z-score

    double zScore = (confidenceLevel > 0.95) ? 2.33 : 1.65;
    double dailyRisk = std::abs(position) * riskDecomp.totalRisk;

    // Standard normal PDF at z_α
    double phi = (1.0 / std::sqrt(2.0 * M_PI)) * std::exp(-0.5 * zScore * zScore);

    // ES = σ × φ(z) / (1 - α)
    double es = dailyRisk * phi / (1.0 - confidenceLevel);

    return es;
}

double VaRMonitor::computeHistoricalES() const
{
    double totalBreachLoss = 0.0;
    int breachCount = 0;

    for (const auto& record : history_) {
        if (record.breached) {
            totalBreachLoss += std::abs(record.actualPnL);
            breachCount++;
        }
    }

    return (breachCount > 0) ? totalBreachLoss / breachCount : 0.0;
}

VaRReport VaRMonitor::generateReport() const
{
    VaRReport report;
    report.totalObservations = static_cast<int>(history_.size());
    report.breaches = 0;
    report.worstBreach = 0.0;
    double totalPredictedVaR = 0.0;
    double totalBreachLoss = 0.0;

    for (const auto& record : history_) {
        totalPredictedVaR += record.predictedVaR;

        if (record.breached) {
            report.breaches++;
            double breachMagnitude = std::abs(record.actualPnL);
            totalBreachLoss += breachMagnitude;
            if (breachMagnitude > report.worstBreach) {
                report.worstBreach = breachMagnitude;
            }
        }
    }

    report.breachRate = getVaRBreachRate();
    report.expectedShortfall = computeHistoricalES();
    report.avgPredictedVaR = (report.totalObservations > 0)
                            ? totalPredictedVaR / report.totalObservations : 0.0;
    report.avgRealizedLoss = (report.breaches > 0)
                            ? totalBreachLoss / report.breaches : 0.0;

    // Calibration check
    if (report.totalObservations < 50) {
        report.isCalibrated = true;  // Not enough data to judge
        report.calibrationMessage = "Insufficient data for calibration assessment";
    } else if (report.breachRate < BREACH_RATE_LOW) {
        report.isCalibrated = false;
        report.calibrationMessage = "VaR too conservative (breach rate " +
            std::to_string(report.breachRate * 100.0) + "% < 2%)";
    } else if (report.breachRate > BREACH_RATE_HIGH) {
        report.isCalibrated = false;
        report.calibrationMessage = "VaR too aggressive (breach rate " +
            std::to_string(report.breachRate * 100.0) + "% > 8%)";
    } else {
        report.isCalibrated = true;
        report.calibrationMessage = "VaR well-calibrated (breach rate " +
            std::to_string(report.breachRate * 100.0) + "%)";
    }

    return report;
}

void VaRMonitor::reset()
{
    history_.clear();
}
