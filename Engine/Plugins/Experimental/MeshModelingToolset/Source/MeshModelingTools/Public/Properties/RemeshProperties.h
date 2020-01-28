// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "MeshConstraints.h"

#include "RemeshProperties.generated.h"

/** Mesh Boundary Constraint Types */
UENUM()
enum class EMeshBoundaryConstraint : uint8
{
	Fixed  = (uint8)EEdgeRefineFlags::FullyConstrained  UMETA(DisplayName = "Fixed"),
	Refine = (uint8)EEdgeRefineFlags::SplitsOnly UMETA(DisplayName = "Refine"),
	Free   = (uint8)EEdgeRefineFlags::NoFlip   UMETA(DisplayName = "Free")
};

/** Group Boundary Constraint Types */
UENUM()
enum class EGroupBoundaryConstraint : uint8
{
	Fixed  = (uint8)EEdgeRefineFlags::FullyConstrained  UMETA(DisplayName = "Fixed"),
	Refine = (uint8)EEdgeRefineFlags::SplitsOnly UMETA(DisplayName = "Refine"),
	Free   = (uint8)EEdgeRefineFlags::NoFlip   UMETA(DisplayName = "Free"),
	Ignore = (uint8)EEdgeRefineFlags::NoConstraint UMETA(DisplayName = "Ignore")
};

/** Material Boundary Constraint Types */
UENUM()
enum class EMaterialBoundaryConstraint : uint8
{
	Fixed  = (uint8)EEdgeRefineFlags::FullyConstrained  UMETA(DisplayName = "Fixed"),
	Refine = (uint8)EEdgeRefineFlags::SplitsOnly UMETA(DisplayName = "Refine"),
	Free   = (uint8)EEdgeRefineFlags::NoFlip   UMETA(DisplayName = "Free"),
	Ignore = (uint8)EEdgeRefineFlags::NoConstraint UMETA(DisplayName = "Ignore")
};

UCLASS()
class MESHMODELINGTOOLS_API UMeshConstraintProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** If true, sharp edges are preserved  */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Edge Constraints")
	bool bPreserveSharpEdges;

	/** Mesh Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Boundary Constraints", meta = (DisplayName = "Mesh Boundary"))
	EMeshBoundaryConstraint MeshBoundaryConstraint = EMeshBoundaryConstraint::Free;

	/** Group Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Boundary Constraints", meta = (DisplayName = "Group Boundary"))
	EGroupBoundaryConstraint GroupBoundaryConstraint = EGroupBoundaryConstraint::Free;

	/** Material Boundary Constraint Type */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Boundary Constraints", meta = (DisplayName = "Material Boundary"))
	EMaterialBoundaryConstraint MaterialBoundaryConstraint = EMaterialBoundaryConstraint::Free;

	/** Prevent normal flips */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Edge Constraints")
	bool bPreventNormalFlips = false;

};

UCLASS()
class MESHMODELINGTOOLS_API URemeshProperties : public UMeshConstraintProperties
{
	GENERATED_BODY()

public:
	/** Smoothing speed for dynamic meshing */
	UPROPERTY(EditAnywhere, Category = Remeshing, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingStrength = 0.1f;

	/** Enable edge flips */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Edge Constraints", meta = (DisplayName = "Allow Flips"))
	bool bFlips = false;

	/** Enable edge splits */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Edge Constraints", meta = (DisplayName = "Allow Splits"))
	bool bSplits = true;

	/** Enable edge collapses */
	UPROPERTY(EditAnywhere, Category = "Remeshing|Edge Constraints", meta = (DisplayName = "Allow Collapses"))
	bool bCollapses = true;

};
