// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LevelStreamingDynamic.h"
#include "LevelInstance/LevelInstanceTypes.h"
#include "LevelInstanceLevelStreaming.generated.h"

class ALevelInstance;

UCLASS(Transient)
class ENGINE_API ULevelStreamingLevelInstance : public ULevelStreamingDynamic
{
	GENERATED_UCLASS_BODY()

public:
	ALevelInstance* GetLevelInstanceActor() const;

#if WITH_EDITOR
	virtual bool ShowInLevelCollection() const override { return false; }
	FBox GetBounds() const;
#endif
	
protected:
	static ULevelStreamingLevelInstance* LoadInstance(ALevelInstance* LevelInstanceActor);
	static void UnloadInstance(ULevelStreamingLevelInstance* LevelStreaming);

	virtual void SetLoadedLevel(ULevel* Level) override;

	friend class ULevelInstanceSubsystem;

private:
	FLevelInstanceID LevelInstanceID;
};