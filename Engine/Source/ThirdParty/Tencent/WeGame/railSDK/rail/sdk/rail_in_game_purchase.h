// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_IN_GAME_PURCHASE_H
#define RAIL_SDK_RAIL_IN_GAME_PURCHASE_H

#include "rail/sdk/rail_in_game_purchase_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailInGamePurchase {
  public:
    // trigger event RequestAllPurchasableProductsResponse
    virtual RailResult AsyncRequestAllPurchasableProducts(const RailString& user_data) = 0;

    // trigger event RequestAllProductsResponse
    virtual RailResult AsyncRequestAllProducts(const RailString& user_data) = 0;

    virtual RailResult GetProductInfo(RailProductID product_id,
                        RailPurchaseProductInfo* product) = 0;

    // set user_data to callback event
    virtual RailResult AsyncPurchaseProducts(const RailArray<RailProductItem>& cart_items,
                        const RailString& user_data) = 0;

    virtual RailResult AsyncFinishOrder(const RailString& order_id,
                        const RailString& user_data) = 0;

    // set user_data to callback event
    // auto delivery purchased products to assets
    virtual RailResult AsyncPurchaseProductsToAssets(const RailArray<RailProductItem>& cart_items,
                        const RailString& user_data) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_IN_GAME_PURCHASE_H
