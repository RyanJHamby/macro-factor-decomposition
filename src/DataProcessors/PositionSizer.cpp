//
//  PositionSizer.cpp
//  InvertedYieldCurveTrader
//
//  Risk-aware ES position sizing implementation
//
//  Created by Ryan Hamby on 12/25/25.
//

#include "PositionSizer.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iostream>

PositionSizing PositionSizer::computePositionSize(
    double baseNotional,
    const RiskDecomposition& riskDecomp,
    const MacroRegime& regime,
    const PositionConstraint& constraints)
{
    if (baseNotional <= 0.0) {
        throw std::invalid_argument("Base notional must be positive");
    }

    if (riskDecomp.numFactors < 1) {
        throw std::invalid_argument("Risk decomposition must have at least 1 factor");
    }

    // Step 1: Compute volatility multiplier from regime
    double volMultiplier = computeVolatilityMultiplier(regime);

    // Step 2: Adjust position inversely to volatility
    // Core formula: Position = Base × (1 / Vol Multiplier)
    // High vol regime (multiplier=2) → size = base/2
    // Low vol regime (multiplier=1) → size = base/1
    double adjustedNotional = baseNotional / volMultiplier;

    // Step 3: Apply hard constraints
    adjustedNotional = std::min(adjustedNotional, constraints.maxNotional);
    adjustedNotional = std::max(0.0, adjustedNotional);

    // Step 4: Compute daily volatility at this size
    double dailyVol = riskDecomp.totalRisk;
    double expectedDailyVolAtSize = adjustedNotional * dailyVol;

    // Step 5: Check if size is within constraints
    bool withinConstraints = true;
    std::vector<std::string> breaches;

    if (adjustedNotional > constraints.maxNotional) {
        withinConstraints = false;
        breaches.push_back("Exceeds max notional");
    }

    double leverage = (adjustedNotional / baseNotional) * constraints.maxLeverageMultiple;
    if (leverage > constraints.maxLeverageMultiple) {
        withinConstraints = false;
        breaches.push_back("Exceeds leverage limit");
    }

    // Check factor exposure constraints
    for (int k = 0; k < riskDecomp.numFactors; k++) {
        const std::string& label = riskDecomp.factorLabels[k];
        if (constraints.maxFactorExposure.count(label)) {
            double maxExposure = constraints.maxFactorExposure.at(label);
            double actualExposure = std::abs(riskDecomp.factorSensitivities[k]);
            if (actualExposure > maxExposure) {
                withinConstraints = false;
                breaches.push_back("Factor " + label + " exposure too high");
            }
        }
    }

    // Step 6: Build rationale string
    std::ostringstream rationale;
    rationale << "Base=" << baseNotional
              << ", VolMult=" << volMultiplier
              << ", Regime=" << regime.riskLabel
              << ", VIX=" << regime.vixLevel;

    // Step 7: Compute expected losses
    double maxDailyLoss = stressTestPosition(adjustedNotional, riskDecomp);
    double monthlyDrawdown = maxDailyLoss * std::sqrt(21);  // ~21 trading days

    // Construct result
    PositionSizing sizing;
    sizing.recommendedNotional = adjustedNotional;
    sizing.recommendedLeverage = leverage;
    sizing.recommendedShares = adjustedNotional / 5000.0;  // ES ~5000 per contract
    sizing.expectedDailyVol = expectedDailyVolAtSize;
    sizing.expectedWorstCaseDaily = expectedDailyVolAtSize * 2.0;  // 2σ
    sizing.maxMonthlyDrawdown = monthlyDrawdown;
    sizing.rationale = rationale.str();
    sizing.regimeAdjustmentFactor = volMultiplier;
    sizing.currentRegime = regime;
    sizing.isWithinConstraints = withinConstraints;
    sizing.constraintBreaches = breaches;
    sizing.shouldReduceSize = !withinConstraints || (volMultiplier > 1.5);
    sizing.shouldAddHedge = (regime.vixLevel > 25.0) || (volMultiplier > 2.0);
    sizing.hedgeRatio = std::min(0.5, (volMultiplier - 1.0) * 0.25);

    return sizing;
}

MacroRegime PositionSizer::classifyRegime(
    double vixLevel,
    double moveIndex,
    double creditSpread,
    double yieldCurveSlope,
    double putCallRatio)
{
    MacroRegime regime;
    regime.vixLevel = vixLevel;
    regime.moveIndex = moveIndex;
    regime.creditSpread = creditSpread;
    regime.yieldCurveSlope = yieldCurveSlope;
    regime.putCallRatio = putCallRatio;

    // Risk-On/Off classification
    if (vixLevel > VIX_RISK_OFF && creditSpread > SPREAD_WIDE) {
        regime.riskLabel = "Risk-Off";
    } else if (vixLevel < VIX_RISK_ON && creditSpread < SPREAD_TIGHT) {
        regime.riskLabel = "Risk-On";
    } else {
        regime.riskLabel = "Neutral";
    }

    // Inflation/Growth classification
    if (yieldCurveSlope > CURVE_STEEP) {
        regime.inflationLabel = "Growth-Sensitive";
    } else if (yieldCurveSlope < CURVE_FLAT) {
        regime.inflationLabel = "Inflation-Sensitive";
    } else {
        regime.inflationLabel = "Balanced";
    }

    // Stable/Fragile classification
    if (vixLevel > 25.0 || moveIndex > 120.0) {
        regime.fragileLabel = "Fragile";
    } else {
        regime.fragileLabel = "Stable";
    }

    // Volatility multiplier
    regime.volatilityMultiplier = computeVolatilityMultiplier(regime);

    // Confidence
    double confidence = 0.7;  // Default
    if (vixLevel > VIX_RISK_OFF && creditSpread > SPREAD_WIDE) {
        confidence = 0.85;  // Clear risk-off signal
    } else if (vixLevel < VIX_RISK_ON && creditSpread < SPREAD_TIGHT) {
        confidence = 0.80;  // Clear risk-on
    }
    regime.confidence = confidence;

    return regime;
}

std::vector<double> PositionSizer::regimeAdjustedVolatilities(
    const std::vector<double>& baseFactorVariances,
    const MacroRegime& regime)
{
    std::vector<double> adjusted = baseFactorVariances;
    double multiplier = regime.volatilityMultiplier;

    for (auto& var : adjusted) {
        var *= (multiplier * multiplier);  // Variance scales as multiplier²
    }

    return adjusted;
}

double PositionSizer::computeVolatilityMultiplier(const MacroRegime& regime)
{
    // Normalize state variables to [0, 1]
    // VIX: 10 → 0, 50 → 1
    double vixScore = std::min(1.0, std::max(0.0, (regime.vixLevel - 10.0) / 40.0));

    // MOVE: 80 → 0, 150 → 1
    double moveScore = std::min(1.0, std::max(0.0, (regime.moveIndex - 80.0) / 70.0));

    // Spread: 100 → 0, 250 → 1
    double spreadScore = std::min(1.0, std::max(0.0, (regime.creditSpread - 100.0) / 150.0));

    // Curve: -50 → 0, 150 → 1 (inverted = worse = higher score)
    double curveScore = std::min(1.0, std::max(0.0, (CURVE_STEEP - regime.yieldCurveSlope) / CURVE_STEEP));

    // Put/call: 0.8 → 0, 1.2 → 1 (higher ratio = more hedging = higher stress)
    double putcallScore = std::min(1.0, std::max(0.0, (regime.putCallRatio - 0.8) / 0.4));

    // Weighted average
    double stressScore = VIX_WEIGHT * vixScore
                       + MOVE_WEIGHT * moveScore
                       + SPREAD_WEIGHT * spreadScore
                       + CURVE_WEIGHT * curveScore
                       + PUTCALL_WEIGHT * putcallScore;

    // Convert stress score to multiplier
    // stressScore=0 → multiplier=1.0 (calm)
    // stressScore=1 → multiplier=2.5 (crisis)
    double multiplier = 1.0 + 1.5 * stressScore;

    return multiplier;
}

HedgingStrategy PositionSizer::recommendHedge(
    double positionSize,
    const RiskDecomposition& riskDecomp,
    const MacroRegime& regime)
{
    HedgingStrategy hedge;

    // Determine if hedging is needed
    hedge.needsHedge = (regime.vixLevel > 25.0) || (regime.volatilityMultiplier > 1.5);

    if (!hedge.needsHedge) {
        hedge.estimatedCost = 0.0;
        hedge.costPercent = 0.0;
        hedge.rationale = "Market conditions stable, no hedging needed";
        return hedge;
    }

    // Determine hedge ratio based on regime stress
    double stressMultiplier = regime.volatilityMultiplier;
    hedge.hedgeRatio = std::min(0.5, (stressMultiplier - 1.0) * 0.25);

    // Determine hedge instrument based on factor exposure
    double growthExposure = (riskDecomp.numFactors > 0) ? riskDecomp.factorSensitivities[0] : 0.0;
    double volExposure = (riskDecomp.numFactors > 2) ? riskDecomp.factorSensitivities[2] : 0.0;

    if (regime.vixLevel > 40.0) {
        hedge.hedgeInstrument = "VIX_Call";
        hedge.rationale = "Extreme VIX, buy VIX calls for tail protection";
        hedge.hedgedRisks = {"Volatility Spike"};
        hedge.estimatedCost = positionSize * hedge.hedgeRatio * 0.01;  // ~1% annual drag
    } else if (growthExposure > 0.5 && regime.inflationLabel == "Inflation-Sensitive") {
        hedge.hedgeInstrument = "ES_Put_Collar";
        hedge.rationale = "High growth exposure in stagflation risk, buy put collar";
        hedge.hedgedRisks = {"Growth Shock", "Inflation Spike"};
        hedge.estimatedCost = positionSize * hedge.hedgeRatio * 0.005;
    } else {
        hedge.hedgeInstrument = "ES_Put_Spread";
        hedge.rationale = "General downside protection via put spread";
        hedge.hedgedRisks = {"Market Decline"};
        hedge.estimatedCost = positionSize * hedge.hedgeRatio * 0.003;
    }

    hedge.hedgeSize = positionSize * hedge.hedgeRatio;
    hedge.costPercent = (hedge.estimatedCost / positionSize) * 100.0;

    return hedge;
}

double PositionSizer::applyHysteresis(
    double currentSize,
    double recommendedSize,
    const MacroRegime& previousRegime,
    const MacroRegime& currentRegime,
    int daysInCurrentRegime)
{
    // If regime is new (< 3 days), use hysteresis to avoid whipsaws
    if (daysInCurrentRegime < 3) {
        // Only reduce size immediately, increase slowly
        if (recommendedSize < currentSize) {
            return recommendedSize;  // Reduce immediately
        } else {
            // Increase by 20% per day only
            double maxIncrease = currentSize * 1.2;
            return std::min(recommendedSize, maxIncrease);
        }
    }

    // If regime is established (> 5 days), follow recommendation
    return recommendedSize;
}

double PositionSizer::stressTestPosition(
    double positionSize,
    const RiskDecomposition& riskDecomp)
{
    // Stress scenario: multiple factors move adversely simultaneously
    // Growth: -2σ, Inflation: +1σ, Volatility: +2σ

    double totalLoss = 0.0;

    for (int k = 0; k < riskDecomp.numFactors; k++) {
        double gamma = riskDecomp.factorSensitivities[k];
        double stressMagnitude = 0.0;

        // Determine stress based on factor label
        const std::string& label = riskDecomp.factorLabels[k];
        if (label == "Growth") {
            stressMagnitude = -2.0;  // -2σ growth shock
        } else if (label == "Inflation") {
            stressMagnitude = 1.0;   // +1σ inflation
        } else if (label == "Volatility") {
            stressMagnitude = 2.0;   // +2σ volatility
        } else if (label == "Policy") {
            stressMagnitude = 1.0;   // +1σ policy tightening
        } else {
            stressMagnitude = 0.5;   // Other factors: mild stress
        }

        // Impact: shock × sensitivity
        totalLoss += gamma * stressMagnitude;
    }

    // Convert to dollar loss
    return positionSize * totalLoss;
}

double PositionSizer::computeDailyVaR(
    double position,
    const RiskDecomposition& riskDecomp,
    double confidenceLevel)
{
    // VaR = Position × sigma × Z-score
    // 95% confidence → Z ≈ 1.65
    // 99% confidence → Z ≈ 2.33

    double zScore = (confidenceLevel > 0.95) ? 2.33 : 1.65;
    double dailyRisk = position * riskDecomp.totalRisk;
    double var = dailyRisk * zScore;

    return var;
}

double PositionSizer::computeKellyFraction(
    double winRate,
    double avgWin,
    double avgLoss)
{
    if (winRate <= 0.0 || winRate >= 1.0) {
        return 0.0;  // Invalid win rate
    }
    if (avgWin <= 0.0 || avgLoss <= 0.0) {
        return 0.0;  // Need positive win/loss magnitudes
    }

    // Kelly formula: f* = (p * R - q) / R
    // where p = win prob, q = 1-p, R = avgWin/avgLoss
    double p = winRate;
    double q = 1.0 - winRate;
    double R = avgWin / avgLoss;

    double fullKelly = (p * R - q) / R;

    // If negative edge, don't trade
    if (fullKelly <= 0.0) {
        return 0.0;
    }

    // Half-Kelly for practical use (reduces variance significantly)
    double halfKelly = fullKelly / 2.0;

    // Cap at 25% of capital
    return std::min(halfKelly, 0.25);
}

void PositionSizer::estimateWinRate(
    const std::vector<double>& tradeReturns,
    double& outWinRate,
    double& outAvgWin,
    double& outAvgLoss)
{
    if (tradeReturns.empty()) {
        outWinRate = 0.0;
        outAvgWin = 0.0;
        outAvgLoss = 0.0;
        return;
    }

    int wins = 0;
    double totalWin = 0.0;
    double totalLoss = 0.0;
    int losses = 0;

    for (double ret : tradeReturns) {
        if (ret > 0.0) {
            wins++;
            totalWin += ret;
        } else if (ret < 0.0) {
            losses++;
            totalLoss += std::abs(ret);
        }
    }

    outWinRate = (wins + losses > 0) ? static_cast<double>(wins) / (wins + losses) : 0.0;
    outAvgWin = (wins > 0) ? totalWin / wins : 0.0;
    outAvgLoss = (losses > 0) ? totalLoss / losses : 0.0;
}

PositionSizing PositionSizer::computeKellyPositionSize(
    double baseNotional,
    const RiskDecomposition& riskDecomp,
    const MacroRegime& regime,
    const PositionConstraint& constraints,
    const std::vector<double>& tradeHistory)
{
    // If no trade history, fall back to standard vol-scaling
    if (tradeHistory.size() < 30) {
        return computePositionSize(baseNotional, riskDecomp, regime, constraints);
    }

    // Estimate Kelly parameters from trade history
    double winRate, avgWin, avgLoss;
    estimateWinRate(tradeHistory, winRate, avgWin, avgLoss);

    double kellyFraction = computeKellyFraction(winRate, avgWin, avgLoss);

    if (kellyFraction <= 0.0) {
        // No edge detected, use minimal position
        PositionSizing sizing = computePositionSize(
            baseNotional * 0.1, riskDecomp, regime, constraints);
        sizing.rationale = "Kelly: no edge detected, minimal position";
        return sizing;
    }

    // Kelly-adjusted notional
    double kellyNotional = baseNotional * kellyFraction;

    // Still apply regime adjustment on top of Kelly
    double volMultiplier = computeVolatilityMultiplier(regime);
    kellyNotional /= volMultiplier;

    // Use standard position sizing with Kelly-adjusted base
    PositionSizing sizing = computePositionSize(
        kellyNotional, riskDecomp, regime, constraints);

    std::ostringstream rationale;
    rationale << "Kelly: f*=" << (kellyFraction * 2.0)
              << ", half-Kelly=" << kellyFraction
              << ", winRate=" << winRate
              << ", avgWin/avgLoss=" << (avgLoss > 0 ? avgWin / avgLoss : 0.0)
              << ", regimeAdj=" << volMultiplier;
    sizing.rationale = rationale.str();

    return sizing;
}
