// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "LevelInstance/Packed/PackedLevelInstanceTypes.h"
#include "LevelInstance/Packed/ILevelInstancePacker.h"
#include "UObject/SoftObjectPtr.h"
#include "Containers/Set.h"

class APackedLevelInstance;
class ALevelInstance;
class AActor;
class UActorComponent;
class UBlueprint;
class FMessageLog;

/**
 * FPackedLevelInstanceBuiler handles packing of ALevelInstance actors into APackedLevelInstance actors and Blueprints.
 */
class ENGINE_API FPackedLevelInstanceBuilder
{
public:
	FPackedLevelInstanceBuilder();
	
	static TSharedPtr<FPackedLevelInstanceBuilder> CreateDefaultBuilder();
	
	/* Packs InPackedLevelInstance using InLevelInstanceToPack as its source level actor */
	bool PackActor(APackedLevelInstance* InPackedLevelInstance, ALevelInstance* InLevelInstanceToPack);
	/* Packs InPackedLevelInstance using itself as the source level actor */
	bool PackActor(APackedLevelInstance* InPackedLevelInstance);
	/* Packs InPackedLevelInstance using InWorldAsset as the source level */
	bool PackActor(APackedLevelInstance* InPackedLevelInstance, TSoftObjectPtr<UWorld> InWorldAsset);

	/* Creates/Updates a APackedLevelInstance Blueprint from InLevelInstance (will overwrite existing asset or show a dialog if InBlueprintAsset is null) */
	bool CreateOrUpdateBlueprint(ALevelInstance* InLevelInstance, TSoftObjectPtr<UBlueprint> InBlueprintAsset, bool bCheckoutAndSave = true, bool bPromptForSave = true);
	/* Creates/Updates a APackedLeveInstance Blueprint from InWorldAsset (will overwrite existing asset or show a dialog if InBlueprintAsset is null) */
	bool CreateOrUpdateBlueprint(TSoftObjectPtr<UWorld> InWorldAsset, TSoftObjectPtr<UBlueprint> InBlueprintAsset, bool bCheckoutAndSave = true, bool bPromptForSave = true);
	/* Update existing Blueprint */
	void UpdateBlueprint(UBlueprint* Blueprint, bool bCheckoutAndSave = true, bool bPromptForSave = true);

	static const FString& GetPackedBPPrefix();
	/* Creates a new APackedLevelInstance Blueprint using InPackagePath/InAssetName as hint for path. Prompts the user to input the final asset name. */
	static UBlueprint* CreatePackedLevelInstanceBlueprintWithDialog(TSoftObjectPtr<UBlueprint> InBlueprintAsset, TSoftObjectPtr<UWorld> InWorldAsset, bool bInCompile);
	/* Creates a new APackedLevelInstance Blueprint using InPackagePath/InAssetName */
	static UBlueprint* CreatePackedLevelInstanceBlueprint(TSoftObjectPtr<UBlueprint> InBlueprintAsset, TSoftObjectPtr<UWorld> InWorldAsset, bool bInCompile);
	
private:
	/* Create/Updates a APackedLevelInstance Blueprint from InPackedActor (will overwrite existing asset or show a dialog if InBlueprintAsset is null) */
	bool CreateOrUpdateBlueprintFromPacked(APackedLevelInstance* InPackedActor, TSoftObjectPtr<UBlueprint> InBlueprintAsset, bool bCheckoutAndSave, bool bPromptForSave);
	/* Creates/Updates a APackedLevelInstance Blueprint from InLevelInstance (will overwrite existing asset or show a dialog if InBlueprintAsset is null) */
	bool CreateOrUpdateBlueprintFromUnpacked(ALevelInstance* InPackedActor, TSoftObjectPtr<UBlueprint> InBlueprintAsset, bool bCheckoutAndSave, bool bPromptForSave);
	/* Creates and loads a ALevelInstance so it can be used for packing */
	ALevelInstance* CreateTransientLevelInstanceForPacking(TSoftObjectPtr<UWorld> InWorldAsset, const FVector& InLocation, const FRotator& InRotator);

	FPackedLevelInstanceBuilder(const FPackedLevelInstanceBuilder&) = delete;
	FPackedLevelInstanceBuilder& operator=(const FPackedLevelInstanceBuilder&) = delete;

public:
	friend class FPackedLevelInstanceBuilderContext;
		
private:
	
	TSet<UClass*> ClassDiscards;
	TMap<FLevelInstancePackerID, TUniquePtr<ILevelInstancePacker>> Packers;
};


class FPackedLevelInstanceBuilderContext
{
public:
	FPackedLevelInstanceBuilderContext(const FPackedLevelInstanceBuilder& InBuilder, APackedLevelInstance* InPackedLevelInstance) 
		: Packers(InBuilder.Packers), ClassDiscards(InBuilder.ClassDiscards), PackedLevelInstance(InPackedLevelInstance), LevelTransform(FVector::ZeroVector), PivotOffset(FVector::ZeroVector) {}

	/* Interface for ILevelInstancePacker's to use */
	void ClusterLevelActor(AActor* InLevelActor);
	void FindOrAddCluster(FLevelInstancePackerClusterID&& InClusterID, UActorComponent* InComponent = nullptr);
	void DiscardActor(AActor* InActor);
	void Report(FMessageLog& LevelInstanceLog) const;

	const TMap<FLevelInstancePackerClusterID, TArray<UActorComponent*>>& GetClusters() const { return Clusters; }

	void SetLevelTransform(const FTransform& InLevelTransform) { LevelTransform = InLevelTransform; }
	void SetPivotOffset(const FVector& InPivotOffset) { PivotOffset = InPivotOffset; }
	const FTransform& GetLevelTransform() const { return LevelTransform; }
	const FVector GetPivotOffset() const { return PivotOffset; }

	bool ShouldPackComponent(UActorComponent* InActorComponent) const;
private:
	const TMap<FLevelInstancePackerID, TUniquePtr<ILevelInstancePacker>>& Packers;
	const TSet<UClass*>& ClassDiscards;

	APackedLevelInstance* PackedLevelInstance;
	
	TMap<FLevelInstancePackerClusterID, TArray<UActorComponent*>> Clusters;

	TMap<AActor*, TSet<UActorComponent*>> PerActorClusteredComponents;
	TSet<AActor*> ActorDiscards;

	FTransform LevelTransform;
	FVector PivotOffset;
};

#endif