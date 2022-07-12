// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnalyticsBlueprintLibrary.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "USDConversionBlueprintLibrary.generated.h"

class AInstancedFoliageActor;
class UFoliageType;
class ULevel;

/** Wrapped static conversion functions from the UsdUtilities module, so that they can be used via scripting */
UCLASS(meta=(ScriptName="UsdConversionLibrary"))
class USDEXPORTER_API UUsdConversionBlueprintLibrary : public UBlueprintFunctionLibrary
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

	/**
	 * Wraps AInstancedFoliageActor::GetInstancedFoliageActorForLevel, and allows retrieving the current AInstancedFoliageActor
	 * for a level. Will default to the current editor level if Level is left nullptr.
	 * This function is useful because it's difficult to retrieve this actor otherwise, as it will be filtered from
	 * the results of functions like EditorLevelLibrary.get_all_level_actors()
	 */
	UFUNCTION( BlueprintCallable, Category = "USD Foliage Exporter" )
	static AInstancedFoliageActor* GetInstancedFoliageActorForLevel( bool bCreateIfNone = false, ULevel* Level = nullptr );

	/**
	 * Returns all the different types of UFoliageType assets that a particular AInstancedFoliageActor uses.
	 * This function exists because we want to retrieve all instances of all foliage types on an actor, but we
	 * can't return nested containers from UFUNCTIONs, so users of this API should call this, and then GetInstanceTransforms.
	 */
	UFUNCTION( BlueprintCallable, meta = ( ScriptMethod ), Category = "USD Foliage Exporter" )
	static TArray<UFoliageType*> GetUsedFoliageTypes( AInstancedFoliageActor* Actor );

	/**
	 * Returns the source asset for a UFoliageType.
	 * It can be a UStaticMesh in case we're dealing with a UFoliageType_InstancedStaticMesh, but it can be other types of objects.
	 */
	UFUNCTION( BlueprintCallable, meta = ( ScriptMethod ), Category = "USD Foliage Exporter" )
	static UObject* GetSource( UFoliageType* FoliageType );

	/**
	 * Returns the transforms of all instances of a particular UFoliageType on a given level. If no level is provided all instances will be returned.
	 * Use GetUsedFoliageTypes() to retrieve all foliage types managed by a particular actor.
	 */
	UFUNCTION( BlueprintCallable, meta = ( ScriptMethod ), Category = "USD Foliage Exporter" )
	static TArray<FTransform> GetInstanceTransforms( AInstancedFoliageActor* Actor, UFoliageType* FoliageType, ULevel* InstancesLevel = nullptr );

	/** Defer to the USDClasses module to actually send analytics information */
	UFUNCTION( BlueprintCallable, Category = "Analytics" )
	static void SendAnalytics( const TArray<FAnalyticsEventAttr>& Attrs, const FString& EventName, bool bAutomated, double ElapsedSeconds, double NumberOfFrames, const FString& Extension );
};
