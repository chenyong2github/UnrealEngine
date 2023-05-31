// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/HLOD/HLODSourceActors.h"
#include "HLODSourceActorsFromLevel.generated.h"


UCLASS()
class ENGINE_API UWorldPartitionHLODSourceActorsFromLevel : public UWorldPartitionHLODSourceActors
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual ULevelStreaming* LoadSourceActors(bool& bOutDirty) const override;
	virtual uint32 GetHLODHash() const override;

	void SetSourceLevel(const UWorld* InSourceLevel);
	const TSoftObjectPtr<UWorld>& GetSourceLevel() const;
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TSoftObjectPtr<UWorld> SourceLevel;
#endif
};
