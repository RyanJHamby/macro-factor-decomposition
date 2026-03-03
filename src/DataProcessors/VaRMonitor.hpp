//
//  VaRMonitor.hpp
//  InvertedYieldCurveTrader
//
//  Automated VaR monitoring with expected shortfall and breach tracking.
//  Runs continuously and alerts when realized losses exceed predicted VaR.
//
//  Created by Ryan Hamby on 1/15/26.
//

#ifndef VAR_MONITOR_HPP
#define VAR_MONITOR_HPP

#include "PositionSizer.hpp"
#include "PortfolioRiskAnalyzer.hpp"
#include <vector>
#include <string>
#include <deque>

/**
 * VaRRecord: Single VaR observation for breach tracking
 */
struct VaRRecord {
    std::string date;
    double predictedVaR;          // VaR at start of day
    double actualPnL;             // Realized P&L
    bool breached;                // |actualPnL| > predictedVaR
    double position;              // Position at start of day
};

/**
 * VaRReport: Summary of VaR monitoring performance
 */
struct VaRReport {
    int totalObservations;
    int breaches;
    double breachRate;            // Should be ~5% for 95% VaR
    double expectedShortfall;     // Average loss when VaR is breached (CVaR)
    double worstBreach;           // Largest single-day loss exceeding VaR
    double avgPredictedVaR;
    double avgRealizedLoss;       // Average loss on breach days

    bool isCalibrated;            // True if breach rate is within [2%, 8%]
    std::string calibrationMessage;
};

/**
 * VaRMonitor: Continuous VaR monitoring and breach detection
 *
 * Tracks predicted VaR vs realized P&L to validate risk model accuracy.
 * A well-calibrated 95% VaR should be breached ~5% of days.
 *
 * Too few breaches (< 2%) → VaR is too conservative (leaving money on table)
 * Too many breaches (> 8%) → VaR is too aggressive (underestimating risk)
 */
class VaRMonitor {
public:
    VaRMonitor();

    /**
     * Record a new observation (call daily)
     *
     * @param date: Observation date
     * @param position: Position notional at start of day
     * @param actualPnL: Realized P&L for the day
     * @param riskDecomp: Risk decomposition for VaR calculation
     * @param confidenceLevel: VaR confidence level (default 0.95)
     */
    void updatePosition(
        const std::string& date,
        double position,
        double actualPnL,
        const RiskDecomposition& riskDecomp,
        double confidenceLevel = 0.95
    );

    /**
     * Check if a single day's P&L breached VaR
     *
     * @param actualPnL: Realized daily P&L
     * @param predictedVaR: VaR prediction at start of day
     * @return True if loss exceeded VaR
     */
    static bool checkVaRBreach(double actualPnL, double predictedVaR);

    /**
     * Compute breach rate over observation history
     *
     * @return Fraction of days where VaR was breached
     */
    double getVaRBreachRate() const;

    /**
     * Compute expected shortfall (CVaR / ES)
     *
     * Average loss on days when VaR is breached.
     * More informative than VaR for tail risk.
     *
     * @param position: Current position notional
     * @param riskDecomp: Risk decomposition
     * @param confidenceLevel: VaR confidence (default 0.95)
     * @return Expected shortfall in dollars
     */
    static double computeExpectedShortfall(
        double position,
        const RiskDecomposition& riskDecomp,
        double confidenceLevel = 0.95
    );

    /**
     * Compute expected shortfall from historical observations
     *
     * @return Average loss on breach days (from recorded history)
     */
    double computeHistoricalES() const;

    /**
     * Generate full VaR monitoring report
     *
     * @return VaRReport with breach statistics and calibration check
     */
    VaRReport generateReport() const;

    /**
     * Get observation history
     */
    const std::deque<VaRRecord>& getHistory() const { return history_; }

    /**
     * Clear history
     */
    void reset();

private:
    std::deque<VaRRecord> history_;
    static constexpr int MAX_HISTORY = 2520;  // ~10 years of trading days
    static constexpr double BREACH_RATE_LOW = 0.02;
    static constexpr double BREACH_RATE_HIGH = 0.08;
};

#endif // VAR_MONITOR_HPP
