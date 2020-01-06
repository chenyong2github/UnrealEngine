// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractiveToolChange.h"
#include "MeshReplacementChange.generated.h"


class FDynamicMesh3;




/**
 * FMeshReplacementChange represents an undoable *complete* change to a FDynamicMesh3.
 * Currently only valid to call Apply/Revert when the Object is a USimpleDynamicMeshComponent
 */
class MODELINGCOMPONENTS_API FMeshReplacementChange : public FToolCommandChange
{
	TSharedPtr<const FDynamicMesh3> Before, After;

public:
	FMeshReplacementChange();
	FMeshReplacementChange(TSharedPtr<const FDynamicMesh3> Before, TSharedPtr<const FDynamicMesh3> After);

	const TSharedPtr<const FDynamicMesh3>& GetMesh(bool bRevert) const
	{
		return bRevert ? Before : After;
	}

	/** This function is called on Apply and Revert (last argument is true on Apply)*/
	TFunction<void(FMeshReplacementChange*, UObject*, bool)> OnChangeAppliedFunc;

	/** Makes the change to the object */
	virtual void Apply(UObject* Object) override;

	/** Reverts change to the object */
	virtual void Revert(UObject* Object) override;

	/** Describes this change (for debugging) */
	virtual FString ToString() const override;
};





UINTERFACE()
class MODELINGCOMPONENTS_API UMeshReplacementCommandChangeTarget : public UInterface
{
	GENERATED_BODY()
};
/**
 * IMeshReplacementCommandChangeTarget is an interface which is used to apply a mesh replacement change
 */
class MODELINGCOMPONENTS_API IMeshReplacementCommandChangeTarget
{
	GENERATED_BODY()
public:
	virtual void ApplyChange(const FMeshReplacementChange* Change, bool bRevert) = 0;
};


