// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/LevelStreamingDynamic.h"
#include "LevelInstance/LevelInstanceTypes.h"
#include "LevelInstanceLevelStreaming.generated.h"

class ILevelInstanceInterface;

UCLASS(Transient)
class ENGINE_API ULevelStreamingLevelInstance : public ULevelStreamingDynamic
{
	GENERATED_UCLASS_BODY()

public:
	ILevelInstanceInterface* GetLevelInstance() const;

#if WITH_EDITOR
	virtual bool ShowInLevelCollection() const override { return false; }
	FBox GetBounds() const;

	virtual TOptional<FFolder::FRootObject> GetFolderRootObject() const override;
#endif
	
protected:
	static ULevelStreamingLevelInstance* LoadInstance(ILevelInstanceInterface* LevelInstanceActor);
	static void UnloadInstance(ULevelStreamingLevelInstance* LevelStreaming);

	virtual void OnLevelLoadedChanged(ULevel* Level) override;

	friend class ULevelInstanceSubsystem;

private:
	FLevelInstanceID LevelInstanceID;
};