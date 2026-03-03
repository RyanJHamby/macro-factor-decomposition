//
//  Backtester.cpp
//  InvertedYieldCurveTrader
//
//  Historical simulation engine implementation
//
//  Created by Ryan Hamby on 1/15/26.
//

#include "Backtester.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include <iomanip>

BacktestResult Backtester::run(
    const BacktestConfig& config,
    const std::vector<HistoricalDataPoint>& historicalData)
{
    BacktestResult result;
    result.numTradingDays = static_cast<int>(historicalData.size());

    if (historicalData.empty()) {
        return result;
    }

    double portfolioValue = config.initialCapital;
    double peakValue = portfolioValue;
    double maxDrawdown = 0.0;
    double maxDrawdownDollar = 0.0;
    double cumulativePnl = 0.0;
    int numTrades = 0;
    int varBreaches = 0;

    SignalDirection previousSignal = SignalDirection::FLAT;
    std::vector<double> dailyReturns;
    std::vector<double> portfolioValues;
    std::vector<double> tradeReturns;
    double currentTradeReturn = 0.0;
    bool inTrade = false;

    // Create constraints
    PositionConstraint constraints;
    constraints.maxNotional = config.baseNotional * 2.0;
    constraints.maxLeverageMultiple = 2.0;
    constraints.maxDailyLoss = config.initialCapital * 0.02;
    constraints.maxDrawdownFromPeak = config.initialCapital * 0.15;

    // Create mock risk decomposition for position sizing
    RiskDecomposition riskDecomp;
    riskDecomp.numFactors = 3;
    riskDecomp.factorLabels = {"Growth", "Inflation", "Volatility"};
    riskDecomp.factorSensitivities = {0.6, -0.2, -0.8};
    riskDecomp.factorRiskContributions = {0.25, 0.08, 0.15};
    riskDecomp.componentContributions = {0.52, 0.17, 0.31};
    riskDecomp.totalRisk = 0.012;  // ~1.2% daily vol
    riskDecomp.totalVariance = 0.000144;

    portfolioValues.push_back(portfolioValue);

    for (size_t i = 0; i < historicalData.size(); i++) {
        const auto& data = historicalData[i];

        // Generate signal
        TradeSignal signal = SignalGenerator::generateSignalFromObservables(
            data.regime.vixLevel,
            data.yieldCurveSlope,
            data.growthFactor,
            data.volatilityFactor,
            data.regime
        );

        // Size position using Kelly if we have enough trade history
        PositionSizing sizing;
        if (tradeReturns.size() >= 30) {
            sizing = PositionSizer::computeKellyPositionSize(
                config.baseNotional, riskDecomp, data.regime,
                constraints, tradeReturns);
        } else {
            sizing = PositionSizer::computePositionSize(
                config.baseNotional, riskDecomp, data.regime, constraints);
        }

        // Compute position based on signal direction
        double position = 0.0;
        if (signal.direction == SignalDirection::LONG) {
            position = sizing.recommendedNotional * signal.conviction;
        } else if (signal.direction == SignalDirection::SHORT) {
            position = -sizing.recommendedNotional * signal.conviction;
        }

        // Track trade changes
        if (signal.direction != previousSignal) {
            numTrades++;

            // Close previous trade
            if (inTrade && std::abs(currentTradeReturn) > 0.0) {
                tradeReturns.push_back(currentTradeReturn);
            }
            currentTradeReturn = 0.0;
            inTrade = (signal.direction != SignalDirection::FLAT);

            // Apply commission and slippage
            double cost = config.commissionPerTrade
                        + std::abs(position) * config.slippageBps * 0.0001;
            portfolioValue -= cost;
        }

        previousSignal = signal.direction;

        // Compute daily P&L
        double dailyPnl = position * data.esReturn;
        currentTradeReturn += (std::abs(position) > 0)
                            ? dailyPnl / std::abs(position) : 0.0;

        portfolioValue += dailyPnl;
        cumulativePnl += dailyPnl;

        // Track daily return as fraction of portfolio
        double dailyReturnPct = dailyPnl / portfolioValues.back();
        dailyReturns.push_back(dailyReturnPct);

        // Track peak and drawdown
        if (portfolioValue > peakValue) {
            peakValue = portfolioValue;
        }
        double currentDrawdown = (peakValue - portfolioValue) / peakValue;
        double currentDrawdownDollar = peakValue - portfolioValue;

        if (currentDrawdown > maxDrawdown) {
            maxDrawdown = currentDrawdown;
        }
        if (currentDrawdownDollar > maxDrawdownDollar) {
            maxDrawdownDollar = currentDrawdownDollar;
        }

        // Compute VaR for tracking
        double predictedVaR = PositionSizer::computeDailyVaR(
            std::abs(position), riskDecomp, 0.95);

        // Check VaR breach
        if (dailyPnl < 0 && std::abs(dailyPnl) > predictedVaR && predictedVaR > 0) {
            varBreaches++;
        }

        // Record daily data
        DailyPnL daily;
        daily.date = data.date;
        daily.signal = signal.direction;
        daily.position = position;
        daily.dailyReturn = data.esReturn;
        daily.pnl = dailyPnl;
        daily.cumulativePnl = cumulativePnl;
        daily.portfolioValue = portfolioValue;
        daily.drawdown = currentDrawdown;
        daily.predictedVaR = predictedVaR;
        result.dailyPnls.push_back(daily);

        portfolioValues.push_back(portfolioValue);
    }

    // Close final trade
    if (inTrade && std::abs(currentTradeReturn) > 0.0) {
        tradeReturns.push_back(currentTradeReturn);
    }

    // Compute performance metrics
    result.totalReturn = (portfolioValue - config.initialCapital) / config.initialCapital;
    double years = static_cast<double>(historicalData.size()) / 252.0;
    result.annualizedReturn = std::pow(1.0 + result.totalReturn, 1.0 / years) - 1.0;
    result.sharpeRatio = computeSharpe(dailyReturns, config.riskFreeRate);
    result.maxDrawdown = maxDrawdown;
    result.maxDrawdownDollar = maxDrawdownDollar;
    result.calmarRatio = (maxDrawdown > 0) ? result.annualizedReturn / maxDrawdown : 0.0;

    // Compute annualized volatility
    if (dailyReturns.size() > 1) {
        double mean = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0) / dailyReturns.size();
        double sumSqDiff = 0.0;
        for (double r : dailyReturns) {
            sumSqDiff += (r - mean) * (r - mean);
        }
        result.annualizedVolatility = std::sqrt(sumSqDiff / (dailyReturns.size() - 1)) * std::sqrt(252.0);
    }

    // Trade statistics
    result.numTrades = numTrades;
    result.tradeReturns = tradeReturns;

    double winRate, avgWin, avgLoss;
    PositionSizer::estimateWinRate(tradeReturns, winRate, avgWin, avgLoss);
    result.winRate = winRate;
    result.avgWin = avgWin;
    result.avgLoss = avgLoss;

    // Profit factor
    double grossProfit = 0.0, grossLoss = 0.0;
    for (double r : tradeReturns) {
        if (r > 0) grossProfit += r;
        else grossLoss += std::abs(r);
    }
    result.profitFactor = (grossLoss > 0) ? grossProfit / grossLoss : 0.0;

    // Kelly estimation
    result.kellyFraction = PositionSizer::computeKellyFraction(winRate, avgWin, avgLoss);
    result.halfKellyFraction = result.kellyFraction;  // Already half-Kelly

    // VaR breach rate
    result.varBreaches = varBreaches;
    result.varBreachRate = (historicalData.size() > 0)
                         ? static_cast<double>(varBreaches) / historicalData.size()
                         : 0.0;

    return result;
}

std::vector<HistoricalDataPoint> Backtester::generateSyntheticHistory(
    int numYears,
    int tradingDaysPerYear)
{
    std::vector<HistoricalDataPoint> history;
    int totalDays = numYears * tradingDaysPerYear;

    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::normal_distribution<double> noise(0.0, 1.0);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    // Define market phases (approximate historical periods 2007-2024)
    // Each phase: {start_fraction, end_fraction, phase_name}
    struct Phase {
        double start;
        double end;
        std::string name;
    };

    std::vector<Phase> phases = {
        {0.000, 0.060, "pre_crisis"},      // 2007: pre-GFC
        {0.060, 0.175, "gfc"},             // 2008-2009: Global Financial Crisis
        {0.175, 0.235, "recovery"},        // 2010-2011: early recovery
        {0.235, 0.470, "bull_run"},        // 2012-2015: QE bull market
        {0.470, 0.530, "volatility"},      // 2016: election year volatility
        {0.530, 0.650, "low_vol"},         // 2017-2018: low vol bull
        {0.650, 0.710, "trade_war"},       // 2018-2019: trade war
        {0.710, 0.770, "covid_crash"},     // 2020 Q1: COVID crash
        {0.770, 0.880, "covid_recovery"},  // 2020-2021: recovery + stimulus
        {0.880, 0.940, "rate_hike"},       // 2022: rate hiking cycle
        {0.940, 1.000, "normalization"}    // 2023-2024: normalization
    };

    int startYear = 2007;

    for (int d = 0; d < totalDays; d++) {
        double dayFraction = static_cast<double>(d) / totalDays;

        // Determine current phase
        std::string currentPhase = "normalization";
        for (const auto& phase : phases) {
            if (dayFraction >= phase.start && dayFraction < phase.end) {
                currentPhase = phase.name;
                break;
            }
        }

        // Generate regime for this phase
        double noiseVal = noise(rng) * 0.3;
        MacroRegime regime = generateRegimeForPhase(currentPhase, noiseVal);

        // Generate factor values
        double growthFactor = 0.0;
        double volFactor = 0.0;
        double curveSlope = regime.yieldCurveSlope;

        if (currentPhase == "gfc" || currentPhase == "covid_crash") {
            growthFactor = -1.5 + noise(rng) * 0.5;
            volFactor = 2.0 + noise(rng) * 0.5;
        } else if (currentPhase == "bull_run" || currentPhase == "low_vol" || currentPhase == "covid_recovery") {
            growthFactor = 0.8 + noise(rng) * 0.3;
            volFactor = -0.3 + noise(rng) * 0.2;
        } else if (currentPhase == "rate_hike") {
            growthFactor = -0.3 + noise(rng) * 0.4;
            volFactor = 0.5 + noise(rng) * 0.3;
        } else {
            growthFactor = 0.2 + noise(rng) * 0.4;
            volFactor = 0.1 + noise(rng) * 0.3;
        }

        // Compute synthetic ES return
        double esReturn = computeSyntheticReturn(growthFactor, volFactor, curveSlope, currentPhase);
        esReturn += noise(rng) * 0.005;  // Idiosyncratic noise

        // Compute date string
        int dayOfYear = d % tradingDaysPerYear;
        int year = startYear + d / tradingDaysPerYear;
        int month = 1 + (dayOfYear * 12) / tradingDaysPerYear;
        int day = 1 + (dayOfYear % 21);
        char dateStr[16];
        std::snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", year, month, day);

        HistoricalDataPoint point;
        point.date = dateStr;
        point.regime = regime;
        point.esReturn = esReturn;
        point.growthFactor = growthFactor;
        point.volatilityFactor = volFactor;
        point.yieldCurveSlope = curveSlope;

        history.push_back(point);
    }

    return history;
}

double Backtester::computeSharpe(
    const std::vector<double>& dailyReturns,
    double riskFreeRate)
{
    if (dailyReturns.size() < 2) return 0.0;

    double n = static_cast<double>(dailyReturns.size());
    double dailyRf = riskFreeRate / 252.0;

    double sum = 0.0;
    for (double r : dailyReturns) {
        sum += (r - dailyRf);
    }
    double meanExcess = sum / n;

    double sumSqDiff = 0.0;
    for (double r : dailyReturns) {
        double diff = (r - dailyRf) - meanExcess;
        sumSqDiff += diff * diff;
    }
    double stdDev = std::sqrt(sumSqDiff / (n - 1));

    if (stdDev < 1e-12) return 0.0;

    return (meanExcess / stdDev) * std::sqrt(252.0);
}

double Backtester::computeMaxDrawdown(
    const std::vector<double>& portfolioValues)
{
    if (portfolioValues.size() < 2) return 0.0;

    double peak = portfolioValues[0];
    double maxDD = 0.0;

    for (double val : portfolioValues) {
        if (val > peak) peak = val;
        double dd = (peak - val) / peak;
        if (dd > maxDD) maxDD = dd;
    }

    return maxDD;
}

MacroRegime Backtester::generateRegimeForPhase(
    const std::string& phase,
    double noise)
{
    MacroRegime regime;

    if (phase == "gfc") {
        regime.vixLevel = 45.0 + noise * 15.0;
        regime.moveIndex = 140.0 + noise * 20.0;
        regime.creditSpread = 300.0 + noise * 50.0;
        regime.yieldCurveSlope = -30.0 + noise * 40.0;
        regime.putCallRatio = 1.3 + noise * 0.2;
        regime.riskLabel = "Risk-Off";
        regime.inflationLabel = "Inflation-Sensitive";
        regime.fragileLabel = "Fragile";
        regime.confidence = 0.9;
        regime.volatilityMultiplier = 2.5;
    } else if (phase == "covid_crash") {
        regime.vixLevel = 55.0 + noise * 20.0;
        regime.moveIndex = 160.0 + noise * 15.0;
        regime.creditSpread = 350.0 + noise * 40.0;
        regime.yieldCurveSlope = 40.0 + noise * 20.0;
        regime.putCallRatio = 1.4 + noise * 0.2;
        regime.riskLabel = "Risk-Off";
        regime.inflationLabel = "Growth-Sensitive";
        regime.fragileLabel = "Fragile";
        regime.confidence = 0.95;
        regime.volatilityMultiplier = 2.5;
    } else if (phase == "bull_run" || phase == "low_vol") {
        regime.vixLevel = 13.0 + noise * 3.0;
        regime.moveIndex = 85.0 + noise * 5.0;
        regime.creditSpread = 90.0 + noise * 15.0;
        regime.yieldCurveSlope = 120.0 + noise * 30.0;
        regime.putCallRatio = 0.85 + noise * 0.05;
        regime.riskLabel = "Risk-On";
        regime.inflationLabel = "Growth-Sensitive";
        regime.fragileLabel = "Stable";
        regime.confidence = 0.85;
        regime.volatilityMultiplier = 1.0;
    } else if (phase == "rate_hike") {
        regime.vixLevel = 25.0 + noise * 8.0;
        regime.moveIndex = 120.0 + noise * 15.0;
        regime.creditSpread = 180.0 + noise * 30.0;
        regime.yieldCurveSlope = -40.0 + noise * 20.0;
        regime.putCallRatio = 1.1 + noise * 0.1;
        regime.riskLabel = "Risk-Off";
        regime.inflationLabel = "Inflation-Sensitive";
        regime.fragileLabel = "Fragile";
        regime.confidence = 0.75;
        regime.volatilityMultiplier = 1.8;
    } else if (phase == "covid_recovery") {
        regime.vixLevel = 18.0 + noise * 5.0;
        regime.moveIndex = 95.0 + noise * 10.0;
        regime.creditSpread = 110.0 + noise * 20.0;
        regime.yieldCurveSlope = 130.0 + noise * 30.0;
        regime.putCallRatio = 0.90 + noise * 0.05;
        regime.riskLabel = "Risk-On";
        regime.inflationLabel = "Growth-Sensitive";
        regime.fragileLabel = "Stable";
        regime.confidence = 0.80;
        regime.volatilityMultiplier = 1.1;
    } else if (phase == "trade_war") {
        regime.vixLevel = 20.0 + noise * 6.0;
        regime.moveIndex = 100.0 + noise * 10.0;
        regime.creditSpread = 140.0 + noise * 25.0;
        regime.yieldCurveSlope = 20.0 + noise * 30.0;
        regime.putCallRatio = 1.0 + noise * 0.1;
        regime.riskLabel = "Neutral";
        regime.inflationLabel = "Balanced";
        regime.fragileLabel = "Stable";
        regime.confidence = 0.65;
        regime.volatilityMultiplier = 1.3;
    } else {
        // normalization / pre_crisis / recovery / volatility
        regime.vixLevel = 16.0 + noise * 4.0;
        regime.moveIndex = 95.0 + noise * 8.0;
        regime.creditSpread = 120.0 + noise * 20.0;
        regime.yieldCurveSlope = 80.0 + noise * 40.0;
        regime.putCallRatio = 0.92 + noise * 0.06;
        regime.riskLabel = "Neutral";
        regime.inflationLabel = "Balanced";
        regime.fragileLabel = "Stable";
        regime.confidence = 0.70;
        regime.volatilityMultiplier = 1.15;
    }

    return regime;
}

double Backtester::computeSyntheticReturn(
    double growthFactor,
    double volFactor,
    double curveSlope,
    const std::string& phase)
{
    // r_t = gamma^T f_t
    // ES is: positive to growth, negative to volatility
    double esGrowthSensitivity = 0.005;    // 50bps per unit growth shock
    double esVolSensitivity = -0.003;      // -30bps per unit vol shock
    double esCurveSensitivity = 0.00002;   // Small positive sensitivity to curve

    double baseReturn = esGrowthSensitivity * growthFactor
                      + esVolSensitivity * volFactor
                      + esCurveSensitivity * curveSlope;

    // Add phase-specific drift (long-term equity risk premium)
    if (phase == "bull_run" || phase == "low_vol" || phase == "covid_recovery") {
        baseReturn += 0.0004;  // ~10% annualized
    } else if (phase == "gfc" || phase == "covid_crash") {
        baseReturn -= 0.001;   // Crisis drag
    } else if (phase == "rate_hike") {
        baseReturn -= 0.0002;  // Bear market drag
    } else {
        baseReturn += 0.0002;  // Normal risk premium
    }

    return baseReturn;
}

void Backtester::printSummary(const BacktestResult& result)
{
    std::cout << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "BACKTEST RESULTS" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << std::endl;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Performance Metrics:" << std::endl;
    std::cout << "  Total Return:         " << (result.totalReturn * 100.0) << "%" << std::endl;
    std::cout << "  Annualized Return:    " << (result.annualizedReturn * 100.0) << "%" << std::endl;
    std::cout << "  Sharpe Ratio:         " << result.sharpeRatio << std::endl;
    std::cout << "  Annualized Vol:       " << (result.annualizedVolatility * 100.0) << "%" << std::endl;
    std::cout << "  Calmar Ratio:         " << result.calmarRatio << std::endl;
    std::cout << std::endl;

    std::cout << "Risk Metrics:" << std::endl;
    std::cout << "  Max Drawdown:         " << (result.maxDrawdown * 100.0) << "%" << std::endl;
    std::cout << "  Max Drawdown ($):     $" << result.maxDrawdownDollar << std::endl;
    std::cout << "  VaR Breach Rate:      " << (result.varBreachRate * 100.0) << "% (target: 5%)" << std::endl;
    std::cout << "  VaR Breaches:         " << result.varBreaches << std::endl;
    std::cout << std::endl;

    std::cout << "Trade Statistics:" << std::endl;
    std::cout << "  Trading Days:         " << result.numTradingDays << std::endl;
    std::cout << "  Number of Trades:     " << result.numTrades << std::endl;
    std::cout << "  Win Rate:             " << (result.winRate * 100.0) << "%" << std::endl;
    std::cout << "  Avg Win:              " << (result.avgWin * 100.0) << "%" << std::endl;
    std::cout << "  Avg Loss:             " << (result.avgLoss * 100.0) << "%" << std::endl;
    std::cout << "  Profit Factor:        " << result.profitFactor << std::endl;
    std::cout << std::endl;

    std::cout << "Kelly Criterion:" << std::endl;
    std::cout << "  Full Kelly:           " << (result.kellyFraction * 2.0 * 100.0) << "%" << std::endl;
    std::cout << "  Half Kelly (used):    " << (result.halfKellyFraction * 100.0) << "%" << std::endl;
    std::cout << std::endl;

    std::cout << "=========================================" << std::endl;
}
