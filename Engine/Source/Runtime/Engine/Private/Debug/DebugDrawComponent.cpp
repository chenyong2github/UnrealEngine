// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/DebugDrawComponent.h"

FPrimitiveSceneProxy* UDebugDrawComponent::CreateSceneProxy()
{
	FDebugRenderSceneProxy* Proxy = CreateDebugSceneProxy();
#if UE_ENABLE_DEBUG_DRAWING
  	if (Proxy != nullptr)
	{
		GetDebugDrawDelegateHelper().InitDelegateHelper(Proxy);
	}

	GetDebugDrawDelegateHelper().ProcessDeferredRegister();
#endif
	return Proxy;
}

#if UE_ENABLE_DEBUG_DRAWING
  void UDebugDrawComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	GetDebugDrawDelegateHelper().RequestRegisterDebugDrawDelegate(Context);
}

void UDebugDrawComponent::DestroyRenderState_Concurrent()
{
	GetDebugDrawDelegateHelper().UnregisterDebugDrawDelegate();
	Super::DestroyRenderState_Concurrent();
}
#endif
