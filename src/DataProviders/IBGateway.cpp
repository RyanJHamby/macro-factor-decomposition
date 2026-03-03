//
//  IBGateway.cpp
//  InvertedYieldCurveTrader
//
//  Interactive Brokers TWS API gateway implementation.
//  Socket-based client using POSIX sockets.
//
//  Created by Ryan Hamby on 1/15/26.
//

#include "IBGateway.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>

IBGateway::IBGateway()
    : socketFd_(-1)
    , connected_(false)
    , nextOrderId_(1)
    , clientId_(1)
    , accountBalance_(0.0)
{}

IBGateway::~IBGateway()
{
    if (connected_) {
        disconnect();
    }
}

bool IBGateway::connect(
    const std::string& host,
    int port,
    int clientId)
{
    clientId_ = clientId;

    // Create socket
    socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd_ < 0) {
        std::cerr << "IBGateway: Failed to create socket" << std::endl;
        return false;
    }

    // Set up server address
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "IBGateway: Invalid address " << host << std::endl;
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    // Connect to TWS
    if (::connect(socketFd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "IBGateway: Connection failed to " << host << ":" << port << std::endl;
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    // Send API handshake
    std::string connectMsg = buildConnectMessage(clientId);
    if (!sendMessage(connectMsg)) {
        std::cerr << "IBGateway: Handshake failed" << std::endl;
        close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    // Read server response (version + next valid order ID)
    std::string response = readMessage();
    if (!response.empty()) {
        parseResponse(response);
    }

    connected_ = true;
    std::cout << "IBGateway: Connected to " << host << ":" << port
              << " (clientId=" << clientId << ")" << std::endl;

    return true;
}

void IBGateway::disconnect()
{
    if (socketFd_ >= 0) {
        close(socketFd_);
        socketFd_ = -1;
    }
    connected_ = false;
    std::cout << "IBGateway: Disconnected" << std::endl;
}

bool IBGateway::isConnected() const
{
    return connected_;
}

int IBGateway::placeOrder(
    const std::string& symbol,
    OrderDirection direction,
    int quantity,
    OrderType orderType,
    double limitPrice,
    double stopPrice)
{
    if (!connected_) {
        std::cerr << "IBGateway: Not connected" << std::endl;
        return -1;
    }

    int orderId = nextOrderId_++;

    IBOrder order;
    order.orderId = orderId;
    order.symbol = symbol;
    order.direction = direction;
    order.quantity = quantity;
    order.orderType = orderType;
    order.limitPrice = limitPrice;
    order.stopPrice = stopPrice;
    order.status = OrderStatus::PENDING_SUBMIT;
    order.fillPrice = 0.0;
    order.submitTimeNs = currentTimeNs();
    order.ackTimeNs = 0;
    order.fillTimeNs = 0;
    order.roundTripLatencyNs = 0;

    // Store order
    {
        std::lock_guard<std::mutex> lock(orderMutex_);
        orders_[orderId] = order;
    }

    // Send order to TWS
    std::string orderMsg = buildPlaceOrderMessage(order);
    if (!sendMessage(orderMsg)) {
        std::lock_guard<std::mutex> lock(orderMutex_);
        orders_[orderId].status = OrderStatus::ERROR;
        return -1;
    }

    // Read acknowledgment
    std::string response = readMessage();
    if (!response.empty()) {
        parseResponse(response);
    }

    // Update status
    {
        std::lock_guard<std::mutex> lock(orderMutex_);
        if (orders_[orderId].status == OrderStatus::PENDING_SUBMIT) {
            orders_[orderId].status = OrderStatus::SUBMITTED;
            orders_[orderId].ackTimeNs = currentTimeNs();
        }
    }

    std::cout << "IBGateway: Order " << orderId << " placed ("
              << symbol << " "
              << (direction == OrderDirection::BUY ? "BUY" : "SELL")
              << " " << quantity << ")" << std::endl;

    return orderId;
}

bool IBGateway::cancelOrder(int orderId)
{
    if (!connected_) return false;

    std::string cancelMsg = buildCancelOrderMessage(orderId);
    if (!sendMessage(cancelMsg)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(orderMutex_);
    if (orders_.count(orderId)) {
        orders_[orderId].status = OrderStatus::CANCELLED;
        return true;
    }

    return false;
}

IBPosition IBGateway::getPosition(const std::string& symbol) const
{
    std::lock_guard<std::mutex> lock(positionMutex_);
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        return it->second;
    }

    IBPosition empty;
    empty.symbol = symbol;
    empty.quantity = 0;
    empty.avgCost = 0.0;
    empty.marketPrice = 0.0;
    empty.marketValue = 0.0;
    empty.unrealizedPnL = 0.0;
    empty.realizedPnL = 0.0;
    return empty;
}

std::map<std::string, IBPosition> IBGateway::getAllPositions() const
{
    std::lock_guard<std::mutex> lock(positionMutex_);
    return positions_;
}

double IBGateway::getAccountBalance() const
{
    std::lock_guard<std::mutex> lock(balanceMutex_);
    return accountBalance_;
}

MarketData IBGateway::requestMarketData(const std::string& symbol)
{
    MarketData data;
    data.symbol = symbol;
    data.bid = 0.0;
    data.ask = 0.0;
    data.last = 0.0;
    data.bidSize = 0;
    data.askSize = 0;
    data.lastSize = 0;
    data.timestamp = currentTimeNs();

    if (!connected_) return data;

    // In a full implementation, would send a market data request
    // and parse the streaming response
    return data;
}

IBOrder IBGateway::getOrder(int orderId) const
{
    std::lock_guard<std::mutex> lock(orderMutex_);
    auto it = orders_.find(orderId);
    if (it != orders_.end()) {
        return it->second;
    }

    IBOrder empty;
    empty.orderId = -1;
    empty.status = OrderStatus::ERROR;
    return empty;
}

std::vector<IBOrder> IBGateway::getAllOrders() const
{
    std::lock_guard<std::mutex> lock(orderMutex_);
    std::vector<IBOrder> result;
    for (const auto& [id, order] : orders_) {
        result.push_back(order);
    }
    return result;
}

long long IBGateway::getAverageLatencyNs() const
{
    std::lock_guard<std::mutex> lock(orderMutex_);
    long long totalLatency = 0;
    int filledCount = 0;

    for (const auto& [id, order] : orders_) {
        if (order.status == OrderStatus::FILLED && order.roundTripLatencyNs > 0) {
            totalLatency += order.roundTripLatencyNs;
            filledCount++;
        }
    }

    return (filledCount > 0) ? totalLatency / filledCount : 0;
}

void IBGateway::setOrderCallback(std::function<void(const IBOrder&)> callback)
{
    orderCallback_ = callback;
}

void IBGateway::setMarketDataCallback(std::function<void(const MarketData&)> callback)
{
    marketDataCallback_ = callback;
}

// ===== Private Methods =====

bool IBGateway::sendMessage(const std::string& message)
{
    if (socketFd_ < 0) return false;

    ssize_t sent = send(socketFd_, message.c_str(), message.size(), 0);
    return (sent == static_cast<ssize_t>(message.size()));
}

std::string IBGateway::readMessage()
{
    if (socketFd_ < 0) return "";

    char buffer[4096];
    ssize_t bytesRead = recv(socketFd_, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        return std::string(buffer);
    }

    return "";
}

std::string IBGateway::buildConnectMessage(int clientId)
{
    // TWS API v100+ handshake
    // Format: "API\0" + length-prefixed version string + client ID
    std::ostringstream msg;
    msg << "API\0";

    // Minimum version we support
    std::string versionStr = "v100..176";
    uint32_t len = static_cast<uint32_t>(versionStr.size());

    msg.write(reinterpret_cast<const char*>(&len), sizeof(len));
    msg << versionStr;

    // Start API message: msg_id=71, version=2, clientId
    msg << "71" << '\0' << "2" << '\0' << std::to_string(clientId) << '\0';

    return msg.str();
}

std::string IBGateway::buildPlaceOrderMessage(const IBOrder& order)
{
    // TWS API Place Order message (msg_id=3)
    std::ostringstream msg;
    msg << "3" << '\0';           // PLACE_ORDER msg id
    msg << "45" << '\0';          // Version
    msg << order.orderId << '\0'; // Order ID

    // Contract fields
    msg << "0" << '\0';           // Contract ID (0 = resolve by symbol)
    msg << order.symbol << '\0';  // Symbol
    msg << "FUT" << '\0';         // Security type (futures)
    msg << "" << '\0';            // Last trade date
    msg << "0" << '\0';           // Strike
    msg << "" << '\0';            // Right
    msg << "" << '\0';            // Multiplier
    msg << "CME" << '\0';         // Exchange
    msg << "" << '\0';            // Primary exchange
    msg << "USD" << '\0';         // Currency
    msg << "" << '\0';            // Local symbol
    msg << "" << '\0';            // Trading class

    // Order fields
    msg << (order.direction == OrderDirection::BUY ? "BUY" : "SELL") << '\0';
    msg << order.quantity << '\0';

    switch (order.orderType) {
        case OrderType::MARKET:
            msg << "MKT" << '\0';
            break;
        case OrderType::LIMIT:
            msg << "LMT" << '\0';
            break;
        case OrderType::STOP:
            msg << "STP" << '\0';
            break;
        case OrderType::STOP_LIMIT:
            msg << "STP LMT" << '\0';
            break;
    }

    msg << order.limitPrice << '\0';  // Limit price
    msg << order.stopPrice << '\0';   // Aux price (stop)

    // Time in force
    msg << "DAY" << '\0';

    return msg.str();
}

std::string IBGateway::buildCancelOrderMessage(int orderId)
{
    // TWS API Cancel Order message (msg_id=4)
    std::ostringstream msg;
    msg << "4" << '\0';
    msg << "1" << '\0';
    msg << orderId << '\0';
    return msg.str();
}

std::string IBGateway::buildRequestPositionsMessage()
{
    // TWS API Request Positions (msg_id=61)
    std::ostringstream msg;
    msg << "61" << '\0';
    msg << "1" << '\0';
    return msg.str();
}

std::string IBGateway::buildRequestAccountMessage()
{
    // TWS API Request Account Summary (msg_id=62)
    std::ostringstream msg;
    msg << "62" << '\0';
    msg << "1" << '\0';
    msg << "1" << '\0';       // Request ID
    msg << "All" << '\0';     // Group
    msg << "NetLiquidation" << '\0';  // Tags
    return msg.str();
}

void IBGateway::parseResponse(const std::string& response)
{
    // Parse null-delimited TWS API messages
    // First field is typically the message type ID
    if (response.empty()) return;

    std::vector<std::string> fields;
    std::istringstream stream(response);
    std::string field;
    while (std::getline(stream, field, '\0')) {
        if (!field.empty()) {
            fields.push_back(field);
        }
    }

    if (fields.empty()) return;

    // Parse based on message type
    int msgType = 0;
    try {
        msgType = std::stoi(fields[0]);
    } catch (...) {
        return;
    }

    switch (msgType) {
        case 9:   // Next Valid ID
            if (fields.size() > 2) {
                nextOrderId_ = std::stoi(fields[2]);
            }
            break;
        case 3:   // Order Status
            handleOrderStatus(response);
            break;
        case 11:  // Execution Details
            handleExecutionDetails(response);
            break;
        case 61:  // Position
            handlePositionUpdate(response);
            break;
        default:
            break;
    }
}

void IBGateway::handleOrderStatus(const std::string& message)
{
    // Parse order status: orderId, status, filled, remaining, avgFillPrice, ...
    std::vector<std::string> fields;
    std::istringstream stream(message);
    std::string field;
    while (std::getline(stream, field, '\0')) {
        fields.push_back(field);
    }

    if (fields.size() < 6) return;

    try {
        int orderId = std::stoi(fields[1]);
        std::string statusStr = fields[2];

        std::lock_guard<std::mutex> lock(orderMutex_);
        if (orders_.count(orderId)) {
            auto& order = orders_[orderId];
            long long now = currentTimeNs();

            if (statusStr == "Filled") {
                order.status = OrderStatus::FILLED;
                order.fillPrice = std::stod(fields[5]);
                order.fillTimeNs = now;
                order.roundTripLatencyNs = now - order.submitTimeNs;

                if (orderCallback_) {
                    orderCallback_(order);
                }
            } else if (statusStr == "Submitted") {
                order.status = OrderStatus::SUBMITTED;
                order.ackTimeNs = now;
            } else if (statusStr == "Cancelled") {
                order.status = OrderStatus::CANCELLED;
            }
        }
    } catch (...) {
        // Parse error, skip
    }
}

void IBGateway::handleExecutionDetails(const std::string& message)
{
    // Parse execution: orderId, execId, time, acct, exchange, side, shares, price, ...
    std::vector<std::string> fields;
    std::istringstream stream(message);
    std::string field;
    while (std::getline(stream, field, '\0')) {
        fields.push_back(field);
    }

    if (fields.size() < 10) return;

    try {
        int orderId = std::stoi(fields[1]);

        std::lock_guard<std::mutex> lock(orderMutex_);
        if (orders_.count(orderId)) {
            auto& order = orders_[orderId];
            order.fillPrice = std::stod(fields[8]);
            order.fillTime = fields[4];
            order.status = OrderStatus::FILLED;
            order.fillTimeNs = currentTimeNs();
            order.roundTripLatencyNs = order.fillTimeNs - order.submitTimeNs;

            // Update position
            {
                std::lock_guard<std::mutex> posLock(positionMutex_);
                auto& pos = positions_[order.symbol];
                pos.symbol = order.symbol;
                if (order.direction == OrderDirection::BUY) {
                    pos.quantity += order.quantity;
                } else {
                    pos.quantity -= order.quantity;
                }
                pos.avgCost = order.fillPrice;
            }
        }
    } catch (...) {
        // Parse error, skip
    }
}

void IBGateway::handlePositionUpdate(const std::string& message)
{
    std::vector<std::string> fields;
    std::istringstream stream(message);
    std::string field;
    while (std::getline(stream, field, '\0')) {
        fields.push_back(field);
    }

    if (fields.size() < 6) return;

    try {
        std::string symbol = fields[2];
        int qty = std::stoi(fields[3]);
        double avgCost = std::stod(fields[4]);

        std::lock_guard<std::mutex> lock(positionMutex_);
        auto& pos = positions_[symbol];
        pos.symbol = symbol;
        pos.quantity = qty;
        pos.avgCost = avgCost;
    } catch (...) {
        // Parse error, skip
    }
}

long long IBGateway::currentTimeNs()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}
