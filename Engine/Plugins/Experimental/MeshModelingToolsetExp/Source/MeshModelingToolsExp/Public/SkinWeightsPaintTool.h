// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshBrushTool.h"
#include "BaseTools/MeshSurfacePointMeshEditingTool.h"
#include "MeshDescription.h"
#include "DynamicMesh/DynamicVerticesOctree3.h"
#include "BoneWeights.h"
#include "Misc/Optional.h"
#include "Containers/Map.h"
#include "DynamicMesh/DynamicMeshOctree3.h"
#include "SkeletalMesh/SkeletalMeshEditionInterface.h"
#include "Engine/SkeletalMesh.h"

#include "SkinWeightsPaintTool.generated.h"


struct FMeshDescription;
class USkinWeightsPaintTool;

using BoneIndex = int32;
using VertexIndex = int32;

namespace SkinPaintTool
{
	struct FSkinToolWeights;

	struct FVertexBoneWeight
	{
		FVertexBoneWeight() : BoneIndex(INDEX_NONE), VertexInBoneSpace(FVector::ZeroVector), Weight(0.0f) {}
		FVertexBoneWeight(int32 InBoneIndex, const FVector& InPosInRefPose, float InWeight) :
			BoneIndex(InBoneIndex), VertexInBoneSpace(InPosInRefPose), Weight(InWeight){}
		
		int32 BoneIndex;
		FVector VertexInBoneSpace;
		float Weight;
	};

	using VertexWeights = TArray<FVertexBoneWeight, TFixedAllocator<UE::AnimationCore::MaxInlineBoneWeightCount>>;

	// data required to preview the skinning deformations as you paint
	struct FSkinToolDeformer
	{
		void Initialize(const USkeletalMeshComponent* SkeletalMeshComponent, const FMeshDescription* Mesh);

		void UpdateVertexDeformation(USkinWeightsPaintTool* Tool);

		void SetVertexNeedsUpdated(int32 VertexIndex);
	
		// which vertices require updating (partially re-calculated skinning deformation while painting)
		TSet<int32> VerticesWithModifiedWeights;
		// position of all vertices in the reference pose
		TArray<FVector> RefPoseVertexPositions;
		// inverted, component space ref pose transform of each bone
		TArray<FTransform> InvCSRefPoseTransforms;
		// bone index to bone name
		TArray<FName> BoneNames;
		TMap<FName, BoneIndex> BoneNameToIndexMap;
		// the skeletal mesh to get the current pose from
		const USkeletalMeshComponent* Component;
	};

	// store a sparse set of modifications to a set of vertex weights on a SINGLE bone
	struct FSingleBoneWeightEdits
	{
		int32 BoneIndex;
		TMap<VertexIndex, float> OldWeights;
		TMap<VertexIndex, float> NewWeights;
	};

	// store a sparse set of modifications to a set of vertex weights for a SET of bones
	// with support for merging edits. these are used for transaction history undo/redo.
	struct FMultiBoneWeightEdits
	{
		void MergeSingleEdit(const int32 BoneIndex, const int32 VertexID, const float OldWeight, const float NewWeight);
		void MergeEdits(const FSingleBoneWeightEdits& BoneWeightEdits);
		float GetVertexDeltaFromEdits(const int32 BoneIndex, const int32 VertexIndex);

		// map of bone indices to weight edits made to that bone
		TMap<BoneIndex, FSingleBoneWeightEdits> PerBoneWeightEdits;
	};

	class FMeshSkinWeightsChange : public FToolCommandChange
	{
	public:
		FMeshSkinWeightsChange() : FToolCommandChange()	{ }

		virtual FString ToString() const override
		{
			return FString(TEXT("Paint Skin Weights"));
		}

		void Apply(UObject* Object) override;

		void Revert(UObject* Object) override;

		void AddBoneWeightEdit(const FSingleBoneWeightEdits& BoneWeightEdit);

	private:
		FMultiBoneWeightEdits AllWeightEdits;
	};

	// intermediate storage of the weight maps for duration of tool
	struct FSkinToolWeights
	{
		// copy the initial weight values from the skeletal mesh
		void InitializeSkinWeights(
			const USkeletalMeshComponent* SkeletalMeshComponent,
			FMeshDescription* Mesh);

		// applies an edit to a single vertex weight on a single bone, then normalizes the remaining weights while
		// keeping the edited weight intact (ie, adapts OTHER influences to achieve normalization)
		void EditVertexWeightAndNormalize(
			const FName BoneToHoldConstant,
			const int32 VertexId,
			const float NewWeightValue,
			FMultiBoneWeightEdits& WeightEdits);

		void ApplyCurrentWeightsToMeshDescription(FMeshDescription* EditedMesh);

		static float GetWeightOfBoneOnVertex(
			const int32 BoneIndex,
			const int32 VertexID,
			const TArray<VertexWeights>& InVertexWeights);

		void SetWeightOfBoneOnVertex(
			const int32 BoneIndex,
			const int32 VertexID,
			const float Weight,
			TArray<VertexWeights>& InOutVertexData);

		void ResetAfterStroke();

		float SetCurrentFalloffAndGetMaxFalloffThisStroke(int32 VertexID, float CurrentStrength);

		void ApplyEditsToWeightMap(const FMultiBoneWeightEdits& Edits, TArray<VertexWeights>& InOutWeights);
		
		// double-buffer of the entire weight matrix (stored sparsely for fast deformation)
		// "Pre" is state of weights at stroke start
		// "Current" is state of weights during stroke
		// When stroke is over, Pre weights are synchronized with current.
		TArray<VertexWeights> PreStrokeWeights;
		TArray<VertexWeights> CurrentWeights;

		// record the current maximum amount of falloff applied to each vertex during the current stroke
		// values range from 0-1, this allows brushes to sweep over the same vertex, and apply only the maximum amount
		// of modification (add/replace/relax etc) that was encountered for the duration of the stroke.
		TArray<float> MaxFalloffPerVertexThisStroke;

		// update deformation when vertex weights are modified
		FSkinToolDeformer Deformer;
	};

}

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API USkinWeightsPaintToolBuilder : public UMeshSurfacePointMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

// weight color mode
UENUM()
enum class EWeightColorMode : uint8
{
	Greyscale,
	ColorRamp,
};

// brush falloff mode
UENUM()
enum class EWeightBrushFalloffMode : uint8
{
	Surface,
	Volume,
};

// brush behavior mode
UENUM()
enum class EBrushBehaviorMode : uint8
{
	Add,
	Replace,
	Multiply,
	Relax
};


UCLASS()
class MESHMODELINGTOOLSEXP_API USkinWeightsPaintToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

	USkinWeightsPaintToolProperties(const FObjectInitializer& ObjectInitializer);
	
public:

	UPROPERTY(EditAnywhere, Category = Brush)
	EBrushBehaviorMode BrushMode;
	
	UPROPERTY(EditAnywhere, Category = Brush)
	EWeightBrushFalloffMode FalloffMode;

	UPROPERTY(EditAnywhere, Category = Brush)
	EWeightColorMode ColorMode;
	
	UPROPERTY(EditAnywhere, Category = WeightColors)
	TArray<FLinearColor> ColorRamp;

	UPROPERTY(EditAnywhere, Category = WeightColors)
	FLinearColor MinColor;

	UPROPERTY(EditAnywhere, Category = WeightColors)
	FLinearColor MaxColor;
	
	TObjectPtr<USkeletalMesh> SkeletalMesh;
	bool bColorModeChanged = false;
};

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API USkinWeightsPaintTool : public UDynamicMeshBrushTool, public ISkeletalMeshEditionInterface
{
	GENERATED_BODY()

public:
	void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	void Setup() override;

	bool HasCancel() const override { return true; }
	bool HasAccept() const override { return true; }
	bool CanAccept() const override { return true; }

	// UBaseBrushTool overrides
	bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	void OnBeginDrag(const FRay& Ray) override;
	void OnUpdateDrag(const FRay& Ray) override;
	void OnEndDrag(const FRay& Ray) override;
	bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	void ExternalUpdateWeights(const int32 BoneIndex, const TMap<int32, float>& IndexValues);

protected:

	virtual void ApplyStamp(const FBrushStampData& Stamp);
	void OnShutdown(EToolShutdownType ShutdownType) override;
	void OnTick(float DeltaTime) override;

	// stamp
	double CalculateBrushFalloff(double Distance) const;
	void CalculateVertexROI(
		const FBrushStampData& Stamp,
		TArray<int>& VertexROI,
		TArray<float>& VertexSqDistances) const;
	void EditWeightOfVerticesInStamp(
		EBrushBehaviorMode EditMode,
		const TArray<int32>& VerticesInStamp,
		const TArray<float>& VertexSqDistances,
		SkinPaintTool::FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp);
	void RelaxWeightOnVertices(
		TArray<int32> VerticesInStamp,
		TArray<float> VertexSqDistances,
		SkinPaintTool::FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp);
	bool bInvertStroke = false;
	bool bSmoothStroke = false;
	FBrushStampData StartStamp;
	FBrushStampData LastStamp;
	bool bStampPending;
	int32 TriangleUnderStamp;
	FVector StampLocalPos;

	// used to accelerate mesh queries
	UE::Geometry::TDynamicVerticesOctree3<FDynamicMesh3> VerticesOctree;
	UE::Geometry::FDynamicMeshOctree3 TrianglesOctree;
	TFuture<void> TriangleOctreeFuture;
	TArray<int32> TriangleToReinsert;

	// tool properties
	UPROPERTY()
	TObjectPtr<USkinWeightsPaintToolProperties> ToolProps;
	void OnToolPropertiesModified(UObject* ModifiedObject, FProperty* ModifiedProperty);
	
	// the currently edited mesh description
	TUniquePtr<FMeshDescription> EditedMesh;

	// storage of vertex weights per bone 
	SkinPaintTool::FSkinToolWeights Weights;

	// vertex colors updated when switching current bone or editing weights
	void UpdateCurrentBoneVertexColors();
	FVector4f WeightToColor(float Value) const;
	bool bVisibleWeightsValid = false;

	// weight editing transactions
	void BeginChange();
	TUniquePtr<SkinPaintTool::FMeshSkinWeightsChange> EndChange();
	TUniquePtr<SkinPaintTool::FMeshSkinWeightsChange> ActiveChange;

	// which bone are we currently painting?
	void UpdateCurrentBone(const FName &BoneName);
	FName CurrentBone = NAME_None;
	TOptional<FName> PendingCurrentBone;

	virtual void HandleSkeletalMeshModified(const TArray<FName>& InBoneNames, const ESkeletalMeshNotifyType InNotifyType) override;

	friend SkinPaintTool::FSkinToolDeformer;
};
