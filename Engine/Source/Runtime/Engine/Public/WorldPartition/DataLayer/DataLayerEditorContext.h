// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

#if WITH_EDITOR

struct FActorDataLayer;
class UWorld;

/**
 * FDataLayerEditorContext
 */

struct ENGINE_API FDataLayerEditorContext
{
public:
	static const uint32 EmptyHash = 0;
	FDataLayerEditorContext() : Hash(FDataLayerEditorContext::EmptyHash) {}
	FDataLayerEditorContext(UWorld* InWorld, const TArray<FName>& InDataLayers);
	FORCEINLINE bool IsEmpty() const { return (Hash == FDataLayerEditorContext::EmptyHash) && DataLayers.IsEmpty(); }
	FORCEINLINE uint32 GetHash() const { return Hash; }
	FORCEINLINE const TArray<FName>& GetDataLayers() const { return DataLayers; }
private:
	uint32 Hash;
	TArray<FName> DataLayers;
};

/**
 * FScopeChangeDataLayerEditorContext
 */

struct ENGINE_API FScopeChangeDataLayerEditorContext
{
	FScopeChangeDataLayerEditorContext(UWorld* InWorld, const FDataLayerEditorContext& InContext);
	FScopeChangeDataLayerEditorContext(UWorld* InWorld, const FActorDataLayer& InContextDataLayer);
	~FScopeChangeDataLayerEditorContext();
private:
	void Initialize(const FDataLayerEditorContext& InContext);

	TWeakObjectPtr<UWorld> World;
	FDataLayerEditorContext OldContext;
};
#endif