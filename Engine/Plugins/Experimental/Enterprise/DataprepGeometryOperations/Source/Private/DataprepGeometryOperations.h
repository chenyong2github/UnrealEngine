// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataprepOperation.h"

#include "Properties/RemeshProperties.h"
#include "CleaningOps/RemeshMeshOp.h"
#include "CleaningOps/SimplifyMeshOp.h"
#include "BakeTransformTool.h"

//
#include "DataprepGeometryOperations.generated.h"

class AStaticMeshActor;
class IMeshBuilderModule;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;


UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Remesh", ToolTip = "Experimental - Remesh input meshes") )
class UDataprepRemeshOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:

	/** Target triangle count */
	UPROPERTY(EditAnywhere, Category = Remeshing)
	int TargetTriangleCount = 1000;

	/** Amount of Vertex Smoothing applied within Remeshing */
	UPROPERTY(EditAnywhere, Category = Remeshing, meta = (DisplayName = "Smoothing Rate", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingStrength = 0.25;

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Remeshing)
	bool bDiscardAttributes = false;

	/** Remeshing type */
	UPROPERTY(EditAnywhere, Category = Remeshing)
	ERemeshType RemeshType = ERemeshType::Standard;

	/** Number of Remeshing passes */
	UPROPERTY(EditAnywhere, Category = Remeshing, meta = (EditCondition = "RemeshType == ERemeshType::FullPass", UIMin = "0", UIMax = "50", ClampMin = "0", ClampMax = "1000"))
	int RemeshIterations = 20;

	/** Mesh Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Boundary Constraints", meta = (DisplayName = "Mesh Boundary"))
	EMeshBoundaryConstraint MeshBoundaryConstraint = EMeshBoundaryConstraint::Free;

	/** Group Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Boundary Constraints", meta = (DisplayName = "Group Boundary"))
	EGroupBoundaryConstraint GroupBoundaryConstraint = EGroupBoundaryConstraint::Free;

	/** Material Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Boundary Constraints", meta = (DisplayName = "Material Boundary"))
	EMaterialBoundaryConstraint MaterialBoundaryConstraint = EMaterialBoundaryConstraint::Free;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Bake Transform", ToolTip = "Experimental - Bake transform of input meshes") )
class UDataprepBakeTransformOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:

	/** Bake rotation */
	UPROPERTY(EditAnywhere, Category = BakeTransform)
	bool bBakeRotation = true;

	/** Bake scale */
	UPROPERTY(EditAnywhere, Category = BakeTransform)
	EBakeScaleMethod BakeScale = EBakeScaleMethod::BakeNonuniformScale;

	/** Recenter pivot after baking transform */
	UPROPERTY(EditAnywhere, Category = BakeTransform)
	bool bRecenterPivot = false;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Weld Edges", ToolTip = "Experimental - Weld edges of input meshes") )
class UDataprepWeldEdgesOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:

	/** Merge search tolerance */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.000001", UIMax = "0.01", ClampMin = "0.00000001", ClampMax = "1000.0"))
	float Tolerance;

	/** Apply to only unique pairs */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bOnlyUnique;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Simplify Mesh", ToolTip = "Experimental - Simplify input meshes") )
class UDataprepSimplifyMeshOperation : public UDataprepEditingOperation
{
	GENERATED_BODY()

public:

	/** Target percentage of original triangle count */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "100"))
	int TargetPercentage = 50;

	/** If true, UVs and Normals are discarded  */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bDiscardAttributes = false;

	/** Mesh Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "SimplifyMesh|Boundary Constraints", meta = (DisplayName = "Mesh Boundary"))
	EMeshBoundaryConstraint MeshBoundaryConstraint = EMeshBoundaryConstraint::Free;

	/** Group Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "SimplifyMesh|Boundary Constraints", meta = (DisplayName = "Group Boundary"))
	EGroupBoundaryConstraint GroupBoundaryConstraint = EGroupBoundaryConstraint::Ignore;

	/** Material Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "SimplifyMesh|Boundary Constraints", meta = (DisplayName = "Material Boundary"))
	EMaterialBoundaryConstraint MaterialBoundaryConstraint = EMaterialBoundaryConstraint::Ignore;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};
