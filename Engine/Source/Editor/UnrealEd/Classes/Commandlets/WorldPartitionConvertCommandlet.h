// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectAnnotation.h"
#include "Commandlets/Commandlet.h"
#include "PackageSourceControlHelper.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartitionConvertCommandlet.generated.h"

UNREALED_API DECLARE_LOG_CATEGORY_EXTERN(LogWorldPartitionConvertCommandlet, Log, All);

class UWorldPartition;
class UWorldComposition;

USTRUCT()
struct FHLODLayerActorMapping
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY()
	FString HLODLayer;
};

UCLASS(Config=Engine)
class UNREALED_API UWorldPartitionConvertCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	void GatherAndPrepareSubLevelsToConvert(const UWorldPartition* WorldPartition, ULevel* Level, TArray<ULevel*>& SubLevels);
	EActorGridPlacement GetLevelGridPlacement(ULevel* Level, EActorGridPlacement DefaultGridPlacement);

protected:
	virtual bool GetAdditionalLevelsToConvert(ULevel* Level, TArray<ULevel*>& SubLevels);
	virtual bool PrepareStreamingLevelForConversion(const UWorldPartition* WorldPartition, ULevelStreaming* StreamingLevel);
	virtual bool ShouldDeleteActor(AActor* Actor, bool bMainLevel) const;
	virtual void PerformAdditionalWorldCleanup(UWorld* World) const;
	virtual void OutputConversionReport() const;
	virtual void OnWorldLoaded(UWorld* World);
	virtual void ReadAdditionalTokensAndSwitches(const TArray<FString>& Tokens, const TArray<FString>& Switches) {}

	UWorldPartition* CreateWorldPartition(class AWorldSettings* MainWorldSettings, UWorldComposition* WorldComposition) const;
	UWorld* LoadWorld(const FString& LevelToLoad);
	ULevel* InitWorld(UWorld* World);

	void ChangeObjectOuter(UObject* Object, UObject* NewOuter);
	void FixupSoftObjectPaths(UPackage* OuterPackage);

	bool DetachDependantLevelPackages(ULevel* Level);
	bool RenameWorldPackageWithSuffix(UWorld* World);

	UHLODLayer* CreateHLODLayerFromINI(const FString& InHLODLayerName);
	void SetupHLODLayerAssets();

	void SetActorGuid(AActor* Actor, const FGuid& NewGuid);
	void CreateWorldMiniMapTexture(UWorld* World);

	// Conversion report
	TSet<FString> MapsWithLevelScriptsBPs;
	TSet<FString> MapsWithMapBuildData;
	TSet<FString> ActorsWithChildActors;
	TSet<FString> GroupActors;
	TSet<FString> ActorsInGroupActors;
	TSet<FString> ActorsReferencesToActors;

	TMap<FString, FString> RemapSoftObjectPaths;

	FString LevelConfigFilename;
	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;
	FPackageSourceControlHelper PackageHelper;

	bool bDeleteSourceLevels;
	bool bGenerateIni;
	bool bReportOnly;
	bool bVerbose;
	bool bConversionSuffix;
	FString ConversionSuffix;

	UPROPERTY(Config)
	TSubclassOf<UWorldPartitionEditorHash> EditorHashClass;

	UPROPERTY(Config)
	TSubclassOf<UWorldPartitionRuntimeHash> RuntimeHashClass;

	UPROPERTY(Config)
	TMap<FName, EActorGridPlacement> LevelsGridPlacement;

	UPROPERTY(Config)
	FVector WorldOrigin;
	
	UPROPERTY(Config)
	FVector WorldExtent;

	UPROPERTY(Config)
	FString HLODLayerAssetsPath;

	UPROPERTY(Config)
	FString DefaultHLODLayerName;

	UPROPERTY(Config)
	TArray<FHLODLayerActorMapping> HLODLayersForActorClasses;

	UPROPERTY(Config)
	uint32 LandscapeGridSize;

	UPROPERTY(Transient)
	TMap<FString, UHLODLayer*> HLODLayers;
};
