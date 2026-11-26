# API Setup Guide - Phase 1

## Overview
We're adding two new data providers to fetch all 8 economic indicators:
1. **FRED API** - 7 economic indicators (free, unlimited)
2. **Polygon.io** - VIX data (free tier: 5 calls/min)

---

## 1. FRED API Setup (5 minutes)

### Step 1: Create FRED Account
1. Go to: https://fredaccount.stlouisfed.org/login/secure/
2. Click "Create New Account"
3. Fill in:
   - Email address
   - Password
   - Name
4. Verify your email

### Step 2: Request API Key
1. After logging in, go to: https://fred.stlouisfed.org/docs/api/api_key.html
2. Click "Request API Key"
3. Fill in the form:
   - API Key Name: "InvertedYieldCurveTrader"
   - Purpose: "Personal algorithmic trading project"
   - Are you a student? (Yes/No)
4. Agree to terms and submit
5. **Copy your API key** - it looks like: `a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6`

### Step 3: Test Your API Key
```bash
# Replace YOUR_API_KEY with your actual key
curl "https://api.stlouisfed.org/fred/series/observations?series_id=CPIAUCSL&api_key=YOUR_API_KEY&file_type=json&limit=10"
```

Expected response:
```json
{
  "realtime_start": "2025-12-17",
  "realtime_end": "2025-12-17",
  "observation_start": "1776-07-04",
  "observation_end": "9999-12-31",
  "units": "lin",
  "output_type": 1,
  "file_type": "json",
  "order_by": "observation_date",
  "sort_order": "asc",
  "count": 10,
  "offset": 0,
  "limit": 10,
  "observations": [...]
}
```

### Step 4: Save API Key
**Option A: Environment Variable (Recommended for local dev)**
```bash
# Add to ~/.bashrc or ~/.zshrc
export FRED_API_KEY="your_api_key_here"

# Reload shell
source ~/.bashrc
```

**Option B: AWS Secrets Manager (Recommended for production)**
```bash
aws secretsmanager create-secret \
    --name trading/fred_api_key \
    --description "FRED API key for economic data" \
    --secret-string "your_api_key_here"
```

**Option C: Lambda Environment Variable (Simple)**
- Add to Lambda function configuration in AWS Console
- Key: `FRED_API_KEY`
- Value: `your_api_key_here`

---

## 2. Polygon.io Setup (5 minutes)

### Step 1: Create Polygon Account
1. Go to: https://polygon.io/dashboard/signup
2. Sign up with:
   - Email
   - Password
   - Name
3. Verify your email

### Step 2: Get API Key
1. Log in to: https://polygon.io/dashboard
2. Your API key is displayed on the dashboard
3. Default plan: "Starter" (Free)
   - 5 API calls per minute
   - Delayed data (15 minutes for stocks, real-time for indices like VIX)
4. **Copy your API key** - it looks like: `abc123XYZ789def456GHI`

### Step 3: Test Your API Key
```bash
# Replace YOUR_API_KEY with your actual key
# Fetch VIX data (last 10 days)
curl "https://api.polygon.io/v2/aggs/ticker/I:VIX/range/1/day/2025-12-01/2025-12-17?apiKey=YOUR_API_KEY"
```

Expected response:
```json
{
  "ticker": "I:VIX",
  "queryCount": 10,
  "resultsCount": 10,
  "adjusted": true,
  "results": [
    {
      "v": 0,
      "vw": 0,
      "o": 14.23,
      "c": 14.42,
      "h": 15.01,
      "l": 13.98,
      "t": 1733097600000,
      "n": 0
    },
    ...
  ],
  "status": "OK"
}
```

### Step 4: Save API Key
**Option A: Environment Variable**
```bash
# Add to ~/.bashrc or ~/.zshrc
export POLYGON_API_KEY="your_api_key_here"

# Reload shell
source ~/.bashrc
```

**Option B: AWS Secrets Manager**
```bash
aws secretsmanager create-secret \
    --name trading/polygon_api_key \
    --description "Polygon.io API key for market data" \
    --secret-string "your_api_key_here"
```

**Option C: Lambda Environment Variable**
- Add to Lambda function configuration
- Key: `POLYGON_API_KEY`
- Value: `your_api_key_here`

---

## 3. Verify Both APIs Work

Create a test script:

```python
# test_apis.py
import os
import requests

def test_fred_api():
    api_key = os.getenv('FRED_API_KEY')
    if not api_key:
        print("❌ FRED_API_KEY not set")
        return False

    url = f"https://api.stlouisfed.org/fred/series/observations"
    params = {
        'series_id': 'CPIAUCSL',
        'api_key': api_key,
        'file_type': 'json',
        'limit': 1
    }

    try:
        response = requests.get(url, params=params)
        response.raise_for_status()
        data = response.json()
        if 'observations' in data:
            print("✅ FRED API working!")
            print(f"   Latest CPI: {data['observations'][-1]}")
            return True
        else:
            print(f"❌ FRED API unexpected response: {data}")
            return False
    except Exception as e:
        print(f"❌ FRED API error: {e}")
        return False

def test_polygon_api():
    api_key = os.getenv('POLYGON_API_KEY')
    if not api_key:
        print("❌ POLYGON_API_KEY not set")
        return False

    url = f"https://api.polygon.io/v2/aggs/ticker/I:VIX/prev"
    params = {'apiKey': api_key}

    try:
        response = requests.get(url, params=params)
        response.raise_for_status()
        data = response.json()
        if data.get('status') == 'OK':
            print("✅ Polygon.io API working!")
            print(f"   Latest VIX: {data['results'][0]['c']}")
            return True
        else:
            print(f"❌ Polygon.io unexpected response: {data}")
            return False
    except Exception as e:
        print(f"❌ Polygon.io error: {e}")
        return False

if __name__ == '__main__':
    print("Testing API connectivity...\n")
    fred_ok = test_fred_api()
    polygon_ok = test_polygon_api()

    print("\n" + "="*50)
    if fred_ok and polygon_ok:
        print("✅ All APIs configured correctly!")
        print("You can proceed with Phase 1 implementation.")
    else:
        print("❌ Some APIs failed. Please check your API keys.")
```

Run the test:
```bash
pip install requests
python test_apis.py
```

---

## 4. Expected Rate Limits

### FRED API
- **Rate limit:** None for reasonable use
- **Recommended:** <1000 requests/day
- **Our usage:** ~7 requests/day (one per indicator)
- **Status:** ✅ Well within limits

### Polygon.io Free Tier
- **Rate limit:** 5 API calls per minute
- **Our usage:** 1 request/day (VIX only)
- **Status:** ✅ Well within limits

### Alpha Vantage (keeping as backup)
- **Rate limit:** 5 API calls per minute, 25 per day (free tier)
- **Our usage:** 0 (replaced by FRED + Polygon)
- **Status:** Keep API key but don't use

---

## 5. Lambda Configuration

### Environment Variables to Add
1. **FRED_API_KEY:** Your FRED API key
2. **POLYGON_API_KEY:** Your Polygon.io API key
3. **ALPHA_VANTAGE_API_KEY:** `YOUR_ALPHA_VANTAGE_API_KEY` (existing, keep as backup)

### AWS Console Steps
1. Go to Lambda console
2. Select your function
3. Configuration → Environment variables
4. Add new variables:
   - Key: `FRED_API_KEY`, Value: `<your_fred_key>`
   - Key: `POLYGON_API_KEY`, Value: `<your_polygon_key>`
5. Save

---

## 6. S3 Bucket Structure

Data will be stored in `s3://alpha-insights/` with this structure:

```
alpha-insights/
├── inflation/
│   └── 2025-12-17          # FRED: CPI data
├── yield-10-year/
│   └── 2025-12-17          # FRED: 10Y Treasury
├── yield-2-year/
│   └── 2025-12-17          # FRED: 2Y Treasury
├── fed-funds-rate/
│   └── 2025-12-17          # FRED: Federal Funds Rate (NEW)
├── unemployment/
│   └── 2025-12-17          # FRED: Unemployment Rate (NEW)
├── gdp/
│   └── 2025-12-17          # FRED: GDP Growth (NEW)
├── consumer-sentiment/
│   └── 2025-12-17          # FRED: U of Michigan sentiment (NEW)
├── ism-manufacturing/
│   └── 2025-12-17          # FRED: ISM PMI (NEW)
└── vix/
    └── 2025-12-17          # Polygon: VIX (NEW)
```

---

## 7. Next Steps

Once APIs are configured:
1. ✅ FRED API key obtained
2. ✅ Polygon.io API key obtained
3. ✅ Environment variables set
4. ✅ Test script passed
5. → Proceed to implement FRED client
6. → Proceed to implement Polygon client
7. → Update Lambda function
8. → Deploy and test

---

## Troubleshooting

### "Invalid API Key" error
- Double-check you copied the key correctly (no extra spaces)
- Verify the key is active (check provider dashboard)
- Try regenerating the key

### "Rate limit exceeded"
- FRED: Very unlikely (no hard limits)
- Polygon: Wait 60 seconds and retry
- Check if you're making too many requests

### "Series not found" error (FRED)
- Verify the series ID is correct (see next section)
- Some series may be discontinued
- Try browsing https://fred.stlouisfed.org/ to find the right series

### Network/timeout errors
- Check internet connectivity
- Verify firewall isn't blocking requests
- Try with a longer timeout value

---

## FRED Series IDs Reference

For quick reference, here are all the FRED series we'll use:

| Indicator | Series ID | Frequency |
|-----------|-----------|-----------|
| CPI (Inflation) | `CPIAUCSL` | Monthly |
| 10-Year Treasury | `DGS10` | Daily |
| 2-Year Treasury | `DGS2` | Daily |
| Federal Funds Rate | `FEDFUNDS` | Monthly |
| Unemployment Rate | `UNRATE` | Monthly |
| Real GDP Growth | `A191RL1Q225SBEA` | Quarterly |
| Consumer Sentiment | `UMCSENT` | Monthly |
| ISM Manufacturing | `NAPM` | Monthly |

Browse more series: https://fred.stlouisfed.org/

---

## Security Best Practices

1. **Never commit API keys to Git**
   - Add to `.gitignore`: `.env`, `*.key`
   - Use environment variables or Secrets Manager

2. **Rotate keys periodically**
   - Every 6-12 months
   - Immediately if compromised

3. **Monitor usage**
   - Check API provider dashboards weekly
   - Set up alerts for unusual activity

4. **Use least privilege**
   - Read-only API keys when possible
   - Separate keys for dev vs. production
