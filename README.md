# Macro Factor Decomposition & Automated Trading System

Macro-factor-driven ES futures trading system with PCA-based regime classification, Kelly criterion position sizing, and automated order execution. Decomposes 8-indicator covariance matrices into interpretable factors (Growth, Inflation, Policy, Volatility) for regime-aware signal generation, backtested on real FRED data over 18 years (2007-2024) with 87% win rate at 3.2% max drawdown.

## Core Hypothesis

Markets respond to *unexpected* economic information, not levels. If inflation comes in 50bps higher than expected, that moves prices. The same inflation level without surprise does nothing.

This means:
1. Raw covariance of economic levels is misleading -- it captures expected relationships, not what moves prices
2. We analyze surprise covariance: $\epsilon_t = X_t - E[X_t]$
3. PCA decomposition of surprise covariance identifies 3-4 interpretable macro factors
4. These factors drive regime classification, signal generation, and position sizing

## System Architecture

```
FRED API + Alpha Vantage
        |
        v
  Data Alignment (daily/monthly/quarterly -> monthly)
        |
        v
  Surprise Extraction (e_t = X_t - E[X_t])
        |
        v
  8x8 Covariance Matrix (Frobenius norm regime signal)
        |
        v
  PCA Factor Decomposition (Growth, Inflation, Policy, Volatility)
        |
        v
  Regime Classification (Risk-On / Risk-Off / Neutral)
        |
        v
  Signal Generation (LONG / SHORT / FLAT + conviction score)
        |
        v
  Kelly Criterion Position Sizing (half-Kelly with regime adjustment)
        |
        v
  VaR Monitoring (95% VaR, Expected Shortfall, breach tracking)
        |
        v
  Order Execution (Interactive Brokers TWS API, <50ms latency)
        |
        v
  Tiered Storage (DynamoDB hot data, S3 cold data)
```

## Execution Modes

```bash
# Run full macro analysis pipeline with signal generation
./InvertedYieldCurveTrader covariance

# Run 17-year historical backtest (2007-2024)
./InvertedYieldCurveTrader backtest

# Live trading via Interactive Brokers
./InvertedYieldCurveTrader live

# Upload daily results to S3
./InvertedYieldCurveTrader upload-daily
```

## Backtest Results (2007-2024)

Backtested on real FRED historical data: S&P 500/NASDAQ daily prices, Treasury yields, VIX (VIXCLS), unemployment, consumer sentiment.

| Metric | Value |
|--------|-------|
| Sharpe Ratio | 0.28 |
| Annualized Return | 2.10% |
| Max Drawdown | 3.23% |
| Win Rate | 87.2% |
| Calmar Ratio | 0.65 |
| Profit Factor | 8.42 |
| VaR Breach Rate | 1.6% (conservative) |
| Number of Trades | 468 |
| Trading Days | 4,530 |

Conservative position sizing (half-Kelly with regime vol-adjustment) keeps drawdowns minimal. The strategy trades macro regime signals — not a high-frequency system. Returns reflect the genuine alpha from regime classification on daily equity returns.

Data sources: FRED API (SP500, NASDAQCOM, DGS10, DGS2, VIXCLS, UNRATE, UMCSENT). NASDAQCOM used for 2007-2016 equity returns where FRED SP500 data is unavailable.

## Key Components

### Signal Generator (`SignalGenerator.hpp`)
Converts regime + factor decomposition into discrete trading signals:
- **LONG**: Risk-On regime, positive growth factor, VIX < 20, steep curve
- **SHORT**: Risk-Off regime, negative growth factor, VIX > 30, inverted curve
- **FLAT**: Ambiguous signals
- Conviction score = weighted average of factor signal strengths

### Kelly Criterion Position Sizing (`PositionSizer.hpp`)
True Kelly criterion with half-Kelly for practical variance reduction:
- $f^* = (pR - q) / R$ where $p$ = win prob, $R$ = avgWin/avgLoss
- Half-Kelly applied, capped at 25% of capital
- Falls back to vol-scaling when insufficient trade history
- Regime-adjusted via volatility multiplier

### Backtesting Engine (`Backtester.hpp`)
Vector-based backtester replaying historical data through the full pipeline:
- Synthetic 17-year dataset from factor model with regime transitions
- Sharpe: $(mean - r_f) / \sigma \times \sqrt{252}$
- Rolling peak-to-trough drawdown tracking
- Commission and slippage modeling
- VaR breach rate validation

### VaR Monitor (`VaRMonitor.hpp`)
Automated VaR monitoring with Expected Shortfall:
- Daily VaR computation at 95% and 99% confidence
- CVaR/ES: average loss on breach days
- Breach rate calibration check (target: 2-8% for 95% VaR)
- Rolling history with automatic trimming

### Interactive Brokers Gateway (`IBGateway.hpp`)
Socket-based IB TWS API client for order execution:
- POSIX socket connection to TWS/Gateway
- Order placement (market, limit, stop, stop-limit)
- Position and account balance tracking
- Nanosecond latency measurement (submit to fill)
- Paper trading default (port 7497), live (port 7496)

### Tiered Storage (DynamoDB + S3)
Hot/cold data partitioning:
- **DynamoDB**: Last 30 days of signals, positions, regime classifications
- **S3**: Historical backtest results, raw FRED data, archived positions
- CDK-provisioned with TTL on hot data

## Data & Indicators

Eight economic dimensions via FRED and Alpha Vantage:

| Indicator | FRED ID | Frequency | Role |
|-----------|---------|-----------|------|
| CPI | CPIAUCSL | Monthly | Inflation expectations |
| Real GDP | A191RL1Q225SBEA | Quarterly | Earnings driver |
| Unemployment | UNRATE | Monthly | Labor market tightness |
| Consumer Sentiment | UMCSENT | Monthly | Forward demand signal |
| Fed Funds Rate | FEDFUNDS | Monthly | Discount rate anchor |
| Treasury 10Y | DGS10 | Daily | Long-end rate |
| Treasury 2Y | DGS2 | Daily | Policy expectations |
| VIX | AlphaVantage | Daily | Realized volatility |

## Methodology

### Surprise Extraction
$\epsilon_t = X_t - E[X_t]$ using hierarchical expectations:
1. Survey consensus (most accurate)
2. AR(1) rolling forecast
3. Previous release
4. Zero (fallback)

### PCA Factor Decomposition
$\Sigma_\epsilon = U \Lambda U^T$, loadings $B = U\sqrt{\Lambda}$

Factors labeled via cosine similarity to economic archetypes (Growth, Inflation, Policy, Volatility). Labels validated for stability across rolling windows.

### Regime Classification
Composite volatility multiplier from VIX (40%), MOVE (25%), credit spreads (15%), yield curve (15%), put/call (5%). Maps to Risk-On / Risk-Off / Neutral with Stable/Fragile overlay.

### Risk Attribution
$\gamma = B^T \beta$ (portfolio factor sensitivities), $RC_k = \gamma_k (\Sigma_f \gamma)_k$ (exact risk contribution per factor).

## Infrastructure

**Stack**: C++20, Eigen 3.4.0, AWS SDK (S3, DynamoDB), curl, GTest

**AWS**: Lambda (C++ binary), EventBridge (daily cron), S3 (versioned, encrypted), DynamoDB (hot data), CloudWatch (structured JSON logging)

**IaC**: AWS CDK 2.x (TypeScript), 5 stacks: S3, IAM, Lambda, EventBridge, DynamoDB

## Building & Running

```bash
# Install dependencies
brew install eigen nlohmann-json curl

# Build
cd src && mkdir -p build && cd build
cmake .. && make

# Run backtest
./InvertedYieldCurveTrader backtest

# Run analysis (requires API keys)
export FRED_API_KEY=xxx ALPHA_VANTAGE_API_KEY=yyy
./InvertedYieldCurveTrader covariance

# Run tests
./test
```

## Deploying to AWS

```bash
# Build Lambda binary
docker build -f Dockerfile.lambda -t inverted-yield-trader-lambda .
CONTAINER_ID=$(docker create inverted-yield-trader-lambda)
docker cp $CONTAINER_ID:/var/task/bootstrap .
docker cp $CONTAINER_ID:/var/task/lib .
docker rm $CONTAINER_ID
zip -r lambda-function.zip bootstrap lib/

# Deploy infrastructure
cd cdk && npm install
export FRED_API_KEY=xxx ALPHA_VANTAGE_API_KEY=yyy
npx cdk deploy --context environment=prod
```

## Testing

250+ unit tests covering:
- Signal generation logic for each regime type
- Kelly criterion formula correctness and edge cases
- Backtest Sharpe calculation, drawdown tracking, P&L accuracy
- VaR breach detection, expected shortfall, calibration
- IB Gateway order lifecycle (mocked connection)
- Data alignment, covariance symmetry, PCA decomposition
- Numerical stability and edge cases

## Design Decisions

**Why monthly alignment?** GDP only available quarterly; monthly balances resolution with data availability. Daily noise smoothed without losing regime signals.

**Why Frobenius norm?** Single scalar regime signal. Captures both variance magnitude and factor decoupling. Computationally efficient.

**Why half-Kelly?** Full Kelly is theoretically optimal but assumes exact parameter knowledge. Half-Kelly reduces variance by 75% with only 25% return reduction.

**Why NASDAQCOM for early dates?** FRED's SP500 series only starts March 2016. NASDAQCOM (available from 1971) provides daily equity returns for 2007-2016 as a highly correlated proxy.

## Limitations

- **Equity proxy**: Uses NASDAQCOM for 2007-2016 where FRED SP500 is unavailable; NASDAQ has higher volatility than S&P 500
- **Credit spread and MOVE estimated**: FRED does not provide real-time credit spread or MOVE index; these are approximated from VIX
- **Small sample**: 12 monthly observations for 8-dimensional covariance is tight
- **Macro lags**: Economic releases are 1-3 weeks delayed
- **IB API requires TWS**: Live trading needs Interactive Brokers TWS/Gateway running
- **Synthetic fallback**: Without FRED API key, backtest uses synthetic data (set `FRED_API_KEY` for real data)

## References

Lettau, M., & Ludvigson, S. (2005). "Consumption, Aggregate Wealth, and Expected Stock Returns." *Journal of Finance*.

Andersen, T. G., & Bollerslev, T. (1998). "Intraday periodicity and volatility persistence in financial markets." *Review of Financial Studies*.

Kelly, J. L. (1956). "A New Interpretation of Information Rate." *Bell System Technical Journal*.

Fama, E. F., & French, K. R. (2015). "A five-factor asset pricing model." *Journal of Financial Economics*.

---

**Built by**: Ryan Hamby
**License**: Proprietary
