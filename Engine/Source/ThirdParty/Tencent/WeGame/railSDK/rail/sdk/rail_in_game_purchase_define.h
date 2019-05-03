// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_IN_GAME_PRUCHASE_DEFINE_H
#define RAIL_SDK_RAIL_IN_GAME_PRUCHASE_DEFINE_H

#include "rail/sdk/rail_assets_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

// define product id, [1, 1000000000] is used for game self
// like in-game-purchase, assert and so on
enum EnumRailProductId {
    EnumRailProductId_For_Game_Start = 1,
    EnumRailProductId_For_Game_End = 1000000000,

    EnumRailProductId_For_Platfrom_Start = 1000000001,
    EnumRailProductId_For_Platfrom_Storage_Space = 1000000001,
    EnumRailProductId_For_Platfrom_All = 1000000011
};

// in game purchase products discount type
enum PurchaseProductDiscountType {
    kPurchaseProductDiscountTypeInvalid = 0,
    kPurchaseProductDiscountTypeNone = 1,       // 没有折扣
    kPurchaseProductDiscountTypePermanent = 2,  // 永久折扣
    kPurchaseProductDiscountTypeTimed = 3,      // 限时折扣
};

// in game purchase order state
enum PurchaseProductOrderState {
    kPurchaseProductOrderStateInvalid = 0,
    kPurchaseProductOrderStateCreateOrderOk = 100,  // 下单成功
    kPurchaseProductOrderStatePayOk = 200,          // 支付成功
    kPurchaseProductOrderStateDeliverOk = 300,      // 发货成功
};

struct RailDiscountInfo {
    RailDiscountInfo() {
        off = 0;
        type = kPurchaseProductDiscountTypeNone;
        discount_price = 0.0;
        start_time = 0;
        end_time = 0;
    }

    float off;                         // 折扣率，[0~1.0)之间:
                                       //        0.15 - 15%off - 8.5折
                                       //        0.20 - 20%off - 8折
    float discount_price;              // 折扣后的价格,后台根据off值自动计算出来的

    PurchaseProductDiscountType type;  // 折扣类型
    uint32_t start_time;               // 限时折扣开始时间，只对限时折扣类型有效
    uint32_t end_time;                 // 限时折扣结束时间，只对限时折扣类型有效
};

// product info
// 道具信息
struct RailPurchaseProductExtraInfo {
    RailPurchaseProductExtraInfo() {}

    RailString exchange_rule;      // 道具的合成规则
    RailString bundle_rule;        // 道具的打包规则
};

struct RailPurchaseProductInfo {
    RailPurchaseProductInfo() {
        product_id = 0;
        is_purchasable = false;
        original_price = 0.0;
    }

    RailProductID product_id;      // 道具ID
    bool is_purchasable;           // 道具是否可以购买
    RailString name;               // 道具名称
    RailString description;        // 道具描述
    RailString category;           // 道具类别
    RailString product_thumbnail;  // 道具图片url
    RailPurchaseProductExtraInfo extra_info;  // 道具附加信息
    // 当is_purchasable=true的时候，下面三个属性有效
    float original_price;          // 道具原价
    RailString currency_type;      // 货币种类
    RailDiscountInfo discount;     // 折扣信息
};

namespace rail_event {

struct RailInGamePurchaseRequestAllPurchasableProductsResponse
    : RailEvent<kRailEventInGamePurchaseAllPurchasableProductsInfoReceived> {
    RailInGamePurchaseRequestAllPurchasableProductsResponse() { result = kFailure; }

    RailArray<RailPurchaseProductInfo> purchasable_products;  // 获取成功时有效，否则为空
};

struct RailInGamePurchaseRequestAllProductsResponse
    : RailEvent<kRailEventInGamePurchaseAllProductsInfoReceived> {
    RailInGamePurchaseRequestAllProductsResponse() { result = kFailure; }

        RailArray<RailPurchaseProductInfo> all_products;  // 获取成功时有效，否则为空
};

struct RailInGamePurchasePurchaseProductsResponse
    : RailEvent<kRailEventInGamePurchasePurchaseProductsResult> {
    RailInGamePurchasePurchaseProductsResponse() {
        result = kFailure;
        user_data = "";
    }

    RailString order_id;
    RailArray<RailProductItem> delivered_products;  // 发货成功时有效，记录每个物品的发货数量
};

struct RailInGamePurchasePurchaseProductsToAssetsResponse
    : RailEvent<kRailEventInGamePurchasePurchaseProductsToAssetsResult> {
    RailInGamePurchasePurchaseProductsToAssetsResponse() {
        result = kFailure;
        user_data = "";
    }

    RailString order_id;
    RailArray<RailAssetInfo> delivered_assets;  // 发货成功时有效，记录每个物品的发货数量,id
};

struct RailInGamePurchaseFinishOrderResponse :
    RailEvent<kRailEventInGamePurchaseFinishOrderResult> {
    RailInGamePurchaseFinishOrderResponse() {
        result = kFailure;
    }

    RailString order_id;
};

}  // namespace rail_event

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_IN_GAME_PRUCHASE_DEFINE_H
