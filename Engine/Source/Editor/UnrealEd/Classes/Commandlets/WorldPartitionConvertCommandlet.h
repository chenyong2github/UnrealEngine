// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectAnnotation.h"
#include "Commandlets/Commandlet.h"
#include "ISourceControlProvider.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartitionConvertCommandlet.generated.h"

UNREALED_API DECLARE_LOG_CATEGORY_EXTERN(LogWorldPartitionConvertCommandlet, Log, All);

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
	void GatherAndPrepareSubLevelsToConvert(ULevel* Level, TArray<ULevel*>& SubLevels);

protected:
	virtual bool GetAdditionalLevelsToConvert(ULevel* Level, TArray<ULevel*>& SubLevels);
	virtual bool PrepareStreamingLevelForConversion(ULevelStreaming* StreamingLevel);	
	virtual bool ShouldDeleteActor(AActor* Actor, bool bMainLevel) const;
	virtual void PerformAdditionalWorldCleanup(UWorld* World) const;
	virtual void OutputConversionReport() const;
	virtual void OnWorldLoaded(UWorld* World) {}
	virtual void ReadAdditionalTokensAndSwitches(const TArray<FString>& Tokens, const TArray<FString>& Switches) {}

	class UWorldPartition* CreateWorldPartition(class AWorldSettings* MainWorldSettings) const;
	ULevel* LoadLevel(const FString& LevelToLoad);

	bool UseSourceControl() const { return SourceControlProvider != nullptr; }
	ISourceControlProvider& GetSourceControlProvider() { check(UseSourceControl()); return *SourceControlProvider; }

	void ChangeObjectOuter(UObject* Object, UObject* NewOuter);
	void FixupSoftObjectPaths(UPackage* OuterPackage);

	bool DeleteFile(const FString& Filename);
	bool AddPackageToSourceControl(UPackage* Package);
	bool CheckoutPackage(UPackage* Package);
	bool SavePackage(UPackage* Package);
	bool DetachDependantLevelPackages(ULevel* Level);
	bool RenameWorldPackageWithSuffix(UWorld* World);

	UHLODLayer* CreateHLODLayerFromINI(const FString& InHLODLayerName);
	void SetupHLODLayerAssets();

	// Conversion report
	TSet<FString> MapsWithLevelScriptsBPs;
	TSet<FString> MapsWithMapBuildData;
	TSet<FString> ActorsWithChildActors;
	TSet<FString> GroupActors;
	TSet<FString> ActorsInGroupActors;
	TSet<FString> ActorsReferencesToActors;

	ISourceControlProvider* SourceControlProvider;
	TMap<FString, FString> RemapSoftObjectPaths;

	FString LevelConfigFilename;
	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;

	bool bNoSourceControl;
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
	FString HLODLayerAssetsPath;

	UPROPERTY(Config)
	FString DefaultHLODLayerName;

	UPROPERTY(Config)
	TArray<FHLODLayerActorMapping> HLODLayersForActorClasses;

	UPROPERTY(Transient)
	TMap<FString, UHLODLayer*> HLODLayers;
};
