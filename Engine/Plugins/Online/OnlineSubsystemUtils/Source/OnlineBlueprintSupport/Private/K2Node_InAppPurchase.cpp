// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_InAppPurchase.h"
#include "InAppPurchaseCallbackProxy.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_InAppPurchase::UK2Node_InAppPurchase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UInAppPurchaseCallbackProxy, CreateProxyObjectForInAppPurchase);
	ProxyFactoryClass = UInAppPurchaseCallbackProxy::StaticClass();

	ProxyClass = UInAppPurchaseCallbackProxy::StaticClass();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#undef LOCTEXT_NAMESPACE
