// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "Engine/CollisionProfile.h"

#include "PCGActorHelpers.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UPCGComponent;
class UMaterialInterface;
class UActorComponent;

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
};