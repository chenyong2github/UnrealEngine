// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "UObject/ObjectMacros.h"

#include "GroomBindingFactory.generated.h"

UCLASS(MinimalAPI)
class UGroomBindingFactory : public UFactory
{
	GENERATED_BODY()

public:
	UGroomBindingFactory();

	//~ Begin UFactory Interface
	virtual bool ShouldShowInNewMenu() const { return false;  } // Not shown in the Menu, only exposed for code/python access.
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ End UFactory Interface

	/**
	 * Create a new groom binding asset within the contents space of the project.
	 * @param InDesiredPackagePath The package path to use for the groom binding
	 * @param bInBuildAsset If enable, it will build the binding asset, otherwise the building will be deferred to the initial loading.
	 * @param InGroomAsset Groom asset for binding
	 * @param InSkeletalMesh Skeletal mesh on which the groom should be bound to
	 * @param InNumInterpolationPoints Number of point used for RBF constraing (if used)
	 * @param InSourceSkeletalMeshForTransfer Skeletal mesh on which the groom was authored. This should be used only if the skeletal mesh on which the groom is attached to, does not match the rest pose of the groom
	 */
	static class UGroomBindingAsset* CreateNewGroomBindingAsset(
		const FString& InDesiredPackagePath,
		bool bInBuildAsset,
		class UGroomAsset* InGroomAsset,
		class USkeletalMesh* InSkeletalMesh,
		int32 InNumInterpolationPoints=100,
		class USkeletalMesh* InSourceSkeletalMeshForTransfer = nullptr);
};



