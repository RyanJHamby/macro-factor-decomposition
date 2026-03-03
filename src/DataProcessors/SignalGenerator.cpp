//
//  SignalGenerator.cpp
//  InvertedYieldCurveTrader
//
//  Signal generation from macro regime and factor decomposition
//
//  Created by Ryan Hamby on 1/15/26.
//

#include "SignalGenerator.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>

TradeSignal SignalGenerator::generateSignal(
    const MacroRegime& regime,
    const MacroFactors& factors,
    const RiskDecomposition& riskDecomp)
{
    double growthFactor = extractGrowthFactor(factors);
    double volFactor = extractVolatilityFactor(factors);

    return generateSignalFromObservables(
        regime.vixLevel,
        regime.yieldCurveSlope,
        growthFactor,
        volFactor,
        regime
    );
}

TradeSignal SignalGenerator::generateSignalFromObservables(
    double vixLevel,
    double yieldCurveSlope,
    double growthFactorValue,
    double volatilityFactorValue,
    const MacroRegime& regime)
{
    TradeSignal signal;
    signal.regime = regime;
    signal.growthFactorScore = growthFactorValue;
    signal.volatilityFactorScore = volatilityFactorValue;
    signal.curveScore = yieldCurveSlope;

    // Count bullish and bearish signals
    int bullishCount = 0;
    int bearishCount = 0;
    std::vector<std::string> factors;

    // Regime signal
    double regimeScore = 0.5;
    if (regime.riskLabel == "Risk-On") {
        bullishCount++;
        regimeScore = 1.0;
        factors.push_back("Risk-On regime");
    } else if (regime.riskLabel == "Risk-Off") {
        bearishCount++;
        regimeScore = 1.0;
        factors.push_back("Risk-Off regime");
    }

    // VIX signal
    double vixScore = 0.0;
    if (vixLevel < VIX_LONG_THRESHOLD) {
        bullishCount++;
        vixScore = std::min(1.0, (VIX_LONG_THRESHOLD - vixLevel) / 10.0);
        factors.push_back("Low VIX (" + std::to_string(static_cast<int>(vixLevel)) + ")");
    } else if (vixLevel > VIX_SHORT_THRESHOLD) {
        bearishCount++;
        vixScore = std::min(1.0, (vixLevel - VIX_SHORT_THRESHOLD) / 20.0);
        factors.push_back("High VIX (" + std::to_string(static_cast<int>(vixLevel)) + ")");
    }

    // Growth factor signal
    double growthScore = 0.0;
    if (growthFactorValue > 0.0) {
        bullishCount++;
        growthScore = std::min(1.0, std::abs(growthFactorValue));
        factors.push_back("Positive growth factor");
    } else if (growthFactorValue < 0.0) {
        bearishCount++;
        growthScore = std::min(1.0, std::abs(growthFactorValue));
        factors.push_back("Negative growth factor");
    }

    // Yield curve signal
    double curveSignalScore = 0.0;
    if (yieldCurveSlope > CURVE_STEEP_THRESHOLD) {
        bullishCount++;
        curveSignalScore = std::min(1.0, (yieldCurveSlope - CURVE_STEEP_THRESHOLD) / 100.0);
        factors.push_back("Steep yield curve");
    } else if (yieldCurveSlope < CURVE_INVERTED_THRESHOLD) {
        bearishCount++;
        curveSignalScore = std::min(1.0, std::abs(yieldCurveSlope) / 100.0);
        factors.push_back("Inverted yield curve");
    }

    // Volatility factor signal
    double volScore = 0.0;
    if (volatilityFactorValue < 0.0) {
        bullishCount++;
        volScore = std::min(1.0, std::abs(volatilityFactorValue));
    } else if (volatilityFactorValue > 0.0) {
        bearishCount++;
        volScore = std::min(1.0, volatilityFactorValue);
    }

    // Compute conviction
    double conviction = computeConviction(regimeScore, growthScore, vixScore, curveSignalScore, volScore);

    // Determine direction
    if (bullishCount >= 3 && bearishCount <= 1 && conviction >= MIN_CONVICTION) {
        signal.direction = SignalDirection::LONG;
        signal.entryReason = "Bullish macro environment: " + std::to_string(bullishCount) + "/4 signals positive";
    } else if (bearishCount >= 3 && bullishCount <= 1 && conviction >= MIN_CONVICTION) {
        signal.direction = SignalDirection::SHORT;
        signal.entryReason = "Bearish macro environment: " + std::to_string(bearishCount) + "/4 signals negative";
    } else {
        signal.direction = SignalDirection::FLAT;
        signal.entryReason = "Ambiguous signals (bull=" + std::to_string(bullishCount)
                           + ", bear=" + std::to_string(bearishCount) + ")";
        conviction *= 0.5;  // Reduce conviction for ambiguous
    }

    signal.conviction = conviction;
    signal.factorsContributing = factors;

    return signal;
}

double SignalGenerator::computeConviction(
    double regimeScore,
    double growthScore,
    double vixScore,
    double curveScore,
    double volScore)
{
    double raw = REGIME_WEIGHT * regimeScore
               + GROWTH_WEIGHT * growthScore
               + VIX_SIGNAL_WEIGHT * vixScore
               + CURVE_SIGNAL_WEIGHT * curveScore
               + VOL_FACTOR_WEIGHT * volScore;

    return std::min(1.0, std::max(0.0, raw));
}

double SignalGenerator::extractGrowthFactor(const MacroFactors& factors)
{
    for (int k = 0; k < factors.numFactors; k++) {
        if (factors.factorLabels[k] == "Growth") {
            // Use the latest factor value if available
            if (!factors.factors.empty() && factors.factors[k].size() > 0) {
                return factors.factors[k](factors.factors[k].size() - 1);
            }
            // Fall back to variance as a proxy for factor strength
            return (factors.factorVariances[k] > 0.5) ? 0.5 : -0.5;
        }
    }
    return 0.0;
}

double SignalGenerator::extractVolatilityFactor(const MacroFactors& factors)
{
    for (int k = 0; k < factors.numFactors; k++) {
        if (factors.factorLabels[k] == "Volatility") {
            if (!factors.factors.empty() && factors.factors[k].size() > 0) {
                return factors.factors[k](factors.factors[k].size() - 1);
            }
            return (factors.factorVariances[k] > 0.5) ? 0.5 : -0.5;
        }
    }
    return 0.0;
}
