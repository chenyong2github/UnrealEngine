// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshBrushTool.h"
#include "BaseTools/MeshSurfacePointMeshEditingTool.h"
#include "Changes/ValueWatcher.h"
#include "MeshDescription.h"
#include "DynamicMesh/DynamicVerticesOctree3.h"
#include "BoneContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "Misc/Optional.h"
#include "Containers/Map.h"
#include "SkeletalMesh/SkeletalMeshEditionInterface.h"

#include "SkinWeightsPaintTool.generated.h"


struct FMeshDescription;
class USkinWeightsPaintTool;

// store a map of bone names to weight arrays, where array length == num vertices.
// this struct stores ALL weights for ALL bones in the entire mesh.
using PerBoneWeightMap = TMap<FName, TArray<float>>;

// store a sparse set of modifications to a set of vertex weights on a SINGLE bone
struct FSingleBoneWeightEdits
{
	FName BoneName;
	TMap<int32, float> OldWeights;
	TMap<int32, float> NewWeights;
};

// store a sparse set of modifications to a set of vertex weights for a SET of bones
// with support for merging edits. these are used for transaction history undo/redo.
struct FMultiBoneWeightEdits
{
	void MergeSingleEdit(const FName BoneName, const int32 VertexID, const float OldWeight, const float NewWeight)
	{
		PerBoneWeightEdits.FindOrAdd(BoneName);
		PerBoneWeightEdits[BoneName].BoneName = BoneName;
		PerBoneWeightEdits[BoneName].NewWeights.Add(VertexID, NewWeight);
		PerBoneWeightEdits[BoneName].OldWeights.FindOrAdd(VertexID, OldWeight);
	}
	
	void MergeEdits(const FSingleBoneWeightEdits& BoneWeightEdits)
	{
		// make sure bone has an entry in the map of weight edits
		const FName Bone = BoneWeightEdits.BoneName;
		PerBoneWeightEdits.FindOrAdd(Bone);
		PerBoneWeightEdits[Bone].BoneName = Bone;
	
		for (const TTuple<int32, float>& NewWeight : BoneWeightEdits.NewWeights)
		{
			int32 VertexIndex = NewWeight.Key;
			PerBoneWeightEdits[Bone].NewWeights.Add(VertexIndex, NewWeight.Value);
			PerBoneWeightEdits[Bone].OldWeights.FindOrAdd(VertexIndex, BoneWeightEdits.OldWeights[VertexIndex]);
		}
	}

	float GetVertexDeltaFromEdits(FName BoneName, int32 VertexIndex)
	{
		PerBoneWeightEdits.FindOrAdd(BoneName);
		if (const float* NewVertexWeight = PerBoneWeightEdits[BoneName].NewWeights.Find(VertexIndex))
		{
			return *NewVertexWeight - PerBoneWeightEdits[BoneName].OldWeights[VertexIndex];
		}

		return 0.0f;
	}

	void ApplyEditsToWeightMap(PerBoneWeightMap& InOutWeightMap)
	{
		for (TTuple<FName, FSingleBoneWeightEdits>& BoneWeightEdits : PerBoneWeightEdits)
		{
			const FSingleBoneWeightEdits& WeightEdits = BoneWeightEdits.Value;
			TArray<float>& VertexWeights = InOutWeightMap[WeightEdits.BoneName];
			for (const TTuple<int32, float>& NewWeight : WeightEdits.NewWeights)
			{
				VertexWeights[NewWeight.Key] = NewWeight.Value;
			}
		}
	}
	
	TMap<FName, FSingleBoneWeightEdits> PerBoneWeightEdits;
};

// data required to preview the skinning deformations as you paint
struct FSkinToolDeformer
{
	void Initialize(
		const USkeletalMeshComponent* SkeletalMeshComponent,
		const FMeshDescription* Mesh);

	void UpdateVertexDeformation(
		PerBoneWeightMap& Weights,
		UPreviewMesh* PreviewMesh,
		UE::Geometry::TDynamicVerticesOctree3<FDynamicMesh3>& OctreeToUpdate);

	void SetVerticesNeedUpdated(TArray<int32> VertexIndices);

	void SetVertexNeedsUpdated(int32 VertexIndex);
	
	// which vertices require updating (partially re-calculated skinning deformation while paintin)
	TSet<int32> VerticesWithModifiedWeights;
	// position of all vertices in the reference pose
	TArray<FVector> RefPoseVertexPositions;
	// inverted, component space ref pose transform of each bone
	TArray<FTransform> InvCSRefPoseTransforms;
	// bone name to bone index
	TMap<FName, int32> BoneNameToIndexMap;
	// the skeletal mesh to get the current pose from
	const USkeletalMeshComponent* Component;
};

class MESHMODELINGTOOLSEXP_API FMeshSkinWeightsChange : public FToolCommandChange
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
		const FReferenceSkeleton& RefSkeleton,
		FMeshDescription* EditedMesh);

	// get an array of weights, per-vertex for the given bone
	TArray<float>* GetWeightsForBone(const FName BoneName);

	// applies an edit to a single vertex weight on a single bone, then normalizes the remaining weights while
	// keeping the edited weight intact (ie, adapts OTHER influences to achieve normalization)
	void EditVertexWeightAndNormalize(
		const FName BoneToHoldConstant,
		const int32 VertexId,
		const float NewWeightValue,
		FMultiBoneWeightEdits& WeightEdits);

	void ApplyCurrentWeightsToMeshDescription(
		const FReferenceSkeleton& RefSkeleton,
		FMeshDescription* EditedMesh);

	void ResetAfterStroke();

	float SetCurrentFalloffAndGetMaxFalloffThisStroke(int32 VertexID, float CurrentStength);
	
	// double-buffer of the entire weight matrix
	// "Pre" is state of weights at stroke start
	// "Current" is state of weights during stroke
	// When stroke is over, Pre weights are synchronized with current.
	PerBoneWeightMap PreStrokeWeightsMap;
	PerBoneWeightMap CurrentWeightsMap;

	// record the current maximum amount of falloff applied to each vertex during the current stroke
	// values range from 0-1, this allows brushes to sweep over the same vertex, and apply only the maximum amount
	// of modification (add/replace/relax etc) that was encountered for the duration of the stroke.
	TArray<float> MaxFalloffPerVertexThisStroke;
};

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
class MESHMODELINGTOOLSEXP_API USkinWeightsPaintToolProperties : 
	public UInteractiveToolPropertySet,
	public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

	USkinWeightsPaintToolProperties(const FObjectInitializer& ObjectInitializer);
	
public:

	UPROPERTY(EditAnywhere, Category = Brush)
	EBrushBehaviorMode BrushMode;
	
	UPROPERTY(EditAnywhere, Category = Brush)
	EWeightBrushFalloffMode FalloffMode;
	
	UPROPERTY(VisibleAnywhere, Category = Skeleton)
	FBoneReference CurrentBone;

	UPROPERTY(EditAnywhere, Category = Brush)
	EWeightColorMode ColorMode;
	
	UPROPERTY(EditAnywhere, Category = WeightColors)
	TArray<FLinearColor> ColorRamp;

	UPROPERTY(EditAnywhere, Category = WeightColors)
	FLinearColor MinColor;

	UPROPERTY(EditAnywhere, Category = WeightColors)
	FLinearColor MaxColor;
	
	// IBoneReferenceSkeletonProvider
	USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

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
	void Render(IToolsContextRenderAPI* RenderAPI) override;

	bool HasCancel() const override { return true; }
	bool HasAccept() const override { return true; }
	bool CanAccept() const override { return true; }

	// UBaseBrushTool overrides
	bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	void OnBeginDrag(const FRay& Ray) override;
	void OnUpdateDrag(const FRay& Ray) override;
	void OnEndDrag(const FRay& Ray) override;
	bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	void ExternalUpdateWeights(const FName &BoneName, const TMap<int32, float>& IndexValues);

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
		FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp);
	void RelaxWeightOnVertices(
		TArray<int32> VerticesInStamp,
		TArray<float> VertexSqDistances,
		FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp);
	bool bInvertStroke = false;
	bool bSmoothStroke = false;
	FBrushStampData StartStamp;
	FBrushStampData LastStamp;
	bool bStampPending;
	UE::Geometry::TDynamicVerticesOctree3<FDynamicMesh3> VerticesOctree;

	// tool properties
	UPROPERTY()
	TObjectPtr<USkinWeightsPaintToolProperties> ToolProps;
	void OnToolPropertiesModified(UObject* ModifiedObject, FProperty* ModifiedProperty);
	
	// the currently edited mesh description
	TUniquePtr<FMeshDescription> EditedMesh;

	// storage of vertex weights per bone 
	FSkinToolWeights Weights;
	
	// update deformation when vertex weights are modified
	FSkinToolDeformer Deformer;

	// vertex colors updated when switching current bone or editing weights
	void UpdateCurrentBoneVertexColors();
	FVector4f WeightToColor(float Value);
	bool bVisibleWeightsValid = false;

	// weight editing transactions
	void BeginChange();
	TUniquePtr<FMeshSkinWeightsChange> EndChange();
	TUniquePtr<FMeshSkinWeightsChange> ActiveChange;

	// which bone are we currently painting?
	void UpdateCurrentBone(const FName &BoneName);
	FName CurrentBone = NAME_None;
	TOptional<FName> PendingCurrentBone;

	// bone rendering
	// TODO, bring this up to new method, add support for rotating bones here
	struct FBonePositionInfo
	{
		FName BoneName;
		int32 ParentBoneIndex;
		FVector Position;
		float Radius;
		TMap<FName, int32> ChildBones;
	};
	FBoneContainer BoneContainer;
	TArray<FBonePositionInfo> BonePositionInfos;
	float MaxDrawRadius = 0.0f;
	void UpdateBonePositionInfos(float MinRadius);
	void RenderBonePositions(FPrimitiveDrawInterface *PDI);

	virtual void HandleSkeletalMeshModified(const TArray<FName>& InBoneNames, const ESkeletalMeshNotifyType InNotifyType) override;
};
