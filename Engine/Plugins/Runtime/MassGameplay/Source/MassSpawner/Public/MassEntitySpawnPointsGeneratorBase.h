// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "MassEntitySpawnPointsGeneratorBase.generated.h"

/** @Todo: Ideally we would return something else then just a FVector, we could return as an example the lane index, smartobject id, navmesh location, etc...*/
DECLARE_DELEGATE_OneParam(FFinishedGeneratingSpawnPointsSignature, const TArray<FVector>&);

/**
 * Base class for Mass Entity Spawn Points Generator.
 * A Mass Spawn Points Generator can be of several type (EQS, ZoneGraph, Volume, Area, etc.)
 * The concept is to override the GenerateSpawnPoints() method and requesting a certain number of Spawn Point Locations to the method.
 */
UCLASS(Abstract, Blueprintable)
class MASSSPAWNER_API UMassEntitySpawnPointsGeneratorBase : public UObject
{
	GENERATED_BODY()

public:

	/** Generate "Count" number of SpawnPoints and return as a list of position
	 * @param Count of point to generate
	 * @param FinishedGeneratingSpawnPointsDelegate is the callback to call once the generation is done
	 */
	 virtual void GenerateSpawnPoints(UObject& QueryOwner, int32 Count, FFinishedGeneratingSpawnPointsSignature& FinishedGeneratingSpawnPointsDelegate) const PURE_VIRTUAL(UMassEntitySpawnPointsGeneratorBase::GenerateSpawnPoints, );

};
