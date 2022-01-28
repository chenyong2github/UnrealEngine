// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerFrameCache.h"

class UAnimInstance;
class UMLDeformerAsset;
class UMLDeformerComponent;
class UMeshDeformer;
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
class UWorld;
class FMLDeformerFrameCache;
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
	void SetWorld(UWorld* InWorld);

	int32 GetNumEditorActors() const;
	void SetEditorActor(EMLDeformerEditorActorIndex Index, const FMLDeformerEditorActor& Actor);
	const FMLDeformerEditorActor& GetEditorActor(EMLDeformerEditorActorIndex Index) const;
	FMLDeformerEditorActor& GetEditorActor(EMLDeformerEditorActorIndex Index);
	bool IsTestActor(EMLDeformerEditorActorIndex Index) const;
	bool IsTrainingActor(EMLDeformerEditorActorIndex Index) const;

	void InitAssets();
	bool GenerateDeltas(uint32 LODIndex, uint32 FrameNumber, TArray<float>& OutDeltas);
	bool ComputeVertexDeltaStatistics(uint32 LODIndex, FMLDeformerFrameCache* FrameCache);

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
	UWorld* GetWorld() const;

	float GetDuration() const;
	float GetSnappedFrameTime(float InTime) const;
	float GetTimeAtFrame(int32 FrameNumber) const;

	/**
	 *Update mean and scale of vertex deltas.
	 *
	 * @param TrainingFrame The training frame that contains the vertex deltas.
	 * @param InOutMeanVertexDelta Mean vertex delta.
	 * @param InOutVertexDeltaScale Vertex delta scale.
	 * @param InOutCount Count.
	 */
	static void UpdateVertexDeltaMeanAndScale(const FMLDeformerTrainingFrame& TrainingFrame, FVector3f& InOutMeanVertexDelta, FVector3f& InOutVertexDeltaScale, float& InOutCount);

	void UpdateTestAnimPlaySpeed();
	void UpdateDeformerGraph();
	static FString GetDefaultDeformerGraphAssetPath();
	static UMeshDeformer* LoadDefaultDeformerGraph();
	void SetAnimFrame(int32 FrameNumber);
	void CreateHeatMapAssets();
	void ClampFrameIndex();

	void UpdateIsReadyForTrainingState();
	bool IsReadyForTraining() const;
	FText GetOverlayText();

	void SetHeatMapMaterialEnabled(bool bEnabled);
	void SetActorVisibility(EMLDeformerEditorActorIndex ActorIndex, bool bIsVisible);
	void SetDefaultDeformerGraphIfNeeded();
	bool IsActorVisible(EMLDeformerEditorActorIndex ActorIndex) const;

	FMLDeformerFrameCache& GetSingleFrameCache() { return SingleFrameCache; }
	const FMLDeformerFrameCache& GetSingleFrameCache() const { return SingleFrameCache; }
	
public:
	TArray<FVector3f> LinearSkinnedPositions;
	TArray<FVector3f> DebugVectors;
	TArray<FVector3f> DebugVectors2;
	TArray<float> VertexDeltas;

	FVector3f VertexDeltaMeanBackup = FVector3f::ZeroVector;
	FVector3f VertexDeltaScaleBackup = FVector3f::OneVector;

	bool bIsVertexDeltaNormalized = false;
	bool bIsReadyForTraining = false;
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

	/** The heatmap material. */
	TObjectPtr<UMeshDeformer> HeatMapDeformerGraph;

	/** The world that our actors are inside. */
	TObjectPtr<UWorld> World;

	/** The timeline slider widget. */
	TSharedPtr<SSimpleTimeSlider> TimeSlider;

	/** Single frame cache, used to calculate the training data for the current frame in the timeline. */
	FMLDeformerFrameCache SingleFrameCache;
};
