// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "LevelInstance/LevelInstanceTypes.h"
#include "LevelInstanceEditorPivotActor.generated.h"

class ALevelInstance;
class ULevelStreaming;

/**
 * 
 */
UCLASS(transient, notplaceable, hidecategories=(Rendering,Replication,Collision,Partition,Input,HLOD,Actor,LOD,Cooking))
class ENGINE_API ALevelInstancePivot : public AActor
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	static ALevelInstancePivot* Create(ALevelInstance* LevelInstanceActor, ULevelStreaming* LevelStreaming);
	
	virtual bool CanDeleteSelectedActor(FText& OutReason) const override { return false; }
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

	void SetPivot(ELevelInstancePivotType PivotType, AActor* PivotActor = nullptr);
private:
	void SetLevelInstanceID(const FLevelInstanceID& InLevelInstanceID) { LevelInstanceID = InLevelInstanceID; }
	const FLevelInstanceID& GetLevelInstanceID() const { return LevelInstanceID; }
	void UpdateOffset();
#endif

private:
#if WITH_EDITORONLY_DATA
	FLevelInstanceID LevelInstanceID;
	FVector SpawnOffset;
	FVector OriginalPivotOffset;
#endif
};