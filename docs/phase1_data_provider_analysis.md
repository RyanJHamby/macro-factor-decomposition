# Phase 1: Data Provider Analysis

## Current State
- **Provider:** Alpha Vantage (free tier)
- **Limitation:** 5 API calls per minute (25 calls per day)
- **Currently collecting:** 2 indicators (Inflation, Treasury Yields)
- **Problem:** Need 8 indicators → 8 API calls daily, but free tier is too restrictive

## Target: 8 Economic Indicators

### Currently Have ✅
1. **Inflation (CPI)** - Alpha Vantage `INFLATION` function
2. **Inverted Yield Curve (10Y-2Y)** - Alpha Vantage `TREASURY_YIELD` function

### Need to Add ❌
3. **VIX (Volatility Index)**
4. **Federal Funds Rate**
5. **Employment Data** (Non-farm payrolls or unemployment rate)
6. **GDP Growth Rate** (already configured but not enabled)
7. **Consumer Sentiment Index** (Michigan or Conference Board)
8. **ISM Manufacturing PMI**

---

## Option 1: FRED API (Federal Reserve Economic Data) ⭐ RECOMMENDED

### Overview
- **Cost:** FREE, unlimited requests
- **Provider:** Federal Reserve Bank of St. Louis
- **Coverage:** 800,000+ economic time series
- **API Key:** Free registration
- **Rate Limits:** None for reasonable use
- **Data Quality:** Official government data (gold standard)

### Available Indicators
| Indicator | FRED Series ID | Frequency | Notes |
|-----------|----------------|-----------|-------|
| ✅ Inflation (CPI) | `CPIAUCSL` | Monthly | Same as Alpha Vantage |
| ✅ 10-Year Treasury | `DGS10` | Daily | Treasury yield |
| ✅ 2-Year Treasury | `DGS2` | Daily | Treasury yield |
| ✅ Federal Funds Rate | `FEDFUNDS` | Monthly | Target rate |
| ✅ Unemployment Rate | `UNRATE` | Monthly | Employment proxy |
| ✅ GDP Growth | `A191RL1Q225SBEA` | Quarterly | Real GDP % change |
| ✅ Consumer Sentiment | `UMCSENT` | Monthly | U of Michigan |
| ✅ ISM Manufacturing | `NAPM` | Monthly | PMI index |
| ❌ VIX | N/A | - | Not available on FRED |

### Pros
- ✅ Free and unlimited
- ✅ 7 out of 8 indicators available
- ✅ Official government data (high quality)
- ✅ Simple JSON API
- ✅ Historical data back to 1950s+
- ✅ No rate limiting for daily fetches

### Cons
- ❌ No VIX data (need separate source)
- ❌ Monthly/Quarterly data (not daily for most)
- ❌ 1-day lag for some series

### Code Example
```python
import requests

API_KEY = "your_fred_api_key"
BASE_URL = "https://api.stlouisfed.org/fred/series/observations"

def fetch_fred_data(series_id, limit=100):
    params = {
        'series_id': series_id,
        'api_key': API_KEY,
        'file_type': 'json',
        'limit': limit,
        'sort_order': 'desc'
    }
    response = requests.get(BASE_URL, params=params)
    return response.json()

# Example: Fetch CPI
cpi_data = fetch_fred_data('CPIAUCSL')
```

---

## Option 2: Alpha Vantage Premium ($49.99/month)

### Overview
- **Cost:** $49.99/month (Premium tier)
- **Rate Limits:** 75 API calls per minute, 1200 per day
- **Coverage:** Economic indicators + market data

### Available Indicators
| Indicator | Alpha Vantage Function | Available? |
|-----------|------------------------|------------|
| ✅ Inflation | `INFLATION` | Yes |
| ✅ Treasury Yields | `TREASURY_YIELD` | Yes |
| ✅ Federal Funds Rate | `FEDERAL_FUNDS_RATE` | Yes |
| ✅ Unemployment | `UNEMPLOYMENT` | Yes |
| ✅ GDP | `REAL_GDP` | Yes |
| ✅ Consumer Sentiment | `CONSUMER_SENTIMENT` | Yes |
| ✅ VIX | TIME_SERIES_DAILY (^VIX) | Yes |
| ❌ ISM PMI | N/A | No |

### Pros
- ✅ All indicators in one place (except ISM)
- ✅ Keep existing integration
- ✅ Includes market data (VIX, stocks, etc.)
- ✅ Higher rate limits

### Cons
- ❌ Costs $49.99/month ($600/year)
- ❌ Still missing ISM PMI
- ❌ Need separate source for ISM anyway

---

## Option 3: Polygon.io (Market Data) + FRED (Economic Data)

### Overview
- **Polygon.io:** Market data (stocks, VIX, etc.)
- **FRED:** Economic indicators
- **Cost:** Polygon free tier or $29/month

### Available Indicators
**From Polygon.io:**
- ✅ VIX (^VIX symbol)
- ✅ Stock market data (ES futures, SPY, etc.)
- Rate limit: 5 calls/min (free) or unlimited (paid)

**From FRED:**
- ✅ All 7 economic indicators (see Option 1)

### Pros
- ✅ Free tier works for daily fetches
- ✅ Real-time market data
- ✅ Complete coverage of all 8 indicators
- ✅ Professional-grade data quality

### Cons
- ❌ Need to integrate 2 APIs
- ❌ More complex code (dual data sources)

---

## Option 4: Yahoo Finance (Free) + FRED (Free)

### Overview
- **Yahoo Finance:** Free market data via `yfinance` Python library
- **FRED:** Economic indicators
- **Cost:** $0

### Available Indicators
**From Yahoo Finance:**
- ✅ VIX (^VIX ticker)
- ✅ Market data (ES futures =ES, SPY, etc.)
- Rate limit: Reasonable use (100+ calls/day)

**From FRED:**
- ✅ All 7 economic indicators

### Pros
- ✅ Completely free
- ✅ Easy to use (`pip install yfinance`)
- ✅ Complete coverage
- ✅ Good for personal projects

### Cons
- ❌ Yahoo Finance is unofficial (may break)
- ❌ Not suitable for production systems
- ❌ Rate limiting is unclear

---

## Recommendation Matrix

| Use Case | Recommended | Why |
|----------|-------------|-----|
| **Personal project, limited budget** | FRED + yfinance | $0, complete coverage |
| **Semi-professional, prove concept** | FRED + Polygon.io free | $0, more reliable than yfinance |
| **Professional, willing to pay** | FRED + Polygon.io ($29) | Professional-grade, good value |
| **Want simplicity** | Alpha Vantage Premium ($50) | Single provider, but expensive |
| **Production system** | FRED + Polygon.io ($29) or Rithmic | Reliable, tested, supported |

---

## Final Recommendation: FRED + Polygon.io Free Tier ⭐

### Why This Combo?
1. **FRED is mandatory** - Best source for economic data, free, unlimited
2. **Polygon.io free tier** - 5 calls/min is enough for VIX daily fetch
3. **Total cost:** $0 initially, upgrade to $29/month if needed
4. **Coverage:** All 8 indicators
5. **Quality:** Professional-grade data
6. **Scalability:** Can upgrade Polygon later for real-time needs

### Implementation Plan
```
Lambda Function Updates:
1. Add FRED API client (fetch 7 indicators)
2. Add Polygon.io API client (fetch VIX)
3. Keep Alpha Vantage as backup

Data Flow:
- Daily EventBridge trigger at 12 UTC
- Fetch from FRED: CPI, yields, fed funds, unemployment, GDP, sentiment, ISM
- Fetch from Polygon: VIX
- Upload all to S3 in existing format
- C++ code reads from S3 (no changes needed)
```

### Next Steps
1. Register for FRED API key (https://fred.stlouisfed.org/docs/api/api_key.html)
2. Register for Polygon.io free tier (https://polygon.io/dashboard/signup)
3. Test both APIs locally
4. Update Lambda function
5. Deploy and validate

---

## API Registration Links

**FRED API:**
- Sign up: https://fredaccount.stlouisfed.org/login/secure/
- Get API key: https://fred.stlouisfed.org/docs/api/api_key.html
- Documentation: https://fred.stlouisfed.org/docs/api/fred/

**Polygon.io:**
- Sign up: https://polygon.io/dashboard/signup
- Free tier: 5 API calls per minute
- Documentation: https://polygon.io/docs/stocks/getting-started

**Alpha Vantage (backup):**
- Key: `YOUR_ALPHA_VANTAGE_API_KEY` (read by the Lambda from the `ALPHA_VANTAGE_API_KEY` env var)
- Keep for backward compatibility

---

## Estimated Time to Implement
- FRED API integration: 2 hours
- Polygon.io integration: 1 hour
- Testing: 1 hour
- Lambda deployment: 30 minutes
- **Total: ~4.5 hours**

## Cost Comparison
| Provider Setup | Monthly Cost | Annual Cost |
|----------------|--------------|-------------|
| FRED + Polygon Free | $0 | $0 |
| FRED + Polygon Starter | $29 | $348 |
| Alpha Vantage Premium | $50 | $600 |
| **Savings with FRED+Polygon:** | **$21-50/month** | **$252-600/year** |

---

## Decision
✅ **Proceed with FRED + Polygon.io Free Tier**

This gives us:
- All 8 indicators
- $0 cost (can upgrade later)
- Professional quality data
- Scalable architecture
