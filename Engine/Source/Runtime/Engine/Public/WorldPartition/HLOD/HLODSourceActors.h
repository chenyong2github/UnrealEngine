// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HLODSourceActors.generated.h"


class ULevelStreaming;
class UHLODLayer;


UCLASS(Abstract)
class ENGINE_API UWorldPartitionHLODSourceActors : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual ULevelStreaming* LoadSourceActors(bool& bOutDirty) const PURE_VIRTUAL(UWorldPartitionHLODSourceActors::LoadSourceActors, return nullptr; );
	virtual uint32 GetHLODHash() const;

	void SetHLODLayer(const UHLODLayer* HLODLayer);
	const UHLODLayer* GetHLODLayer() const;
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<const UHLODLayer> HLODLayer;
#endif
};
