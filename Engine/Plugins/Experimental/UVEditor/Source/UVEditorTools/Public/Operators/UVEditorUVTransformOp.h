// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "InteractiveTool.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

#include "UVEditorUVTransformOp.generated.h"

class UUVTransformProperties;

/**
 * UV Transform Strategies for the UV Transform Tool
 */
UENUM()
enum class EUVEditorUVTransformType
{
	/** Apply Scale, Totation and Translation properties to all UV values */
	Transform,
	/** Position all selected elements given alignment rules */
	Align,
	/** Position all selected elements given distribution rules */
	Distribute
};

/**
 * Transform Pivot Mode
 */
UENUM()
enum class EUVEditorPivotType
{
	/** Pivot around the collective center of all island bounding boxes */
	BoundingBoxCenter,
	/** Pivot around the global origin point */
	Origin,
	/** Pivot around each island's bounding box center */
	IndividualBoundingBoxCenter,
	/** Pivot around a user specified point */
	Manual
};

/**
 * Translation Mode
 */
UENUM()
enum class EUVEditorTranslationMode
{
	/** Move elements relative to their current position by the amount specified */
	Relative,
	/** Move elements such that the transform origin is placed at the value specified */
	Absolute
};

UENUM()
enum class EUVEditorAlignDirection
{
	Top,
	Bottom,
	Left,
	Right,
	CenterVertically,
	CenterHorizontally
};

UENUM()
enum class EUVEditorAlignAnchor
{
	//FirstItem, // TODO: Support this later once we have support in the Selection API

	/** Align relative to the collective bounding box of all islands */
	BoundingBox,
	/** Align relative to the local UDIM tile containing the island */
	UDIMTile,
	/** Align relative to a user specified point */
	Manual
};

UENUM()
enum class EUVEditorDistributeMode
{
	VerticalSpace,
	HorizontalSpace,
	TopEdges,
	BottomEdges,
	LeftEdges,
	RightEdges,
	CentersVertically,
	CentersHorizontally
};


/**
 * UV Transform Settings
 */
UCLASS()
class UVEDITORTOOLS_API UUVEditorUVTransformProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Type of Transform applied to input UVs */
	UPROPERTY()
	EUVEditorUVTransformType TransformType = EUVEditorUVTransformType::Transform;

	/** Scale applied to UVs, potentially non-uniform */
	UPROPERTY(EditAnywhere, Category = "Scaling", Transient, meta = (DisplayName = "Scale",
		      EditCondition = "TransformType == EUVEditorUVTransformType::Transform", EditConditionHides, HideEditConditionToggle = true))
	FVector2D Scale = FVector2D(1.0, 1.0); 

	/** Rotation applied to UVs after scaling, specified in degrees */
	UPROPERTY(EditAnywhere, Category = "Rotation", Transient, meta = (DisplayName = "Rotation",
		      EditCondition = "TransformType == EUVEditorUVTransformType::Transform", EditConditionHides, HideEditConditionToggle = true))
	float Rotation = 0.0;

	/** Translation applied to UVs, and after scaling and rotation */
	UPROPERTY(EditAnywhere, Category = "Translation", Transient, meta = (DisplayName = "Transform",
		      EditCondition = "TransformType == EUVEditorUVTransformType::Transform", EditConditionHides, HideEditConditionToggle = true))
	FVector2D Translation = FVector2D(0, 0);

	/** Translation applied to UVs, and after scaling and rotation */
	UPROPERTY(EditAnywhere, Category = "Translation", Transient, meta = (DisplayName = "Transform Mode",
		EditCondition = "TransformType == EUVEditorUVTransformType::Transform", EditConditionHides, HideEditConditionToggle = true))
	EUVEditorTranslationMode TranslationMode = EUVEditorTranslationMode::Relative;

	/** Transformation origin mode used for scaling and rotation */
	UPROPERTY(EditAnywhere, Category = "Transform Origin", Transient, meta = (DisplayName = "Mode",
		EditCondition = "TransformType == EUVEditorUVTransformType::Transform", EditConditionHides, HideEditConditionToggle = true))
	EUVEditorPivotType PivotMode = EUVEditorPivotType::BoundingBoxCenter;

	/** Manual Transformation origin point */
	UPROPERTY(EditAnywhere, Category = "Transform Origin", Transient, meta = (DisplayName = "Coords",
		EditCondition = "TransformType == EUVEditorUVTransformType::Transform && PivotMode == EUVEditorPivotType::Manual", EditConditionHides, HideEditConditionToggle = true))
	FVector2D ManualPivot = FVector2D(0, 0);

	/** Controls what geometry the alignment is to be relative to when performed. */
	UPROPERTY(EditAnywhere, Category = "Align", meta = (DisplayName = "Alignment Anchor",
		EditCondition = "TransformType == EUVEditorUVTransformType::Align", EditConditionHides, HideEditConditionToggle = true))
	EUVEditorAlignAnchor AlignAnchor = EUVEditorAlignAnchor::BoundingBox;

	/** Manual anchor location for relative alignment */
	UPROPERTY(EditAnywhere, Category = "Align", meta = (DisplayName = "Anchor Coords",
		EditCondition = "TransformType == EUVEditorUVTransformType::Align && AlignAnchor == EUVEditorAlignAnchor::Manual", EditConditionHides, HideEditConditionToggle = true))
	FVector2D ManualAnchor = FVector2D(0, 0);

	/** Controls what side of the island bounding boxes are being aligned */
	UPROPERTY(EditAnywhere, Category = "Align", meta = (DisplayName = "Alignment Direction",
		EditCondition = "TransformType == EUVEditorUVTransformType::Align", EditConditionHides, HideEditConditionToggle = true))
	EUVEditorAlignDirection AlignDirection = EUVEditorAlignDirection::Top;

	/** Controls the distribution behavior */
	UPROPERTY(EditAnywhere, Category = "Distribute", meta = (DisplayName = "Distribution Mode",
		EditCondition = "TransformType == EUVEditorUVTransformType::Distribute", EditConditionHides, HideEditConditionToggle = true))
	EUVEditorDistributeMode DistributeMode = EUVEditorDistributeMode::TopEdges;

};


namespace UE
{
namespace Geometry
{
	class FDynamicMesh3;
	class FDynamicMeshUVPacker;
	class FMeshConnectedComponents;

	enum class EUVEditorUVTransformTypeBackend
	{
		Transform,
		Align,
		Distribute
	};

	enum class EUVEditorPivotTypeBackend
	{
		Origin,
		IndividualBoundingBoxCenter,
		BoundingBoxCenter,
		Manual
	};

	UENUM()
	enum class EUVEditorTranslationModeBackend
	{
		Relative,
		Absolute
	};

	enum class EUVEditorAlignDirectionBackend
	{
		Top,
		Bottom,
		Left,
		Right,
		CenterVertically,
		CenterHorizontally
	};

	enum class EUVEditorAlignAnchorBackend
	{
		//FirstItem, // TODO:  Support this later once we have support in the Selection API
		UDIMTile,
		BoundingBox,
		Manual
	};

	enum class EUVEditorDistributeModeBackend
	{
		LeftEdges,
		RightEdges,
		TopEdges,
		BottomEdges,
		CentersVertically,
		CentersHorizontally,
		VerticalSpace,
		HorizontalSpace
	};

class UVEDITORTOOLS_API FUVEditorUVTransformBaseOp : public FDynamicMeshOperator
{
public:
	virtual ~FUVEditorUVTransformBaseOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	TOptional<TSet<int32>> Selection;
	int UVLayerIndex = 0;

	void SetTransform(const FTransformSRT3d& Transform);

	virtual void CalculateResult(FProgressCancel* Progress) override;

protected:
	virtual void HandleTransformationOp(FProgressCancel* Progress) = 0;

	FVector2f GetAlignmentPointFromBoundingBoxAndDirection(EUVEditorAlignDirectionBackend Direction, const FAxisAlignedBox2d& BoundingBox);
	FVector2f GetAlignmentPointFromUDIMAndDirection(EUVEditorAlignDirectionBackend Direction, FVector2i UDIMTile);
	void RebuildBoundingBoxes();
	void CollectTransformElements();

	TMap<int32, int32> ElementToComponent;
	FDynamicMeshUVOverlay* ActiveUVLayer;
	FAxisAlignedBox2d OverallBoundingBox;
	TArray<FAxisAlignedBox2d> PerComponentBoundingBoxes;
	TOptional< TSet<int32> > TransformingElements;
	TSharedPtr<FMeshConnectedComponents> UVComponents;
};


/**
 */
class UVEDITORTOOLS_API FUVEditorUVTransformOp : public FUVEditorUVTransformBaseOp
{
public:
	virtual ~FUVEditorUVTransformOp() {}

	// inputs
	FVector2D Scale = FVector2D(1.0, 1.0);
	float Rotation = 0.0;
	FVector2D Translation = FVector2D(0, 0);
	EUVEditorTranslationModeBackend TranslationMode = EUVEditorTranslationModeBackend::Relative;

	EUVEditorPivotTypeBackend PivotMode = EUVEditorPivotTypeBackend::Origin;
	FVector2D ManualPivot = FVector2D(0, 0);

protected:
	virtual void HandleTransformationOp(FProgressCancel* Progress) override;
	FVector2f GetPivotFromMode(int32 ElementID);
};

class UVEDITORTOOLS_API FUVEditorUVAlignOp : public FUVEditorUVTransformBaseOp
{
public:
	virtual ~FUVEditorUVAlignOp() {}

	EUVEditorAlignAnchorBackend AlignAnchor = EUVEditorAlignAnchorBackend::BoundingBox;
	EUVEditorAlignDirectionBackend AlignDirection = EUVEditorAlignDirectionBackend::Top;
	FVector2D ManualAnchor = FVector2D(0, 0);

protected:
	virtual void HandleTransformationOp(FProgressCancel* Progress) override;



};

class UVEDITORTOOLS_API FUVEditorUVDistributeOp : public FUVEditorUVTransformBaseOp
{
public:
	virtual ~FUVEditorUVDistributeOp() {}

	EUVEditorDistributeModeBackend DistributeMode = EUVEditorDistributeModeBackend::TopEdges;

protected:
	virtual void HandleTransformationOp(FProgressCancel* Progress) override;

};


} // end namespace UE::Geometry
} // end namespace UE

/**
 * Can be hooked up to a UMeshOpPreviewWithBackgroundCompute to perform UV Transform operations.
 *
 * Inherits from UObject so that it can hold a strong pointer to the settings UObject, which
 * needs to be a UObject to be displayed in the details panel.
 */
UCLASS()
class UVEDITORTOOLS_API UUVEditorUVTransformOperatorFactory : public UObject, public UE::Geometry::IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	TObjectPtr<UUVEditorUVTransformProperties> Settings;

	TOptional<TSet<int32>> Selection;

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	TUniqueFunction<int32()> GetSelectedUVChannel = []() { return 0; };
	FTransform TargetTransform;
};