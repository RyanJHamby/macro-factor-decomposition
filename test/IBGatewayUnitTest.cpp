//
//  IBGatewayUnitTest.cpp
//  InvertedYieldCurveTrader
//
//  Unit tests for IBGateway (mocked connection)
//
//  Created by Ryan Hamby on 1/15/26.
//

#include <gtest/gtest.h>
#include "../src/DataProviders/IBGateway.hpp"

class IBGatewayTest : public ::testing::Test {};

TEST_F(IBGatewayTest, InitialStateDisconnected) {
    IBGateway gw;
    EXPECT_FALSE(gw.isConnected());
}

TEST_F(IBGatewayTest, PlaceOrderFailsWhenDisconnected) {
    IBGateway gw;
    int orderId = gw.placeOrder("ES", OrderDirection::BUY, 1);
    EXPECT_EQ(orderId, -1);
}

TEST_F(IBGatewayTest, CancelOrderFailsWhenDisconnected) {
    IBGateway gw;
    EXPECT_FALSE(gw.cancelOrder(1));
}

TEST_F(IBGatewayTest, GetPositionReturnsEmptyWhenDisconnected) {
    IBGateway gw;
    IBPosition pos = gw.getPosition("ES");
    EXPECT_EQ(pos.quantity, 0);
    EXPECT_EQ(pos.symbol, "ES");
}

TEST_F(IBGatewayTest, GetAccountBalanceZeroWhenDisconnected) {
    IBGateway gw;
    EXPECT_NEAR(gw.getAccountBalance(), 0.0, 0.01);
}

TEST_F(IBGatewayTest, GetOrderReturnsErrorForInvalidId) {
    IBGateway gw;
    IBOrder order = gw.getOrder(999);
    EXPECT_EQ(order.orderId, -1);
    EXPECT_EQ(order.status, OrderStatus::ERROR);
}

TEST_F(IBGatewayTest, GetAllOrdersEmptyInitially) {
    IBGateway gw;
    auto orders = gw.getAllOrders();
    EXPECT_TRUE(orders.empty());
}

TEST_F(IBGatewayTest, GetAllPositionsEmptyInitially) {
    IBGateway gw;
    auto positions = gw.getAllPositions();
    EXPECT_TRUE(positions.empty());
}

TEST_F(IBGatewayTest, AverageLatencyZeroInitially) {
    IBGateway gw;
    EXPECT_EQ(gw.getAverageLatencyNs(), 0);
}

TEST_F(IBGatewayTest, MarketDataReturnsEmptyWhenDisconnected) {
    IBGateway gw;
    MarketData data = gw.requestMarketData("ES");
    EXPECT_EQ(data.symbol, "ES");
    EXPECT_NEAR(data.bid, 0.0, 0.01);
    EXPECT_NEAR(data.ask, 0.0, 0.01);
}

TEST_F(IBGatewayTest, ConnectToInvalidHostFails) {
    IBGateway gw;
    // This should fail (no TWS running on this port)
    bool connected = gw.connect("127.0.0.1", 19999, 1);
    EXPECT_FALSE(connected);
    EXPECT_FALSE(gw.isConnected());
}

TEST_F(IBGatewayTest, OrderCallbackCanBeSet) {
    IBGateway gw;
    bool called = false;
    gw.setOrderCallback([&called](const IBOrder& order) {
        called = true;
    });
    // Callback won't be called without connection, but setting it shouldn't crash
    EXPECT_FALSE(called);
}
