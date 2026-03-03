//
//  IBGateway.hpp
//  InvertedYieldCurveTrader
//
//  Interactive Brokers TWS API gateway for order execution and position tracking.
//  Socket-based client implementing essential EClient/EWrapper messages.
//
//  Created by Ryan Hamby on 1/15/26.
//

#ifndef IB_GATEWAY_HPP
#define IB_GATEWAY_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <atomic>
#include <mutex>
#include <functional>

/**
 * OrderDirection: Buy or sell
 */
enum class OrderDirection {
    BUY,
    SELL
};

/**
 * OrderType: Market, limit, or stop order
 */
enum class OrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT
};

/**
 * OrderStatus: Lifecycle states
 */
enum class OrderStatus {
    PENDING_SUBMIT,
    SUBMITTED,
    FILLED,
    PARTIALLY_FILLED,
    CANCELLED,
    ERROR
};

/**
 * IBOrder: Order with fill information and latency tracking
 */
struct IBOrder {
    int orderId;
    std::string symbol;
    OrderDirection direction;
    int quantity;
    OrderType orderType;
    double limitPrice;
    double stopPrice;

    // Fill information
    OrderStatus status;
    double fillPrice;
    std::string fillTime;

    // Latency tracking (nanoseconds)
    long long submitTimeNs;
    long long ackTimeNs;
    long long fillTimeNs;
    long long roundTripLatencyNs;  // submit → fill acknowledgment
};

/**
 * Position: Current portfolio position
 */
struct IBPosition {
    std::string symbol;
    int quantity;
    double avgCost;
    double marketPrice;
    double marketValue;
    double unrealizedPnL;
    double realizedPnL;
};

/**
 * MarketData: Real-time quote
 */
struct MarketData {
    std::string symbol;
    double bid;
    double ask;
    double last;
    int bidSize;
    int askSize;
    int lastSize;
    long long timestamp;
};

/**
 * IBGateway: Interactive Brokers TWS API client
 *
 * Socket-based implementation of essential EClient messages:
 *   - Connect handshake
 *   - Place/cancel orders
 *   - Request positions and account summary
 *
 * Default: paper trading gateway (port 7497)
 * Live: port 7496
 */
class IBGateway {
public:
    IBGateway();
    ~IBGateway();

    /**
     * Connect to TWS/Gateway
     *
     * @param host: TWS host (default "127.0.0.1")
     * @param port: TWS port (7497=paper, 7496=live)
     * @param clientId: Client identifier
     * @return True if connected successfully
     */
    bool connect(
        const std::string& host = "127.0.0.1",
        int port = 7497,
        int clientId = 1
    );

    /**
     * Disconnect from TWS
     */
    void disconnect();

    /**
     * Check if connected
     */
    bool isConnected() const;

    /**
     * Place an order
     *
     * @param symbol: Contract symbol (e.g., "ES")
     * @param direction: BUY or SELL
     * @param quantity: Number of contracts
     * @param orderType: MARKET, LIMIT, STOP, STOP_LIMIT
     * @param limitPrice: Limit price (0.0 for market orders)
     * @param stopPrice: Stop price (0.0 if not stop order)
     * @return Order ID (negative on failure)
     */
    int placeOrder(
        const std::string& symbol,
        OrderDirection direction,
        int quantity,
        OrderType orderType = OrderType::MARKET,
        double limitPrice = 0.0,
        double stopPrice = 0.0
    );

    /**
     * Cancel an open order
     *
     * @param orderId: Order to cancel
     * @return True if cancellation request sent
     */
    bool cancelOrder(int orderId);

    /**
     * Get current position for a symbol
     *
     * @param symbol: Contract symbol
     * @return Position details
     */
    IBPosition getPosition(const std::string& symbol) const;

    /**
     * Get all current positions
     *
     * @return Map of symbol → position
     */
    std::map<std::string, IBPosition> getAllPositions() const;

    /**
     * Get account balance
     *
     * @return Net liquidation value
     */
    double getAccountBalance() const;

    /**
     * Request market data for a symbol
     *
     * @param symbol: Contract symbol
     * @return Latest market data snapshot
     */
    MarketData requestMarketData(const std::string& symbol);

    /**
     * Get order by ID
     *
     * @param orderId: Order ID
     * @return Order details with fill info
     */
    IBOrder getOrder(int orderId) const;

    /**
     * Get all orders
     */
    std::vector<IBOrder> getAllOrders() const;

    /**
     * Get average round-trip latency (nanoseconds)
     *
     * @return Average latency across all filled orders
     */
    long long getAverageLatencyNs() const;

    /**
     * Set callback for order status updates
     */
    void setOrderCallback(std::function<void(const IBOrder&)> callback);

    /**
     * Set callback for market data updates
     */
    void setMarketDataCallback(std::function<void(const MarketData&)> callback);

private:
    int socketFd_;
    std::atomic<bool> connected_;
    std::atomic<int> nextOrderId_;
    int clientId_;

    // Thread-safe order and position storage
    mutable std::mutex orderMutex_;
    std::map<int, IBOrder> orders_;

    mutable std::mutex positionMutex_;
    std::map<std::string, IBPosition> positions_;

    double accountBalance_;
    mutable std::mutex balanceMutex_;

    // Callbacks
    std::function<void(const IBOrder&)> orderCallback_;
    std::function<void(const MarketData&)> marketDataCallback_;

    /**
     * Send raw message to TWS socket
     */
    bool sendMessage(const std::string& message);

    /**
     * Read response from TWS socket
     */
    std::string readMessage();

    /**
     * Build TWS API connect handshake message
     */
    std::string buildConnectMessage(int clientId);

    /**
     * Build place order message
     */
    std::string buildPlaceOrderMessage(const IBOrder& order);

    /**
     * Build cancel order message
     */
    std::string buildCancelOrderMessage(int orderId);

    /**
     * Build request positions message
     */
    std::string buildRequestPositionsMessage();

    /**
     * Build request account summary message
     */
    std::string buildRequestAccountMessage();

    /**
     * Parse incoming EWrapper messages
     */
    void parseResponse(const std::string& response);

    /**
     * Parse order status message
     */
    void handleOrderStatus(const std::string& message);

    /**
     * Parse execution details message
     */
    void handleExecutionDetails(const std::string& message);

    /**
     * Parse position update message
     */
    void handlePositionUpdate(const std::string& message);

    /**
     * Get current time in nanoseconds
     */
    static long long currentTimeNs();
};

#endif // IB_GATEWAY_HPP
