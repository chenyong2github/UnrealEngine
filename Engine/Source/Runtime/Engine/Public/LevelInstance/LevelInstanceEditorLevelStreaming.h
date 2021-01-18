// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "LevelInstance/LevelInstanceTypes.h"
#include "LevelInstanceEditorLevelStreaming.generated.h"

class ALevelInstance;

UCLASS(Transient, MinimalAPI)
class ULevelStreamingLevelInstanceEditor : public ULevelStreamingAlwaysLoaded
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual bool ShowInLevelCollection() const override { return false; }
	ALevelInstance* GetLevelInstanceActor() const;
	FBox GetBounds() const;

protected:
	void OnLevelActorAdded(AActor* InActor);

	friend class ULevelInstanceSubsystem;

	static ULevelStreamingLevelInstanceEditor* Load(ALevelInstance* LevelInstanceActor);
	static void Unload(ULevelStreamingLevelInstanceEditor* LevelStreaming);

	virtual void SetLoadedLevel(ULevel* Level) override;
private:
	FLevelInstanceID LevelInstanceID;
#endif
};