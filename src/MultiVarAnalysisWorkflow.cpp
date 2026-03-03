//
//  MultiVarAnalysisWorkflow.cpp
//  InvertedYieldCurveTrader
//
//  Created by Ryan Hamby on 8/27/23.
//

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include "DataProcessors/InflationDataProcessor.hpp"
#include "DataProcessors/GDPDataProcessor.hpp"
#include "DataProcessors/InterestRateDataProcessor.hpp"
#include "DataProcessors/InvertedYieldDataProcessor.hpp"
#include "DataProcessors/CovarianceCalculator.hpp"
#include "DataProcessors/StatsCalculator.hpp"
#include "DataProcessors/InvertedYieldStatsCalculator.hpp"
#include "DataProcessors/StockDataProcessor.hpp"
#include "DataProcessors/DataAligner.hpp"
#include "DataProcessors/FedFundsProcessor.hpp"
#include "DataProcessors/UnemploymentProcessor.hpp"
#include "DataProcessors/ConsumerSentimentProcessor.hpp"
#include "DataProcessors/VIXDataProcessor.hpp"
#include "DataProcessors/SurpriseTransformer.hpp"
#include "DataProcessors/MacroFactorModel.hpp"
#include "DataProcessors/PortfolioRiskAnalyzer.hpp"
#include "DataProcessors/PositionSizer.hpp"
#include "DataProcessors/SignalGenerator.hpp"
#include "DataProcessors/Backtester.hpp"
#include "DataProcessors/VaRMonitor.hpp"
#include "DataProviders/IBGateway.hpp"
#include "Utils/Date.hpp"
#include "Utils/Logger.hpp"
#include "Utils/SecretsManager.hpp"

using json = nlohmann::json;
using namespace Aws;
using namespace Aws::Auth;

int main(int argc, char **argv) {

    Aws::SDKOptions options;
    Aws::InitAPI(options); // Should only be called once.

    int result = 0;

    if (argc > 1) {
        Aws::Client::ClientConfiguration clientConfig;
        clientConfig.region = "us-east-1";

        auto provider = Aws::MakeShared<DefaultAWSCredentialsProviderChain>("alloc-tag");
        auto creds = provider->GetAWSCredentials();
        if (creds.IsEmpty()) {
            std::cerr << "Failed authentication" << std::endl;
        }

        if (std::strcmp(argv[1], "covariance") == 0) {
            // PHASE 1: Extract macro surprises and decompose into interpretable factors
            // This is the core analytical pipeline:
            // Step 1.2: Convert economic levels -> surprises (information shocks)
            // Step 2.1: Decompose surprise covariance into macro factors (Growth, Inflation, Policy, Volatility)

            try {
                Logger::info("Analysis started", {
                    {"phase", "1"},
                    {"mode", "covariance"},
                    {"timestamp", std::time(nullptr)}
                });

                // Fetch all 8 economic indicators
                std::map<std::string, std::vector<double>> rawData;

                // Get API keys securely
                std::map<std::string, std::string> secrets;
                try {
                    secrets = SecretsManager::getAllSecrets();
                } catch (const std::exception& e) {
                    Logger::critical("Failed to retrieve API keys", e);
                    return 1;
                }

                std::string fredKeyStr = secrets["fred_api_key"];
                std::string alphaKeyStr = secrets["alpha_vantage_api_key"];

                std::cout << "=========================================" << std::endl;
                std::cout << "PHASE 1: Macro Surprise-Driven Analysis" << std::endl;
                std::cout << "=========================================" << std::endl;
                std::cout << std::endl;

                // STEP 1: Fetch all 8 economic indicators
                std::cout << "Step 1: Fetching all 8 economic indicators..." << std::endl;
                Logger::info("Fetching economic indicators");

                try {
                    InflationDataProcessor inflationProcessor;
                    FedFundsProcessor fedFundsProcessor;
                    UnemploymentProcessor unemploymentProcessor;
                    ConsumerSentimentProcessor sentimentProcessor;
                    GDPDataProcessor gdpProcessor;
                    InterestRateDataProcessor interestRateProcessor;
                    InvertedYieldDataProcessor invertedYieldProcessor;
                    VIXDataProcessor vixProcessor;

                    rawData["inflation"] = inflationProcessor.process(fredKeyStr, 10);
                    rawData["fed_funds"] = fedFundsProcessor.process(fredKeyStr, 12);
                    rawData["unemployment"] = unemploymentProcessor.process(fredKeyStr, 12);
                    rawData["consumer_sentiment"] = sentimentProcessor.process(fredKeyStr, 12);
                    rawData["gdp"] = gdpProcessor.process(fredKeyStr, 8);

                    invertedYieldProcessor.process(fredKeyStr);
                    rawData["inverted_yield"] = invertedYieldProcessor.getRecentValues();

                    rawData["vix"] = vixProcessor.process(alphaKeyStr, 30);

                    Logger::info("Data fetch successful", {
                        {"indicators_fetched", rawData.size()}
                    });
                } catch (const std::exception& e) {
                    Logger::error("Data fetch failed", e);
                    return 1;
                }

                std::cout << "Fetched " << rawData.size() << " indicators" << std::endl;
                std::cout << std::endl;

                // STEP 2: Align to monthly frequency
                std::cout << "Step 2: Aligning all indicators to monthly frequency..." << std::endl;

                auto alignedData = DataAligner::alignAllIndicators(rawData);

                std::cout << "Aligned to 12 monthly observations" << std::endl;
                std::cout << std::endl;

                // STEP 3: Extract surprises (Step 1.2: Surprise Extraction)
                std::cout << "Step 3 (1.2): Extracting macro surprises from levels..." << std::endl;
                std::cout << "  e_t = X_t - E[X_t]  (information shocks)" << std::endl;
                std::cout << std::endl;

                std::map<std::string, IndicatorSurprise> allSurprises;
                std::map<std::string, std::vector<double>> surpriseVectors;

                for (auto& [indicator, levels] : alignedData) {
                    IndicatorSurprise surprise = SurpriseTransformer::extractSurprise(levels, indicator, 6);
                    allSurprises[indicator] = surprise;
                    surpriseVectors[indicator] = surprise.surprises;

                    std::cout << "  " << indicator << ": ";
                    std::cout << "mean=" << surprise.meanSurprise << ", ";
                    std::cout << "source=" << surprise.expectationSource << ", ";
                    std::cout << "validated=" << (surprise.isValidated ? "yes" : "no") << std::endl;
                }
                std::cout << std::endl;

                // STEP 4: Calculate covariance of SURPRISES (not levels)
                std::cout << "Step 4: Computing 8x8 covariance matrix of surprises..." << std::endl;
                std::cout << "  Sigma_e = Cov(e_1, ..., e_8)" << std::endl;
                std::cout << std::endl;

                CovarianceCalculator covCalculator;
                CovarianceMatrix surpriseCovMatrix = covCalculator.calculateCovarianceMatrix(surpriseVectors);

                double frobenius = surpriseCovMatrix.getFrobeniusNorm();
                std::cout << "Covariance matrix computed. Frobenius norm (regime volatility): " << frobenius << std::endl;
                std::cout << std::endl;

                // STEP 5: Decompose surprises into macro factors (Step 2.1)
                std::cout << "Step 5 (2.1): Decomposing surprises into 3 macro factors..." << std::endl;
                std::cout << "  e_t = B f_t + u_t  (PCA decomposition)" << std::endl;
                std::cout << std::endl;

                MacroFactors factors = MacroFactorModel::decomposeSurpriseCovariance(surpriseCovMatrix, 3);

                std::cout << "Factor decomposition complete" << std::endl;
                std::cout << std::endl;

                // Display factor results
                std::cout << "=========================================" << std::endl;
                std::cout << "Macro Factor Results" << std::endl;
                std::cout << "=========================================" << std::endl;
                std::cout << std::endl;

                std::cout << "Variance Explained: " << (factors.cumulativeVarianceExplained * 100.0) << "%" << std::endl;
                std::cout << std::endl;

                for (int k = 0; k < factors.numFactors; k++) {
                    std::cout << "Factor " << (k + 1) << ": " << factors.factorLabels[k] << std::endl;
                    std::cout << "  Variance: " << factors.factorVariances[k] << std::endl;
                    std::cout << "  Confidence: " << (factors.labelConfidences[k] * 100.0) << "%" << std::endl;
                    std::cout << "  Loadings:" << std::endl;

                    for (int i = 0; i < static_cast<int>(factors.indicatorNames.size()); i++) {
                        double loading = factors.loadings(i, k);
                        if (std::abs(loading) > 0.05) {  // Only show meaningful loadings
                            std::cout << "    " << factors.indicatorNames[i] << ": " << loading << std::endl;
                        }
                    }
                    std::cout << std::endl;
                }

                // PHASE 2: Risk Attribution and Position Sizing
                std::cout << std::endl;
                std::cout << "=========================================" << std::endl;
                std::cout << "PHASE 2: Risk Attribution & Position Sizing" << std::endl;
                std::cout << "=========================================" << std::endl;
                std::cout << std::endl;

                // STEP 6: Analyze ES portfolio risk attribution (Step 3.1)
                std::cout << "Step 6 (3.1): Computing ES portfolio risk attribution..." << std::endl;
                std::cout << "  RC_k = gamma_k (Sigma_f gamma)_k  (exact risk contribution formula)" << std::endl;
                std::cout << std::endl;

                // Create ES portfolio sensitivities (typical ES exposure pattern)
                Eigen::VectorXd esPortfolioBeta(factors.indicatorNames.size());
                esPortfolioBeta(0) = 0.3;   // fed_funds: tightening bad for ES
                esPortfolioBeta(1) = -0.5;  // unemployment: falling is good
                esPortfolioBeta(2) = 0.4;   // sentiment: positive
                esPortfolioBeta(3) = 0.6;   // GDP: strong growth good
                esPortfolioBeta(4) = -0.2;  // inflation: high inflation hurts valuations
                esPortfolioBeta(5) = -0.1;  // treasury_10y: slightly negative
                esPortfolioBeta(6) = -0.8;  // VIX: strong negative
                if (factors.indicatorNames.size() > 7) {
                    esPortfolioBeta(7) = -0.7;  // MOVE: negative
                }

                RiskDecomposition portfolioRisk = PortfolioRiskAnalyzer::analyzeRisk(esPortfolioBeta, factors);

                std::cout << "Portfolio Total Risk (Daily Vol): " << portfolioRisk.totalRisk << std::endl;
                std::cout << "Portfolio Total Variance: " << portfolioRisk.totalVariance << std::endl;
                std::cout << std::endl;

                std::cout << "Risk Attribution by Macro Factor:" << std::endl;
                for (int k = 0; k < portfolioRisk.numFactors; k++) {
                    std::cout << "  " << portfolioRisk.factorLabels[k] << ":" << std::endl;
                    std::cout << "    Sensitivity (gamma_k): " << portfolioRisk.factorSensitivities[k] << std::endl;
                    std::cout << "    Risk Contribution: " << portfolioRisk.factorRiskContributions[k] << std::endl;
                    std::cout << "    % of Total Risk: " << (portfolioRisk.componentContributions[k] * 100.0) << "%" << std::endl;
                }
                std::cout << std::endl;

                // STEP 7: Classify market regime and size position (Step 4.1)
                std::cout << "Step 7 (4.1): Classifying market regime and sizing ES position..." << std::endl;
                std::cout << std::endl;

                double currentVIX = 18.0;
                double currentMOVE = 100.0;
                double creditSpread = 110.0;
                double yieldCurveSlope = 120.0;
                double putCallRatio = 0.92;

                MacroRegime currentRegime = PositionSizer::classifyRegime(
                    currentVIX, currentMOVE, creditSpread, yieldCurveSlope, putCallRatio
                );

                std::cout << "Current Market Regime:" << std::endl;
                std::cout << "  VIX Level: " << currentVIX << std::endl;
                std::cout << "  MOVE Index: " << currentMOVE << std::endl;
                std::cout << "  Credit Spread: " << creditSpread << " bps" << std::endl;
                std::cout << "  Yield Curve Slope: " << yieldCurveSlope << " bps" << std::endl;
                std::cout << "  Put/Call Ratio: " << putCallRatio << std::endl;
                std::cout << std::endl;

                std::cout << "Regime Classification:" << std::endl;
                std::cout << "  Risk Label: " << currentRegime.riskLabel << std::endl;
                std::cout << "  Inflation Sensitivity: " << currentRegime.inflationLabel << std::endl;
                std::cout << "  Stability: " << currentRegime.fragileLabel << std::endl;
                std::cout << "  Volatility Multiplier: " << currentRegime.volatilityMultiplier << std::endl;
                std::cout << "  Confidence: " << (currentRegime.confidence * 100.0) << "%" << std::endl;
                std::cout << std::endl;

                // Compute position sizing
                double baseNotional = 5000000.0;
                PositionConstraint constraints;
                constraints.maxNotional = 10000000.0;
                constraints.maxLeverageMultiple = 2.0;
                constraints.maxFactorExposure["Growth"] = 1.0;
                constraints.maxFactorExposure["Inflation"] = 0.5;
                constraints.maxFactorExposure["Volatility"] = 1.0;
                constraints.maxDailyLoss = 100000.0;
                constraints.maxDrawdownFromPeak = 500000.0;

                PositionSizing sizing = PositionSizer::computePositionSize(
                    baseNotional, portfolioRisk, currentRegime, constraints
                );

                std::cout << "Position Sizing Recommendation:" << std::endl;
                std::cout << "  Base Notional: $" << baseNotional << std::endl;
                std::cout << "  Recommended Notional: $" << sizing.recommendedNotional << std::endl;
                std::cout << "  Recommended Shares (ES contracts): " << sizing.recommendedShares << std::endl;
                std::cout << "  Recommended Leverage: " << sizing.recommendedLeverage << "x" << std::endl;
                std::cout << std::endl;

                std::cout << "Risk Metrics at Recommended Size:" << std::endl;
                std::cout << "  Expected Daily Volatility: $" << sizing.expectedDailyVol << std::endl;
                std::cout << "  Expected Worst Case (2s): $" << sizing.expectedWorstCaseDaily << std::endl;
                std::cout << "  Max Monthly Drawdown: $" << sizing.maxMonthlyDrawdown << std::endl;
                std::cout << std::endl;

                std::cout << "Position Status:" << std::endl;
                std::cout << "  Within Constraints: " << (sizing.isWithinConstraints ? "YES" : "NO") << std::endl;
                if (!sizing.isWithinConstraints) {
                    std::cout << "  Breaches:" << std::endl;
                    for (const auto& breach : sizing.constraintBreaches) {
                        std::cout << "    - " << breach << std::endl;
                    }
                }
                std::cout << "  Should Reduce Size: " << (sizing.shouldReduceSize ? "YES" : "NO") << std::endl;
                std::cout << "  Should Add Hedge: " << (sizing.shouldAddHedge ? "YES" : "NO") << std::endl;
                std::cout << std::endl;

                // Hedging recommendation
                if (sizing.shouldAddHedge) {
                    HedgingStrategy hedge = PositionSizer::recommendHedge(
                        sizing.recommendedNotional, portfolioRisk, currentRegime
                    );

                    std::cout << "Hedging Recommendation:" << std::endl;
                    std::cout << "  Instrument: " << hedge.hedgeInstrument << std::endl;
                    std::cout << "  Hedge Ratio: " << (hedge.hedgeRatio * 100.0) << "%" << std::endl;
                    std::cout << "  Hedge Size: $" << hedge.hedgeSize << std::endl;
                    std::cout << "  Estimated Annual Cost: $" << hedge.estimatedCost << " (" << hedge.costPercent << "%)" << std::endl;
                    std::cout << "  Rationale: " << hedge.rationale << std::endl;
                    std::cout << "  Hedged Risks:" << std::endl;
                    for (const auto& risk : hedge.hedgedRisks) {
                        std::cout << "    - " << risk << std::endl;
                    }
                }
                std::cout << std::endl;

                // STEP 8: Generate trading signal
                std::cout << "Step 8: Generating trading signal..." << std::endl;

                TradeSignal signal = SignalGenerator::generateSignal(
                    currentRegime, factors, portfolioRisk
                );

                std::cout << "Signal Direction: ";
                switch (signal.direction) {
                    case SignalDirection::LONG: std::cout << "LONG"; break;
                    case SignalDirection::SHORT: std::cout << "SHORT"; break;
                    case SignalDirection::FLAT: std::cout << "FLAT"; break;
                }
                std::cout << std::endl;
                std::cout << "  Conviction: " << (signal.conviction * 100.0) << "%" << std::endl;
                std::cout << "  Reason: " << signal.entryReason << std::endl;
                std::cout << "  Contributing Factors:" << std::endl;
                for (const auto& factor : signal.factorsContributing) {
                    std::cout << "    - " << factor << std::endl;
                }
                std::cout << std::endl;

                // STEP 9: Compute VaR and Expected Shortfall
                std::cout << "Step 9: Computing VaR and Expected Shortfall..." << std::endl;

                double var95 = PositionSizer::computeDailyVaR(
                    sizing.recommendedNotional, portfolioRisk, 0.95);
                double var99 = PositionSizer::computeDailyVaR(
                    sizing.recommendedNotional, portfolioRisk, 0.99);
                double es95 = VaRMonitor::computeExpectedShortfall(
                    sizing.recommendedNotional, portfolioRisk, 0.95);

                std::cout << "  VaR (95%): $" << std::fixed << std::setprecision(0) << var95 << std::endl;
                std::cout << "  VaR (99%): $" << var99 << std::endl;
                std::cout << "  Expected Shortfall (95%): $" << es95 << std::endl;
                std::cout << std::endl;

                // Write results to file
                try {
                    std::ofstream outputFile("./output.txt");
                    if (!outputFile.is_open()) {
                        Logger::error("Failed to open output.txt for writing");
                        return 1;
                    }

                    outputFile << frobenius << std::endl;
                    for (int k = 0; k < factors.numFactors; k++) {
                        outputFile << factors.factorLabels[k] << ":" << factors.labelConfidences[k] << std::endl;
                    }
                    outputFile << "POSITION_SIZING:" << sizing.recommendedNotional << ":" << sizing.recommendedShares << std::endl;
                    outputFile << "REGIME:" << currentRegime.riskLabel << ":" << currentRegime.volatilityMultiplier << std::endl;

                    // Signal output
                    std::string dirStr = (signal.direction == SignalDirection::LONG) ? "LONG" :
                                        (signal.direction == SignalDirection::SHORT) ? "SHORT" : "FLAT";
                    outputFile << "SIGNAL:" << dirStr << ":" << signal.conviction << std::endl;
                    outputFile << "VAR_95:" << var95 << std::endl;
                    outputFile << "ES_95:" << es95 << std::endl;

                    outputFile.close();

                    Logger::info("Analysis completed successfully", {
                        {"factors", factors.numFactors},
                        {"variance_explained", factors.cumulativeVarianceExplained},
                        {"position_notional", sizing.recommendedNotional},
                        {"regime", currentRegime.riskLabel},
                        {"signal", dirStr},
                        {"conviction", signal.conviction}
                    });

                } catch (const std::exception& e) {
                    Logger::error("Failed to write output file", e);
                    return 1;
                }

                std::cout << "=========================================" << std::endl;
                std::cout << "Results written to output.txt" << std::endl;
                std::cout << "=========================================" << std::endl;

            } catch (const std::exception& e) {
                Logger::critical("Phase 1 analysis failed", e);
                return 1;
            }
        } else if (std::strcmp(argv[1], "backtest") == 0) {
            // BACKTEST MODE: Run 17-year historical simulation

            try {
                Logger::info("Backtest started", {
                    {"mode", "backtest"},
                    {"timestamp", std::time(nullptr)}
                });

                std::cout << "=========================================" << std::endl;
                std::cout << "BACKTEST: 17-Year Historical Simulation" << std::endl;
                std::cout << "=========================================" << std::endl;
                std::cout << std::endl;

                // Generate synthetic historical data (2007-2024)
                std::cout << "Generating synthetic historical data (2007-2024)..." << std::endl;
                auto historicalData = Backtester::generateSyntheticHistory(17, 252);
                std::cout << "Generated " << historicalData.size() << " trading days" << std::endl;
                std::cout << std::endl;

                // Configure backtest
                BacktestConfig config;
                config.initialCapital = 10000000.0;  // $10M
                config.baseNotional = 5000000.0;     // $5M base position
                config.commissionPerTrade = 2.50;
                config.slippageBps = 1.0;
                config.riskFreeRate = 0.04;

                // Run backtest
                std::cout << "Running backtest..." << std::endl;
                BacktestResult btResult = Backtester::run(config, historicalData);

                // Print results
                Backtester::printSummary(btResult);

                // VaR monitoring report
                VaRMonitor varMonitor;
                RiskDecomposition riskDecomp;
                riskDecomp.numFactors = 3;
                riskDecomp.factorLabels = {"Growth", "Inflation", "Volatility"};
                riskDecomp.factorSensitivities = {0.6, -0.2, -0.8};
                riskDecomp.factorRiskContributions = {0.25, 0.08, 0.15};
                riskDecomp.componentContributions = {0.52, 0.17, 0.31};
                riskDecomp.totalRisk = 0.012;
                riskDecomp.totalVariance = 0.000144;

                for (const auto& daily : btResult.dailyPnls) {
                    varMonitor.updatePosition(
                        daily.date, daily.position, daily.pnl, riskDecomp);
                }

                VaRReport varReport = varMonitor.generateReport();
                std::cout << "VaR Monitoring Report:" << std::endl;
                std::cout << "  Observations: " << varReport.totalObservations << std::endl;
                std::cout << "  Breach Rate: " << std::fixed << std::setprecision(1)
                          << (varReport.breachRate * 100.0) << "% (target: 5%)" << std::endl;
                std::cout << "  Calibration: " << varReport.calibrationMessage << std::endl;
                std::cout << "  Historical ES: $" << std::setprecision(0) << varReport.expectedShortfall << std::endl;
                std::cout << std::endl;

                // Write backtest results to file
                try {
                    std::ofstream outputFile("./backtest_results.txt");
                    if (outputFile.is_open()) {
                        outputFile << std::fixed << std::setprecision(4);
                        outputFile << "SHARPE:" << btResult.sharpeRatio << std::endl;
                        outputFile << "ANNUALIZED_RETURN:" << btResult.annualizedReturn << std::endl;
                        outputFile << "MAX_DRAWDOWN:" << btResult.maxDrawdown << std::endl;
                        outputFile << "WIN_RATE:" << btResult.winRate << std::endl;
                        outputFile << "CALMAR:" << btResult.calmarRatio << std::endl;
                        outputFile << "KELLY:" << btResult.halfKellyFraction << std::endl;
                        outputFile << "VAR_BREACH_RATE:" << varReport.breachRate << std::endl;
                        outputFile << "NUM_TRADES:" << btResult.numTrades << std::endl;
                        outputFile.close();
                        std::cout << "Results written to backtest_results.txt" << std::endl;
                    }
                } catch (const std::exception& e) {
                    Logger::error("Failed to write backtest results", e);
                }

                Logger::info("Backtest completed", {
                    {"sharpe", btResult.sharpeRatio},
                    {"max_drawdown", btResult.maxDrawdown},
                    {"annualized_return", btResult.annualizedReturn},
                    {"win_rate", btResult.winRate}
                });

            } catch (const std::exception& e) {
                Logger::critical("Backtest failed", e);
                return 1;
            }
        } else if (std::strcmp(argv[1], "live") == 0) {
            // LIVE MODE: Connect to IB, generate signal, place order, monitor VaR

            try {
                Logger::info("Live trading started", {
                    {"mode", "live"},
                    {"timestamp", std::time(nullptr)}
                });

                std::cout << "=========================================" << std::endl;
                std::cout << "LIVE TRADING MODE" << std::endl;
                std::cout << "=========================================" << std::endl;
                std::cout << std::endl;

                // Connect to IB paper trading gateway
                IBGateway ib;
                std::string ibHost = "127.0.0.1";
                int ibPort = 7497;  // Paper trading

                const char* ibPortEnv = std::getenv("IB_PORT");
                if (ibPortEnv) {
                    ibPort = std::atoi(ibPortEnv);
                }

                std::cout << "Connecting to IB Gateway at " << ibHost << ":" << ibPort << "..." << std::endl;

                if (!ib.connect(ibHost, ibPort, 1)) {
                    Logger::error("Failed to connect to IB Gateway");
                    std::cerr << "Failed to connect to IB Gateway. Ensure TWS/Gateway is running." << std::endl;
                    return 1;
                }

                // Get account info
                double balance = ib.getAccountBalance();
                std::cout << "Account Balance: $" << std::fixed << std::setprecision(2) << balance << std::endl;

                // Get current position
                IBPosition esPos = ib.getPosition("ES");
                std::cout << "Current ES Position: " << esPos.quantity << " contracts" << std::endl;
                std::cout << std::endl;

                // Generate signal from current market conditions
                // In production, these would come from live data feeds
                MacroRegime regime = PositionSizer::classifyRegime(
                    18.0, 100.0, 110.0, 120.0, 0.92);

                TradeSignal signal = SignalGenerator::generateSignalFromObservables(
                    regime.vixLevel, regime.yieldCurveSlope,
                    0.5, -0.2, regime);

                std::cout << "Generated Signal: ";
                switch (signal.direction) {
                    case SignalDirection::LONG: std::cout << "LONG"; break;
                    case SignalDirection::SHORT: std::cout << "SHORT"; break;
                    case SignalDirection::FLAT: std::cout << "FLAT"; break;
                }
                std::cout << " (conviction: " << (signal.conviction * 100.0) << "%)" << std::endl;

                // Place order if signal is not FLAT
                if (signal.direction != SignalDirection::FLAT) {
                    OrderDirection dir = (signal.direction == SignalDirection::LONG)
                                        ? OrderDirection::BUY : OrderDirection::SELL;
                    int qty = static_cast<int>(signal.conviction * 5);  // 1-5 contracts based on conviction
                    if (qty < 1) qty = 1;

                    int orderId = ib.placeOrder("ES", dir, qty, OrderType::MARKET);
                    if (orderId > 0) {
                        std::cout << "Order placed: ID=" << orderId << std::endl;

                        // Check latency
                        IBOrder order = ib.getOrder(orderId);
                        if (order.roundTripLatencyNs > 0) {
                            double latencyMs = order.roundTripLatencyNs / 1e6;
                            std::cout << "Round-trip latency: " << std::setprecision(1)
                                      << latencyMs << "ms" << std::endl;
                        }
                    }
                }

                std::cout << std::endl;
                std::cout << "Average latency: "
                          << (ib.getAverageLatencyNs() / 1e6) << "ms" << std::endl;

                ib.disconnect();

                Logger::info("Live trading cycle completed");

            } catch (const std::exception& e) {
                Logger::critical("Live trading failed", e);
                return 1;
            }
        } else if (std::strcmp(argv[1], "upload-daily") == 0) {
            // Upload the daily analysis results to S3

            Logger::info("S3 upload started");

            const char* s3BucketEnv = std::getenv("S3_BUCKET");
            std::string s3Bucket = s3BucketEnv ? s3BucketEnv : "inverted-yield-trader-daily-results";

            try {
                std::ifstream inputFile("./output.txt");
                if (!inputFile.is_open()) {
                    Logger::error("output.txt not found. Run 'covariance' mode first.");
                    return 1;
                }

                std::string today = getDateDaysAgo(0);
                std::string s3Key = "positions/" + today + ".json";

                std::stringstream buffer;
                buffer << inputFile.rdbuf();
                inputFile.close();

                json dailyResult;
                dailyResult["date"] = today;
                dailyResult["timestamp"] = std::time(nullptr);
                dailyResult["output"] = buffer.str();

                std::string jsonContent = dailyResult.dump(2);

                Logger::info("Uploading to S3", {
                    {"bucket", s3Bucket},
                    {"key", s3Key},
                    {"size_bytes", jsonContent.size()}
                });

                Aws::S3::S3Client s3Client;
                Aws::S3::Model::PutObjectRequest putRequest;
                putRequest.SetBucket(s3Bucket);
                putRequest.SetKey(s3Key);
                putRequest.SetBody(std::make_shared<std::stringstream>(jsonContent));

                auto outcome = s3Client.PutObject(putRequest);

                if (outcome.IsSuccess()) {
                    Logger::info("S3 upload successful", {
                        {"bucket", s3Bucket},
                        {"key", s3Key}
                    });
                    std::cout << "Successfully uploaded to s3://" << s3Bucket << "/" << s3Key << std::endl;
                } else {
                    Logger::error("S3 upload failed", {
                        {"error", outcome.GetError().GetMessage()}
                    });
                    return 1;
                }

            } catch (const std::exception& e) {
                Logger::critical("S3 upload failed", e);
                return 1;
            }
        }
    }

    Aws::ShutdownAPI(options); // Should only be called once.
    return result;
}
