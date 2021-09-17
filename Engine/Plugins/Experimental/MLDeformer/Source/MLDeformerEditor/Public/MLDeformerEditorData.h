// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInputInfo.h"

class UAnimInstance;
class UMLDeformerAsset;
class UMLDeformerComponent;
class UComputeGraphComponent;
class UComputeGraph;
class IPersonaToolkit;
class UDebugSkelMeshComponent;
class IDetailsView;
class USkeletalMesh;
class UGeometryCache;
class UGeometryCacheComponent;
class UMLDeformerVizSettings;
class FMLDeformerEditorToolkit;
class UMaterial;
class AActor;
class UTextRenderComponent;
class SSimpleTimeSlider;
struct FMLDeformerTrainingData;
struct FReferenceSkeleton;
struct FGeometryCacheMeshData;

enum class EMLDeformerEditorActorIndex : int8
{
	Base = 0,		// Linear skinned.
	Target,			// Geometry cache.
	Test,			// Linear skinned test model.
	DeformedTest,	// Test model with ML deformer applied to it.
	GroundTruth		// Ground truth model that plays the same animation as the test anim asset (optional).
};

struct FMLDeformerEditorActor
{
	TObjectPtr<AActor> Actor = nullptr;
	TObjectPtr<UTextRenderComponent> LabelComponent = nullptr;
	TObjectPtr<UDebugSkelMeshComponent> SkelMeshComponent = nullptr;
	TObjectPtr<UGeometryCacheComponent> GeomCacheComponent = nullptr;
	TObjectPtr<UComputeGraphComponent> ComputeGraphComponent = nullptr;
	TObjectPtr<UMLDeformerComponent> MLDeformerComponent = nullptr;
};

class FMLDeformerEditorData
{
public:
	FMLDeformerEditorData();
	~FMLDeformerEditorData() = default;

	void SetPersonaToolkit(TSharedPtr<IPersonaToolkit> InPersonaToolkit);
	void SetDeformerAsset(UMLDeformerAsset* InAsset);
	void SetAnimInstance(UAnimInstance* InAnimInstance);
	void SetDetailsView(TSharedPtr<IDetailsView> InDetailsView);
	void SetEditorToolkit(FMLDeformerEditorToolkit* InToolkit);
	void SetVizSettingsDetailsView(TSharedPtr<IDetailsView> InDetailsView);
	void SetTimeSlider(TSharedPtr<SSimpleTimeSlider> InTimeSlider);

	int32 GetNumEditorActors() const;
	void SetEditorActor(EMLDeformerEditorActorIndex Index, const FMLDeformerEditorActor& Actor);
	const FMLDeformerEditorActor& GetEditorActor(EMLDeformerEditorActorIndex Index) const;
	FMLDeformerEditorActor& GetEditorActor(EMLDeformerEditorActorIndex Index);
	bool IsTestActor(EMLDeformerEditorActorIndex Index) const;
	bool IsTrainingActor(EMLDeformerEditorActorIndex Index) const;

	void InitAssets();
	bool GenerateDeltas(uint32 LODIndex, uint32 FrameNumber, float DeltaCutoffLength, TArray<float>& OutDeltas);
	bool GenerateTrainingData(uint32 LODIndex, uint32 FrameNumber, float DeltaCutoffLength, TArray<float>& OutDeltas, TArray<FTransform>& OutBoneTransforms, TArray<float>& OutCurveValues);
	bool ComputeVertexDeltaStatistics(uint32 LODIndex, float DeltaCutoffLength);

	void SetTimeSliderRange(double StartTime, double EndTime);
	void UpdateTimeSlider();
	void OnTimeSliderScrubPositionChanged(double NewScrubTime, bool bIsScrubbing);
	void OnPlayButtonPressed();
	double CalcTimelinePosition() const;
	bool IsPlayingAnim() const;

	IPersonaToolkit* GetPersonaToolkit() const;
	TSharedPtr<IPersonaToolkit> GetPersonaToolkitPointer() const;
	UMLDeformerAsset* GetDeformerAsset() const;
	TWeakObjectPtr<UMLDeformerAsset> GetDeformerAssetPointer() const;
	UAnimInstance* GetAnimInstance() const;
	IDetailsView* GetDetailsView() const;
	IDetailsView* GetVizSettingsDetailsView() const;
	FMLDeformerEditorToolkit* GetEditorToolkit() const;
	SSimpleTimeSlider* GetTimeSlider() const;

	float GetDuration() const;
	float GetSnappedFrameTime(float InTime) const;
	float GetTimeAtFrame(int32 FrameNumber) const;
	int32 GetNumImportedVertices() const;

	/**
	 *Update mean and scale of vertex deltas.
	 *
	 * @param InGeomCachePositions Geometric cache positions
	 * @param InLinearSkinnedPositions Skinned positions
	 * @param InDeltaCutoffLength Delta cutoff length
	 * @param InOutMeanVertexDelta Mean vertex delta
	 * @param InOutVertexDeltaScale Vertex delta scale
	 * @param InOutCount Count
	 */
	void UpdateVertexDeltaMeanAndScale(const TArray<FVector3f>& InGeomCachePositions, const TArray<FVector3f>& InLinearSkinnedPositions,
		float InDeltaCutoffLength, FVector3f& InOutMeanVertexDelta, FVector3f& InOutVertexDeltaScale, float& InOutCount) const;

	void ExtractBoneTransforms(TArray<FMatrix44f>& OutBoneMatrices, TArray<FTransform>& OutBoneTransforms) const;
	void ExtractSkinnedPositions(int32 LODIndex, TArray<FMatrix44f>& InBoneMatrices, TArray<FVector3f>& TempPositions, TArray<FVector3f>& OutPositions, bool bImportedVertices=true) const;
	void ExtractGeomCachePositions(int32 LODIndex, TArray<FVector3f>& TempPositions, TArray<FVector3f>& OutPositions) const;

	void ExtractInputBoneTransforms(TArray<FTransform>& OutTransforms) const;
	void ExtractInputCurveValues(TArray<float>& OutValues) const;

	void UpdateDebugPointData();
	void UpdateTestAnimPlaySpeed();
	void UpdateDeformerGraph();
	static FString GetDefaultDeformerGraphAssetPath();
	static UComputeGraph* LoadDefaultDeformerGraph();
	void SetAnimFrame(int32 FrameNumber);
	void CreateMaterials();

	bool IsReadyForTraining() const;
	FText GetOverlayText();

	void SetHeatMapMaterialEnabled(bool bEnabled);
	void SetActorVisibility(EMLDeformerEditorActorIndex ActorIndex, bool bIsVisible);
	void SetDefaultDeformerGraphIfNeeded();
	bool IsActorVisible(EMLDeformerEditorActorIndex ActorIndex) const;

public:
	TArray<FMatrix44f> CurrentBoneMatrices;
	TArray<FTransform> CurrentBoneTransforms;
	TArray<float> CurrentCurveWeights;

	TArray<FVector3f> LinearSkinnedPositions;
	TArray<FVector3f> GeomCachePositions;
	TArray<FVector3f> TempVectorBuffer;
	TArray<float> VertexDeltas;
	bool bIsVertexDeltaNormalized = false;
	TArray<FGeometryCacheMeshData> MeshDatas;

	int32 CurrentFrame = -1;

private:
	/** The editor actors. */
	TArray<FMLDeformerEditorActor> Actors;

	/** Preview scene to be supplied by IHasPersonaToolkit::GetPersonaToolkit. */
	TSharedPtr<IPersonaToolkit> PersonaToolkit;

	/** The ML Deformer asset we are editing. */
	TWeakObjectPtr<UMLDeformerAsset> MLDeformerAsset;

	/** Viewport anim instance, which is the anim instance playing on the linear skinned mesh. */
	TObjectPtr<UAnimInstance> AnimInstance;

	/** Asset details tab. */
	TSharedPtr<IDetailsView> DetailsView;

	/** Viz settings details tab. */
	TSharedPtr<IDetailsView> VizSettingsDetailsView;

	/** The editor toolkit. */
	TObjectPtr<FMLDeformerEditorToolkit> EditorToolkit;

	/** The heatmap material. */
	TObjectPtr<UMaterial> HeatMapMaterial;

	/** The timeline slider widget. */
	TSharedPtr<SSimpleTimeSlider> TimeSlider;
};
