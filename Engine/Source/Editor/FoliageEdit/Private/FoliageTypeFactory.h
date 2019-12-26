// Copyright Epic Games, Inc. All Rights Reserved.

/**
* Factory for FoliageType assets
*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "FoliageTypeFactory.generated.h"

UCLASS()
class UFoliageType_InstancedStaticMeshFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FString GetDefaultNewAssetName() const override
	{
		return TEXT("NewInstancedStaticMeshFoliage");
	}
	virtual FText GetToolTip() const override;
	// End of UFactory interface
};

UCLASS()
class UFoliageType_ActorFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FString GetDefaultNewAssetName() const override
	{
		return TEXT("NewActorFoliage");
	}
	virtual FText GetToolTip() const override;
	// End of UFactory interface
};