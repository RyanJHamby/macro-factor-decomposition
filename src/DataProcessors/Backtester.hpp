//
//  Backtester.hpp
//  InvertedYieldCurveTrader
//
//  Vector-based backtesting engine that replays historical data through
//  the signal → position → P&L pipeline with Sharpe/drawdown analytics.
//
//  Created by Ryan Hamby on 1/15/26.
//

#ifndef BACKTESTER_HPP
#define BACKTESTER_HPP

#include "SignalGenerator.hpp"
#include "PositionSizer.hpp"
#include "PortfolioRiskAnalyzer.hpp"
#include "MacroFactorModel.hpp"
#include "../DataProviders/FREDDataClient.hpp"
#include <vector>
#include <string>

/**
 * BacktestConfig: Parameters for historical simulation
 */
struct BacktestConfig {
    std::string startDate;
    std::string endDate;
    double initialCapital;        // Starting capital ($)
    double commissionPerTrade;    // Commission per trade ($)
    double slippageBps;           // Slippage in basis points
    double riskFreeRate;          // Annual risk-free rate (default 0.04)
    double baseNotional;          // Base position size ($)

    BacktestConfig()
        : initialCapital(10000000.0)
        , commissionPerTrade(2.50)
        , slippageBps(1.0)
        , riskFreeRate(0.04)
        , baseNotional(5000000.0) {}
};

/**
 * DailyPnL: Single-day P&L record
 */
struct DailyPnL {
    std::string date;
    SignalDirection signal;
    double position;              // Notional exposure
    double dailyReturn;           // Asset return for the day
    double pnl;                   // Daily P&L ($)
    double cumulativePnl;         // Running total P&L
    double portfolioValue;        // NAV
    double drawdown;              // Current drawdown from peak
    double predictedVaR;          // VaR at start of day
};

/**
 * BacktestResult: Complete backtest output with performance metrics
 */
struct BacktestResult {
    // Daily records
    std::vector<DailyPnL> dailyPnls;

    // Return metrics
    double totalReturn;           // Total % return
    double annualizedReturn;      // Annualized % return
    double sharpeRatio;           // Annualized Sharpe ratio

    // Risk metrics
    double maxDrawdown;           // Maximum peak-to-trough (%)
    double maxDrawdownDollar;     // Maximum peak-to-trough ($)
    double calmarRatio;           // Annualized return / max drawdown
    double annualizedVolatility;  // Annualized std dev of returns

    // Trade statistics
    double winRate;
    double avgWin;
    double avgLoss;
    int numTrades;                // Number of position changes
    int numTradingDays;
    double profitFactor;          // Gross profit / gross loss

    // VaR statistics
    double varBreachRate;         // % of days VaR was breached
    int varBreaches;

    // Kelly parameters (estimated from backtest)
    double kellyFraction;
    double halfKellyFraction;

    // Trade returns for Kelly estimation
    std::vector<double> tradeReturns;
};

/**
 * HistoricalDataPoint: Single observation for backtesting
 *
 * Contains regime classification and factor data for one time period.
 */
struct HistoricalDataPoint {
    std::string date;
    MacroRegime regime;
    double esReturn;              // ES futures daily return
    double growthFactor;
    double volatilityFactor;
    double yieldCurveSlope;
};

/**
 * Backtester: Historical simulation engine
 *
 * Replays historical data through:
 *   classify regime → generate signal → size position (Kelly) → compute P&L
 *
 * Tracks Sharpe ratio, max drawdown, win rate, and VaR breach rate.
 */
class Backtester {
public:
    /**
     * Run backtest over historical data
     *
     * @param config: Backtest parameters
     * @param historicalData: Vector of daily observations
     * @return BacktestResult with full analytics
     */
    static BacktestResult run(
        const BacktestConfig& config,
        const std::vector<HistoricalDataPoint>& historicalData
    );

    /**
     * Generate synthetic historical dataset from factor model
     *
     * Creates realistic ES returns using:
     *   r_t = gamma^T f_t + noise
     *
     * Covers 2007-2024 with regime transitions.
     *
     * @param numYears: Number of years to simulate (default 17)
     * @param tradingDaysPerYear: Trading days per year (default 252)
     * @return Vector of historical data points
     */
    static std::vector<HistoricalDataPoint> generateSyntheticHistory(
        int numYears = 17,
        int tradingDaysPerYear = 252
    );

    /**
     * Fetch real historical data from FRED API
     *
     * Fetches SP500 (returns proxy), DGS10/DGS2 (yield curve), VIXCLS (VIX),
     * UNRATE (unemployment), UMCSENT (consumer sentiment) from 2007-2024.
     *
     * Constructs HistoricalDataPoint vector with real regime classifications
     * derived from actual market data.
     *
     * @param fredApiKey: FRED API key
     * @param startDate: Start date (YYYY-MM-DD), default "2007-01-01"
     * @param endDate: End date (YYYY-MM-DD), default "2024-12-31"
     * @return Vector of historical data points from real FRED data
     */
    static std::vector<HistoricalDataPoint> fetchHistoricalData(
        const std::string& fredApiKey,
        const std::string& startDate = "2007-01-01",
        const std::string& endDate = "2024-12-31"
    );

    /**
     * Compute Sharpe ratio from daily returns
     *
     * Sharpe = (mean_return - rf/252) / std_return * sqrt(252)
     *
     * @param dailyReturns: Vector of daily returns
     * @param riskFreeRate: Annual risk-free rate
     * @return Annualized Sharpe ratio
     */
    static double computeSharpe(
        const std::vector<double>& dailyReturns,
        double riskFreeRate = 0.04
    );

    /**
     * Compute maximum drawdown from portfolio value series
     *
     * @param portfolioValues: Daily NAV series
     * @return Maximum peak-to-trough drawdown (as positive percentage)
     */
    static double computeMaxDrawdown(
        const std::vector<double>& portfolioValues
    );

    /**
     * Print backtest summary to console
     */
    static void printSummary(const BacktestResult& result);

private:
    /**
     * Generate a regime for a given market phase
     */
    static MacroRegime generateRegimeForPhase(
        const std::string& phase,
        double noise
    );

    /**
     * Compute daily ES return from factors + noise
     */
    static double computeSyntheticReturn(
        double growthFactor,
        double volFactor,
        double curveSlope,
        const std::string& phase
    );
};

#endif // BACKTESTER_HPP
