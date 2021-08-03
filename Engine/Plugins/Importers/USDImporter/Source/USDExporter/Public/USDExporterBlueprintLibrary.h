// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "USDExporterBlueprintLibrary.generated.h"

class AInstancedFoliageActor;
class UFoliageType;
class ULevel;

/**
 * Library of functions that can be used via Python scripting to help export Unreal scenes and assets to USD
 */
UCLASS(meta=(ScriptName="USDExporterLibrary"))
class USDEXPORTER_API UUsdExporterBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
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
	 * Returns the transforms of all instances of a particular UFoliageType on a given level (which defaults to the Actor's level if left nullptr).
	 * Use GetUsedFoliageTypes() to retrieve all foliage types managed by a particular actor.
	 */
	UFUNCTION( BlueprintCallable, meta = ( ScriptMethod ), Category = "USD Foliage Exporter" )
	static TArray<FTransform> GetInstanceTransforms( AInstancedFoliageActor* Actor, UFoliageType* FoliageType, ULevel* InstancesLevel = nullptr);
};