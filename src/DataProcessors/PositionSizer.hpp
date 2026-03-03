//
//  PositionSizer.hpp
//  InvertedYieldCurveTrader
//
//  Risk-aware position sizing for ES futures based on macro regime.
//  NOT a prediction system. This is risk management.
//
//  Created by Ryan Hamby on 12/25/25.
//

#ifndef POSITION_SIZER_HPP
#define POSITION_SIZER_HPP

#include "PortfolioRiskAnalyzer.hpp"
#include <vector>
#include <string>
#include <map>

/**
 * MacroRegime: Market state affecting factor volatilities
 *
 * Combines VIX, MOVE, spreads, curve to classify market conditions.
 * Used to condition factor variances: Vol_k(State_t).
 */
struct MacroRegime {
    // Market state variables (daily)
    double vixLevel;           // 10-100, current fear gauge
    double moveIndex;          // Bond volatility
    double creditSpread;       // BAA-AAA, credit risk appetite
    double yieldCurveSlope;    // 2s10s, growth expectations
    double putCallRatio;       // Option positioning (smoothed)

    // Derived regime classification
    std::string riskLabel;     // "Risk-On", "Risk-Off"
    std::string inflationLabel; // "Inflation-Sensitive", "Growth-Sensitive"
    std::string fragileLabel;  // "Stable", "Fragile"

    // Overall regime confidence (0-1)
    double confidence;

    // Volatility multiplier: how much do factors amplify in this regime?
    // High-VIX regime: growth shocks 2-3x worse
    // Low-VIX regime: baseline
    double volatilityMultiplier;  // 1.0 = baseline, 2.0 = double volatility
};

/**
 * PositionConstraint: Risk limits for a position
 */
struct PositionConstraint {
    double maxNotional;                    // Max dollar exposure (e.g., $10M)
    double maxLeverageMultiple;            // Max leverage (e.g., 2x = 200%)
    std::map<std::string, double> maxFactorExposure;  // Max γ_k per factor
    double maxDailyLoss;                   // Max loss tolerance per day
    double maxDrawdownFromPeak;            // Max cumulative loss
};

/**
 * PositionSizing: Recommended position size and rationale
 */
struct PositionSizing {
    // Position size
    double recommendedNotional;            // $ to invest in ES
    double recommendedLeverage;            // Leverage multiple (1.0 = no leverage)
    double recommendedShares;              // Number of ES contracts

    // Risk metrics at this size
    double expectedDailyVol;               // Expected volatility at this size
    double expectedWorstCaseDaily;         // 2σ down move
    double maxMonthlyDrawdown;             // Stress-tested max monthly loss

    // Regime adjustments
    std::string rationale;                 // Why this size? (human-readable)
    double regimeAdjustmentFactor;         // Scale factor applied
    MacroRegime currentRegime;

    // Position constraints
    bool isWithinConstraints;
    std::vector<std::string> constraintBreaches;  // What limits we're hitting

    // Rebalancing signals
    bool shouldReduceSize;                 // True if risk too high
    bool shouldAddHedge;                   // True if need protection
    double hedgeRatio;                     // % of position to hedge (0-1)
};

/**
 * HedgingStrategy: What derivative contracts to buy for downside protection
 */
struct HedgingStrategy {
    bool needsHedge;
    std::string hedgeInstrument;  // "ES_Put_Vertical", "VIX_Call", etc.
    double hedgeSize;             // Notional to hedge
    double hedgeRatio;            // % of position size (e.g., 0.2 = 20% collar)
    std::string rationale;        // Why this hedge?

    // Cost of hedging
    double estimatedCost;         // Annual drag on returns from hedge
    double costPercent;           // As % of position

    // What risks does it address?
    std::vector<std::string> hedgedRisks;  // ["Growth shock", "Volatility spike"]
};

/**
 * PositionSizer: Dynamic position sizing based on macro regime
 *
 * Core principle: Size ES exposure inversely to factor volatility
 *
 * ES Position = Base Size × (1 / Factor Volatility Multiplier)
 *
 * High growth volatility → reduce size
 * High inflation volatility → reduce size
 * High volatility regime (VIX > 30) → reduce size
 */
class PositionSizer {
public:
    /**
     * Compute position size given current macro regime
     *
     * @param baseNotional: Max desired position (e.g., $10M)
     * @param riskDecomp: Risk decomposition (γ_k and Var_k)
     * @param regime: Current market regime
     * @param constraints: Hard limits on position
     * @return Recommended position with rationale
     */
    static PositionSizing computePositionSize(
        double baseNotional,
        const RiskDecomposition& riskDecomp,
        const MacroRegime& regime,
        const PositionConstraint& constraints
    );

    /**
     * Classify market regime from observable data
     *
     * Inputs: VIX, MOVE, spreads, curve, put/call
     * Output: Risk-On/Off, Inflation/Growth-Sensitive, Stable/Fragile
     *         + volatility multiplier for factor variances
     *
     * @param vixLevel: Current VIX (10-100)
     * @param moveIndex: Bond volatility (80-150)
     * @param creditSpread: BAA-AAA spread (bps)
     * @param yieldCurveSlope: 2s10s (bps)
     * @param putCallRatio: Put/call ratio (smoothed)
     * @return MacroRegime classification
     */
    static MacroRegime classifyRegime(
        double vixLevel,
        double moveIndex,
        double creditSpread,
        double yieldCurveSlope,
        double putCallRatio
    );

    /**
     * Compute factor volatilities conditioned on regime
     *
     * Base: Σ_f from unconditional factor model
     * Adjusted: Σ_f(Regime) = Σ_f × multiplier(Regime)
     *
     * High-VIX, high-MOVE, inverted curve → factors more volatile
     * Low-VIX, flat curve → baseline volatility
     *
     * @param baseFactorVariances: σ²_k from MacroFactors
     * @param regime: Current regime
     * @return Regime-adjusted factor variances
     */
    static std::vector<double> regimeAdjustedVolatilities(
        const std::vector<double>& baseFactorVariances,
        const MacroRegime& regime
    );

    /**
     * Compute factor volatility multiplier from regime
     *
     * Multiplier = 1 + w_vix × (VIX/20 - 1) + w_move × (MOVE/100 - 1) + ...
     *
     * Explains: Why are we reducing size?
     *
     * @param regime: Current market regime
     * @return Multiplier (1.0 = baseline, 2.0 = double volatility)
     */
    static double computeVolatilityMultiplier(const MacroRegime& regime);

    /**
     * Recommend hedging strategy given position and regime
     *
     * When to hedge:
     * - High growth sensitivity + low sentiment (recession risk)
     * - Large position in high-volatility regime
     * - Yield curve inverted + credit spreads widening
     *
     * @param positionSize: Current position (notional)
     * @param riskDecomp: Factor sensitivities and contributions
     * @param regime: Current market regime
     * @return Hedging recommendation with cost-benefit
     */
    static HedgingStrategy recommendHedge(
        double positionSize,
        const RiskDecomposition& riskDecomp,
        const MacroRegime& regime
    );

    /**
     * Dynamic size adjustment with hysteresis
     *
     * Don't flip size every day. Wait for regime "stickiness".
     * Reduce size immediately if risk breaches limits.
     * Increase slowly as regime stabilizes.
     *
     * @param currentSize: Current position
     * @param recommendedSize: What we computed
     * @param previousRegime: Last regime (for hysteresis)
     * @param daysInCurrentRegime: How long we've been here
     * @return Size with hysteresis applied
     */
    static double applyHysteresis(
        double currentSize,
        double recommendedSize,
        const MacroRegime& previousRegime,
        const MacroRegime& currentRegime,
        int daysInCurrentRegime
    );

    /**
     * Stress test a position
     *
     * How much can we lose if multiple factors spike simultaneously?
     * - Growth factor: -2σ
     * - Inflation factor: +1σ
     * - Volatility factor: +2σ
     *
     * @param positionSize: Notional to stress test
     * @param riskDecomp: Factor sensitivities
     * @return Worst-case P&L from stress scenario
     */
    static double stressTestPosition(
        double positionSize,
        const RiskDecomposition& riskDecomp
    );

    /**
     * Compute daily risk-adjusted position limit
     *
     * VaR at 95% confidence: How much can we lose in 1 day?
     * DailyLimit = Position × 2σ (approximately 95% confidence)
     *
     * If we hit this limit, reduce position immediately.
     *
     * @param position: Current notional
     * @param riskDecomp: Daily volatility
     * @param confidenceLevel: Confidence for VaR (default 0.95)
     * @return Maximum acceptable daily loss
     */
    static double computeDailyVaR(
        double position,
        const RiskDecomposition& riskDecomp,
        double confidenceLevel = 0.95
    );

    /**
     * Compute Kelly criterion optimal fraction
     *
     * f* = (p * R - q) / R
     * where p = win probability, q = 1-p, R = avgWin/avgLoss
     *
     * Returns half-Kelly (f*/2) for practical use to reduce variance.
     *
     * @param winRate: Historical win probability (0-1)
     * @param avgWin: Average winning trade return
     * @param avgLoss: Average losing trade return (positive number)
     * @return Optimal fraction of capital to risk (half-Kelly)
     */
    static double computeKellyFraction(
        double winRate,
        double avgWin,
        double avgLoss
    );

    /**
     * Estimate win rate and average win/loss from backtest trade history
     *
     * @param tradeReturns: Vector of individual trade P&L values
     * @param outWinRate: Estimated win probability
     * @param outAvgWin: Average winning trade
     * @param outAvgLoss: Average losing trade (positive number)
     */
    static void estimateWinRate(
        const std::vector<double>& tradeReturns,
        double& outWinRate,
        double& outAvgWin,
        double& outAvgLoss
    );

    /**
     * Compute Kelly-adjusted position size
     *
     * Uses Kelly fraction when backtest data available,
     * falls back to vol-scaling otherwise.
     *
     * @param baseNotional: Maximum desired position
     * @param riskDecomp: Risk decomposition
     * @param regime: Current market regime
     * @param constraints: Hard limits
     * @param tradeHistory: Historical trade returns (empty = use vol-scaling)
     * @return Recommended position with Kelly adjustment
     */
    static PositionSizing computeKellyPositionSize(
        double baseNotional,
        const RiskDecomposition& riskDecomp,
        const MacroRegime& regime,
        const PositionConstraint& constraints,
        const std::vector<double>& tradeHistory
    );

private:
    /**
     * Risk adjustment weights for regime classification
     *
     * How much does each factor contribute to regime volatility multiplier?
     */
    static constexpr double VIX_WEIGHT = 0.4;
    static constexpr double MOVE_WEIGHT = 0.25;
    static constexpr double SPREAD_WEIGHT = 0.15;
    static constexpr double CURVE_WEIGHT = 0.15;
    static constexpr double PUTCALL_WEIGHT = 0.05;

    // Regime thresholds
    static constexpr double VIX_RISK_ON = 12.0;
    static constexpr double VIX_RISK_OFF = 30.0;
    static constexpr double CURVE_STEEP = 100.0;  // 2s10s in bps
    static constexpr double CURVE_FLAT = 0.0;
    static constexpr double SPREAD_TIGHT = 100.0; // BAA-AAA in bps
    static constexpr double SPREAD_WIDE = 250.0;
};

#endif // POSITION_SIZER_HPP
