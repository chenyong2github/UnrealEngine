// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LateUpdateManager.h"
#include "PrimitiveSceneProxy.h"
#include "Components/PrimitiveComponent.h"
#include "PrimitiveSceneInfo.h"
#include "HeadMountedDisplayTypes.h"

FLateUpdateManager::FLateUpdateManager()
{
}

void FLateUpdateManager::Setup(const FTransform& ParentToWorld, USceneComponent* Component, bool bSkipLateUpdate)
{
	check(IsInGameThread());

	FScopeLock Lock(&StateCriticalSection);

	GameThreadState.ParentToWorld = ParentToWorld;
	GatherLateUpdatePrimitives(Component);
	GameThreadState.bSkip = bSkipLateUpdate;
	++GameThreadState.TrackingNumber;
}

void FLateUpdateManager::Apply_RenderThread(FSceneInterface* Scene, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform)
{
	check(IsInRenderingThread());

	{
		// Only grab the lock to protect access to the GameThreadState collection - the RenderThreadState is
		// only ever accessed from the rendering thread.
		FScopeLock Lock(&StateCriticalSection);
		RenderThreadState.bSkip = GameThreadState.bSkip;
		RenderThreadState.ParentToWorld = GameThreadState.ParentToWorld;
		RenderThreadState.Primitives = MoveTemp(GameThreadState.Primitives);

		GameThreadState.Primitives.Reset();

		RenderThreadState.TrackingNumber = GameThreadState.TrackingNumber;
	}

	if (!RenderThreadState.Primitives.Num() || RenderThreadState.bSkip)
	{
		return;
	}

	const FTransform OldCameraTransform = OldRelativeTransform * RenderThreadState.ParentToWorld;
	const FTransform NewCameraTransform = NewRelativeTransform * RenderThreadState.ParentToWorld;
	const FMatrix LateUpdateTransform = (OldCameraTransform.Inverse() * NewCameraTransform).ToMatrixWithScale();

	bool bIndicesHaveChanged = false;

	// Apply delta to the cached scene proxies
	// Also check whether any primitive indices have changed, in case the scene has been modified in the meantime.
	for (auto PrimitivePair : RenderThreadState.Primitives)
	{
		FPrimitiveSceneInfo* RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(PrimitivePair.Value);
		FPrimitiveSceneInfo* CachedSceneInfo = PrimitivePair.Key;

		// If the retrieved scene info is different than our cached scene info then the scene has changed in the meantime
		// and we need to search through the entire scene to make sure it still exists.
		if (CachedSceneInfo != RetrievedSceneInfo)
		{
			bIndicesHaveChanged = true;
			break; // No need to continue here, as we are going to brute force the scene primitives below anyway.
		}
		else if (CachedSceneInfo->Proxy)
		{
			CachedSceneInfo->Proxy->ApplyLateUpdateTransform(LateUpdateTransform);
			PrimitivePair.Value = -1; // Set the cached index to -1 to indicate that this primitive was already processed
		}
	}

	// Indices have changed, so we need to scan the entire scene for primitives that might still exist
	if (bIndicesHaveChanged)
	{
		int32 Index = 0;
		FPrimitiveSceneInfo* RetrievedSceneInfo;
		RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(Index++);
		while(RetrievedSceneInfo)
		{
			if (RetrievedSceneInfo->Proxy && RenderThreadState.Primitives.Contains(RetrievedSceneInfo) && RenderThreadState.Primitives[RetrievedSceneInfo] >= 0)
			{
				RetrievedSceneInfo->Proxy->ApplyLateUpdateTransform(LateUpdateTransform);
			}
			RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(Index++);
		}
	}
}

void FLateUpdateManager::CacheSceneInfo(USceneComponent* Component)
{
	// If a scene proxy is present, cache it
	UPrimitiveComponent* PrimitiveComponent = dynamic_cast<UPrimitiveComponent*>(Component);
	if (PrimitiveComponent && PrimitiveComponent->SceneProxy)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveComponent->SceneProxy->GetPrimitiveSceneInfo();
		if (PrimitiveSceneInfo && PrimitiveSceneInfo->IsIndexValid())
		{
			GameThreadState.Primitives.Emplace(PrimitiveSceneInfo, PrimitiveSceneInfo->GetIndex());
		}
	}
}

void FLateUpdateManager::GatherLateUpdatePrimitives(USceneComponent* ParentComponent)
{
	CacheSceneInfo(ParentComponent);

	TArray<USceneComponent*> Components;
	ParentComponent->GetChildrenComponents(true, Components);
	for(USceneComponent* Component : Components)
	{
		if (Component != nullptr)
		{
			CacheSceneInfo(Component);
		}
	}
}
