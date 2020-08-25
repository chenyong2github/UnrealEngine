// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectAnnotation.h"
#include "Commandlets/Commandlet.h"
#include "WorldPartitionPreCookCommandlet.generated.h"

UCLASS()
class UWorldPartitionPreCookCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	void OnLevelInstanceActorPostLoad(class ALevelInstance* LevelInstanceActor);
	bool PreCookLevelAndSave(ULevel* InLevel);
	bool SaveLevel(ULevel* InLevel);
	void RemoveSubLevel(ULevel* InLevel);
	ULevel* LoadLevel(const FString& InLevelName);
	ULevel* LoadSubLevel(const FString& InLevelName);

	UWorld* MainWorld;
	TSet<FString> PartitionedWorldsToGenerate;
};