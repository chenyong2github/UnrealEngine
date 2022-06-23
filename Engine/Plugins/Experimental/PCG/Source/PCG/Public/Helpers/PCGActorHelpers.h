// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "Engine/CollisionProfile.h"

#include <type_traits>

#include "PCGActorHelpers.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UPCGComponent;
class UMaterialInterface;
class UActorComponent;
class ULevel;
class UWorld;

struct FPCGISMCBuilderParameters
{
	UStaticMesh* Mesh = nullptr;
	TArray<UMaterialInterface*> MaterialOverrides;
	EComponentMobility::Type Mobility = EComponentMobility::Static;
	FName CollisionProfile = TEXT("Default");
};

UCLASS(BlueprintType)
class PCG_API UPCGActorHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static UInstancedStaticMeshComponent* GetOrCreateISMC(AActor* InActor, UPCGComponent* SourceComponent, const FPCGISMCBuilderParameters& Params);
	static bool DeleteActors(UWorld* World, const TArray<TSoftObjectPtr<AActor>>& ActorsToDelete);

	/**
	* Fetches all the components of ActorClass's CDO, including the ones added via the BP editor (which AActor.GetComponents fails to do)
	* @param ActorClass class of AActor for which we will retrieve all components
	* @param OutComponents this is where the found components will end up. Note that the preexisting contents of OutComponents will get overridden.
	* @param InComponentClass if supplied will be used to filter the results
	*/
	static void GetActorClassDefaultComponents(const TSubclassOf<AActor>& ActorClass, TArray<UActorComponent*>& OutComponents, const TSubclassOf<UActorComponent>& InComponentClass = TSubclassOf<UActorComponent>());

	template <typename T, typename = typename std::enable_if_t<std::is_base_of_v<AActor, T>>>
	static void ForEachActorInLevel(ULevel* Level, TFunctionRef<void(AActor*)> Callback)
	{
		return ForEachActorInLevel(Level, T::StaticClass(), Callback);
	}

	static void ForEachActorInLevel(ULevel* Level, TSubclassOf<AActor> ActorClass, TFunctionRef<void(AActor*)> Callback);

	/**
	* Spawn a new actor of type T and attach it to the parent (if not null)
	* @param World The world
	* @param BaseName Base name for the actor, will have a unique name
	* @param Transform The transform for the new actor
	* @param Parent Optional parent to attach to.
	*/
	template <typename T = AActor, typename = typename std::enable_if_t<std::is_base_of_v<AActor, T>>>
	static AActor* SpawnDefaultActor(UWorld* World, FName BaseName, const FTransform& Transform, AActor* Parent = nullptr)
	{
		return SpawnDefaultActor(World, T::StaticClass(), BaseName, Transform, Parent);
	}

	/**
	* Spawn a new actor and attach it to the parent (if not null)
	* @param World The world
	* @param ActorClass Class of the actor to spawn
	* @param BaseName Base name for the actor, will have a unique name
	* @param Transform The transform for the new actor
	* @param Parent Optional parent to attach to.
	*/
	static AActor* SpawnDefaultActor(UWorld* World, TSubclassOf<AActor> ActorClass, FName BaseName, const FTransform& Transform, AActor* Parent = nullptr);
};