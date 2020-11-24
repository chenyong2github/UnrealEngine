// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LevelSnapshotFilters.generated.h"

// If you are building your filter in C++ then you should inherit this class
UCLASS(Abstract)
class LEVELSNAPSHOTFILTERS_API ULevelSnapshotFilter : public UObject
{
	GENERATED_BODY()
	
public:

	virtual bool IsActorValid(const FName ActorName, const UClass* ActorClass) const;

	virtual bool IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const;
};

// If you are building your filter in Blueprints then you should inherit this class
UCLASS(Abstract, Blueprintable)
class LEVELSNAPSHOTFILTERS_API ULevelSnapshotBlueprintFilter : public ULevelSnapshotFilter
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintNativeEvent, Category = "Snapshots")
	bool IsActorValid(const FName ActorName, const UClass* ActorClass) const override;

	UFUNCTION(BlueprintNativeEvent, Category = "Snapshots")
	bool IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const override;
};