// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "PrefabUncooked.generated.h"

// This type is an obscure implementation detail, it is meant to be 
// hidden from the user completely by our editor or other frontend:
USTRUCT()
struct FPFVariableAssignment
{
	GENERATED_BODY()
	
	// This identifier may chain/nest into an object graph:
	UPROPERTY(EditAnywhere, Category = Prefab)
	FString PFVariableIdentifier;

	// Type could be inferred from value, but explicit will be more durable:
	UPROPERTY(EditAnywhere, Category = Prefab)
	FString PFVariableType;

	// Stringized version of a value, again more durable than an actual value type:
	UPROPERTY(EditAnywhere, Category = Prefab)
	FString PFVariableValue;
};

// Structure encapsulating the state of an instance that will be created from the prefab:
USTRUCT()
struct FPFInstanceDeclaration
{
	GENERATED_BODY()

	// Path to our type - the immediate goal is for this to be a native UClass or another 
	// Prefab, but we could support other instancing facilities:
	UPROPERTY(EditAnywhere, Category = Prefab, meta = (DisplayName = "Instance Type"))
	FSoftObjectPath PFInstanceType;

	// This will be completely hidden from the user, they will poke at a 
	// preview object:
	UPROPERTY(EditAnywhere, Category = Prefab)
	TArray<FPFVariableAssignment> PFVariableAssignments;
};

UCLASS()
class PREFABSUNCOOKED_API UPrefabUncooked : public UObject
{
	GENERATED_BODY()

	UPROPERTY(Category = Prefab, EditAnywhere)
	FPFInstanceDeclaration InstanceDecl; // this will be an array, sooner than later

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
};
