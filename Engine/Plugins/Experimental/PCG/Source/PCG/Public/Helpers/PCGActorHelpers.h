// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGActorHelpers.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UPCGComponent;
class UMaterialInterface;

UCLASS(BlueprintType)
class PCG_API UPCGActorHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static UInstancedStaticMeshComponent* GetOrCreateISMC(AActor* InActor, const UPCGComponent* SourceComponent, UStaticMesh* InMesh, const TArray<UMaterialInterface*>& InMaterials = TArray<UMaterialInterface*>());
};