// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

#include "USDConversionBlueprintLibrary.generated.h"

/** Wrapped static conversion functions from the UsdUtilities module, so that they can be used via scripting */
UCLASS(meta=(ScriptName="UsdConversionLibrary"))
class USDEXPORTER_API UUsdConversionBlueprintLibrary : public UObject
{
	GENERATED_BODY()

public:
	/** Fully streams in and displays all levels whose names are not in LevelsToIgnore */
	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static void StreamInRequiredLevels( UWorld* World, const TSet<FString>& LevelsToIgnore );

	/**
	 * If we have the Sequencer open with a level sequence animating the level before export, this function can revert
	 * any actor or component to its unanimated state
	 */
	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static void RevertSequencerAnimations();

	/**
	 * If we used `ReverseSequencerAnimations` to undo the effect of an opened sequencer before export, this function
	 * can be used to re-apply the sequencer state back to the level after the export is complete
	 */
	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static void ReapplySequencerAnimations();

	/**
	 * Returns the path name (e.g. "/Game/Maps/MyLevel") of levels that are loaded on `World`.
	 * We use these to revert the `World` to its initial state after we force-stream levels in for exporting
	 */
	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static TArray<FString> GetLoadedLevelNames( UWorld* World );

	/**
	 * Returns the path name (e.g. "/Game/Maps/MyLevel") of levels that checked to be visible in the editor within `World`.
	 * We use these to revert the `World` to its initial state after we force-stream levels in for exporting
	 */
	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static TArray<FString> GetVisibleInEditorLevelNames( UWorld* World );

	/** Streams out/hides sublevels that were streamed in before export */
	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static void StreamOutLevels( UWorld* OwningWorld, const TArray<FString>& LevelNamesToStreamOut, const TArray<FString>& LevelNamesToHide );

	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static TSet<AActor*> GetActorsToConvert( UWorld* World );

	UFUNCTION( BlueprintCallable, Category = "Layer utils" )
	static FString MakePathRelativeToLayer( const FString& AnchorLayerPath, const FString& PathToMakeRelative );

	UFUNCTION( BlueprintCallable, Category = "Layer utils" )
	static void InsertSubLayer( const FString& ParentLayerPath, const FString& SubLayerPath, int32 Index = -1 );

	UFUNCTION( BlueprintCallable, Category = "Layer utils" )
	static void AddPayload( const FString& ReferencingStagePath, const FString& ReferencingPrimPath, const FString& TargetStagePath );

	UFUNCTION( BlueprintCallable, Category = "Prim utils" )
	static FString GetPrimPathForObject( const UObject* ActorOrComponent, const FString& ParentPrimPath = TEXT(""), bool bUseActorFolders = false );

	UFUNCTION( BlueprintCallable, Category = "Prim utils" )
	static FString GetSchemaNameForComponent( const USceneComponent* Component );
};
