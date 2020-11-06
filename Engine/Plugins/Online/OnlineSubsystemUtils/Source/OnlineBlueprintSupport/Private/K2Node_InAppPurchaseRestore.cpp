// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_InAppPurchaseRestore.h"
#include "InAppPurchaseRestoreCallbackProxy.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_InAppPurchaseRestore::UK2Node_InAppPurchaseRestore(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UInAppPurchaseRestoreCallbackProxy, CreateProxyObjectForInAppPurchaseRestore);
	ProxyFactoryClass = UInAppPurchaseRestoreCallbackProxy::StaticClass();

	ProxyClass = UInAppPurchaseRestoreCallbackProxy::StaticClass();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#undef LOCTEXT_NAMESPACE
