// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealUSDWrapper.h"
#include "USDLevelSequenceHelper.h"
#include "USDMemory.h"
#include "USDStageOptions.h"

#include "UsdWrappers/UsdStage.h"

#include "Logging/TokenizedMessage.h"
#include "CoreMinimal.h"

#include "USDStageImportContext.generated.h"

class UUsdAssetCache;
class UUsdStageImportOptions;

USTRUCT()
struct USDSTAGEIMPORTER_API FUsdStageImportContext
{
	GENERATED_BODY()

	UWorld* World;

	/** Spawned actor that contains the imported scene as a child hierarchy */
	UPROPERTY()
	AActor* SceneActor;

	/** Name to use when importing a single mesh */
	UPROPERTY()
	FString ObjectName;

	UPROPERTY()
	FString PackagePath;

	/** Path of the main usd file to import */
	UPROPERTY()
	FString FilePath;

	UPROPERTY()
	UUsdStageImportOptions* ImportOptions;

	/** Keep track of the last imported object so that we have something valid to return to upstream code that calls the import factories */
	UPROPERTY()
	UObject* ImportedAsset;

	/** Level sequence that will contain the animation data during the import process */
	FUsdLevelSequenceHelper LevelSequenceHelper;

	UPROPERTY()
	UUsdAssetCache* AssetCache;

	/**
	 * When parsing materials, we keep track of which primvar we mapped to which UV channel.
	 * When parsing meshes later, we use this data to place the correct primvar values in each UV channel.
	 */
	TMap< FString, TMap< FString, int32 > > MaterialToPrimvarToUVIndex;

	UE::FUsdStage Stage;

	/** Object flags to apply to newly imported objects */
	EObjectFlags ImportObjectFlags;

	/** If true, options dialog won't be shown */
	bool bIsAutomated;

	/**
	 * If true, this will try loading the stage from the static stage cache before re-reading the file. If false,
	 * the USD file at FilePath is reopened (but the stage is left untouched).
	 **/
	bool bReadFromStageCache;

	/** If we're reading from the stage cache and the stage was originally open, it will be left open when the import is completed */
	bool bStageWasOriginallyOpen;

	/** We modify the stage with our meters per unit import option on import. If the stage was already open, we use this to undo the changes after import */
	float OriginalMetersPerUnit;
	EUsdUpAxis OriginalUpAxis;

public:
	FUsdStageImportContext();

	bool Init(const FString& InName, const FString& InFilePath, const FString& InInitialPackagePath, EObjectFlags InFlags, bool bInIsAutomated, bool bIsReimport = false, bool bAllowActorImport = true);

private:
	/** Error messages **/
	TArray<TSharedRef<FTokenizedMessage>> TokenizedErrorMessages;
};
