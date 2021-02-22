// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

struct FObjectCacheEventSink
{
	static void NotifyUsedMaterialsChanged_Concurrent(const UPrimitiveComponent* PrimitiveComponent, const TArray<UMaterialInterface*>& UsedMaterials);
	static void NotifyRenderStateChanged_Concurrent(UActorComponent*);
	static void NotifyReferencedTextureChanged_Concurrent(UMaterialInterface*);
	static void NotifyStaticMeshChanged_Concurrent(UStaticMeshComponent*);
	static void NotifyMaterialDestroyed_Concurrent(UMaterialInterface*);
};

#endif // #if WITH_EDITOR