// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractiveToolChange.h"
#include "VectorTypes.h"
#include "DynamicMeshChangeTracker.h"
#include "MeshChange.generated.h"

//class FDynamicMeshChange;		// need to refactor this out of DynamicMeshChangeTracker




/**
 * FMeshChange represents an undoable change to a FDynamicMesh3.
 * Currently only valid to call Apply/Revert when the Object is a one of several components backed by FDynamicMesh: USimpleDynamicMeshComponent, UOctreeDynamicMeshComponent, UPreviewMesh
 */
class MODELINGCOMPONENTS_API FMeshChange : public FToolCommandChange
{
public:
	FMeshChange();
	FMeshChange(TUniquePtr<FDynamicMeshChange> DynamicMeshChangeIn);

	TUniquePtr<FDynamicMeshChange> DynamicMeshChange;

	/** This function is called on Apply and Revert (last argument is true on Apply)*/
	TFunction<void(FMeshChange*, UObject*, bool)> OnChangeAppliedFunc;

	/** Makes the change to the object */
	virtual void Apply(UObject* Object) override;

	/** Reverts change to the object */
	virtual void Revert(UObject* Object) override;

	/** Describes this change (for debugging) */
	virtual FString ToString() const override;


	/** 
	 * Apply embedded DynamicMeshChange to given Mesh. This function is for 
	 * change-targets to call, when passed a FMeshChange to apply to a Mesh they own. 
	 * This allows FMeshChange subclasses to customize the change behavior if necessary.
	 * The default behavior just forwards the call to DynamicMeshChange->Apply(Mesh, bRevert).
	 */
	virtual void ApplyChangeToMesh(FDynamicMesh3* Mesh, bool bRevert) const;
};





UINTERFACE()
class MODELINGCOMPONENTS_API UMeshCommandChangeTarget : public UInterface
{
	GENERATED_BODY()
};
/**
 * IMeshCommandChangeTarget is an interface which is used to apply a mesh change
 */
class MODELINGCOMPONENTS_API IMeshCommandChangeTarget
{
	GENERATED_BODY()
public:
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) = 0;
};


