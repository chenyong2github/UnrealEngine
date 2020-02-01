// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_InAppPurchase2.h"
#include "InAppPurchaseCallbackProxy2.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_InAppPurchase2::UK2Node_InAppPurchase2(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UInAppPurchaseCallbackProxy2, CreateProxyObjectForInAppPurchase);
	ProxyFactoryClass = UInAppPurchaseCallbackProxy2::StaticClass();

	ProxyClass = UInAppPurchaseCallbackProxy2::StaticClass();
}

#undef LOCTEXT_NAMESPACE
