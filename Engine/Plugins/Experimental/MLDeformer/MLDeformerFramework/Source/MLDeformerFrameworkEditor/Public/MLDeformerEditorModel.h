// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "MLDeformerEditorActor.h"
#include "Misc/FrameTime.h"
#include "MLDeformerVizSettings.h"

class UMLDeformerModel;
class UMLDeformerInputInfo;
class AAnimationEditorPreviewActor;
class IPersonaPreviewScene;
class FEditorViewportClient;
class FSceneView;
class FViewport;

class FPrimitiveDrawInterface;
class UWorld;
class UMeshDeformer;
class USkeletalMesh;
class UAnimSequence;
class UMaterial;
class UGeometryCache;
class UNeuralNetwork;
class UMorphTarget;
class FMorphTargetVertexInfoBuffers;

/** Training process return codes. */
UENUM()
enum class ETrainingResult : uint8
{
	/** The training successfully finished. */
	Success = 0,

	/** The user has aborted the training process. */
	Aborted,

	/** The user has aborted the training process and we can't use the resulting network. */
	AbortedCantUse,

	/** The input or output data to the network has issues, which means we cannot train. */
	FailOnData,

	/** The python script has some error (see output log). */
	FailPythonError
};

namespace UE::MLDeformer
{
	class FMLDeformerEditorToolkit;
	class FMLDeformerEditorActor;
	class FMLDeformerSampler;

	/** The base class for the editor side of an UMLDeformerModel. */
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorModel
		: public TSharedFromThis<FMLDeformerEditorModel>, public FGCObject
	{
	public:
		/** 
		 * The editor model initialization settings.
		 * This is used in the Init call.
		 */
		struct MLDEFORMERFRAMEWORKEDITOR_API InitSettings
		{
			FMLDeformerEditorToolkit* Editor = nullptr;
			UMLDeformerModel* Model = nullptr;
		};

		virtual ~FMLDeformerEditorModel();

		// Required overrides.
		virtual int32 GetNumTrainingFrames() const PURE_VIRTUAL(FMLDeformerEditorModel::GetNumTrainingFrames, return 0;);
		virtual ETrainingResult Train() PURE_VIRTUAL(FMLDeformerEditorModel::Train, return ETrainingResult::Success;);

		// Optional overrides.
		virtual void Init(const InitSettings& Settings);
		virtual void CreateActors(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
		virtual void OnPostCreateActors() {}
		virtual void ClearWorld();
		virtual FMLDeformerEditorActor* CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const;
		virtual FMLDeformerSampler* CreateSampler() const;
		virtual double GetTrainingTimeAtFrame(int32 FrameNumber) const;
		virtual int32 GetTrainingFrameAtTime(double TimeInSeconds) const;
		virtual double GetTestTimeAtFrame(int32 FrameNumber) const;
		virtual int32 GetTestFrameAtTime(double TimeInSeconds) const;
		virtual int32 GetNumTestFrames() const;
		virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime);
		virtual void CreateTrainingLinearSkinnedActor(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
		virtual void CreateTestLinearSkinnedActor(UWorld* World);
		virtual void CreateTestMLDeformedActor(UWorld* World);
		virtual void CreateTrainingGroundTruthActor(UWorld* World) {}
		virtual void CreateTestGroundTruthActor(UWorld* World) {}
		virtual void OnTrainingDataFrameChanged();
		virtual void UpdateActorTransforms();
		virtual void UpdateActorVisibility();
		virtual void UpdateLabels();
		virtual void OnInputAssetsChanged();
		virtual void OnPostInputAssetChanged();
		virtual void HandleDefaultPropertyChanges(FPropertyChangedEvent& PropertyChangedEvent);
		virtual void OnPlayPressed();
		virtual void OnPreTraining() {}
		virtual void OnPostTraining(ETrainingResult TrainingResult, bool bUsePartiallyTrainedWhenAborted);
		virtual void OnTrainingAborted(bool bUsePartiallyTrainedData) {}
		virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {}
		virtual bool IsPlayingAnim() const;
		virtual bool IsPlayingForward() const;
		virtual double CalcTrainingTimelinePosition() const;
		virtual double CalcTestTimelinePosition() const;
		virtual void OnTimeSliderScrubPositionChanged(double NewScrubTime, bool bIsScrubbing);
		virtual void UpdateTestAnimPlaySpeed();
		virtual void ClampCurrentTrainingFrameIndex();
		virtual void ClampCurrentTestFrameIndex();
		virtual int32 GetNumFramesForTraining() const;
		virtual void SetTrainingFrame(int32 FrameNumber);
		virtual void SetTestFrame(int32 FrameNumber);
		virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI);
		virtual void UpdateIsReadyForTrainingState() { bIsReadyForTraining = false; }
		virtual FText GetOverlayText() const;
		virtual void InitInputInfo(UMLDeformerInputInfo* InputInfo);
		virtual void RefreshMLDeformerComponents();
		virtual void CreateHeatMapMaterial();
		virtual void CreateHeatMapDeformerGraph();
		virtual void CreateHeatMapAssets();
		virtual void SetHeatMapMaterialEnabled(bool bEnabled);
		virtual UMeshDeformer* LoadDefaultDeformerGraph();
		virtual void SetDefaultDeformerGraphIfNeeded();
		virtual void UpdateDeformerGraph();
		virtual void SampleDeltas();
		virtual bool LoadTrainedNetwork() const;
		virtual bool IsTrained() const;
		virtual FMLDeformerEditorActor* GetTimelineEditorActor() const;
		virtual FString GetHeatMapMaterialPath() const;
		virtual FString GetHeatMapDeformerGraphPath() const;
		virtual FString GetDefaultDeformerGraphAssetPath() const;
		virtual FString GetTrainedNetworkOnnxFile() const;

		/** Get the current view range */
		TRange<double> GetViewRange() const;

		/** Set the current view range */
		void SetViewRange(TRange<double> InRange);

		/** Get the working range of the model's data */
		TRange<double> GetWorkingRange() const;

		/** Get the playback range of the model's data */
		TRange<FFrameNumber> GetPlaybackRange() const;

		/** Get the current scrub position */
		FFrameNumber GetTickResScrubPosition() const;

		int32 GetTicksPerFrame() const;
		/** Get the current scrub time */
		float GetScrubTime() const;

		/** Set the current scrub position */
		void SetScrubPosition(FFrameTime NewScrubPostion);

		/** Set the current scrub position */
		void SetScrubPosition(FFrameNumber NewScrubPostion);

		/** Set if frames are displayed */
		void SetDisplayFrames(bool bDisplayFrames);

		bool IsDisplayingFrames() const;

		void HandleModelChanged();
		void HandleVizModeChanged(EMLDeformerVizMode Mode);

		/** Handle the view range being changed */
		void HandleViewRangeChanged(TRange<double> InRange);

		/** Handle the working range being changed */
		void HandleWorkingRangeChanged(TRange<double> InRange);

		/** Get the framerate specified by the anim sequence */
		double GetFrameRate() const;

		/** Get the tick resolution we are displaying at */
		int32 GetTickResolution() const;

		FMLDeformerEditorToolkit* GetEditor() const { return Editor; }
		UMLDeformerModel* GetModel() const { return Model.Get(); }
		UWorld* GetWorld() const;
		const TArray<FMLDeformerEditorActor*>& GetEditorActors() const { return EditorActors; }
		FMLDeformerEditorActor* FindEditorActor(int32 ActorTypeID) const;
		bool IsReadyForTraining() const { return bIsReadyForTraining; }
		FMLDeformerSampler* GetSampler() const { return Sampler; }
		void SetResamplingInputOutputsNeeded(bool bNeeded) { bNeedToResampleInputOutputs = bNeeded; }
		bool GetResamplingInputOutputsNeeded() const { return bNeedToResampleInputOutputs; }

		FText GetBaseAssetChangedErrorText() const;
		FText GetVertexMapChangedErrorText() const;
		FText GetInputsErrorText() const;
		FText GetIncompatibleSkeletonErrorText(USkeletalMesh* InSkelMesh, UAnimSequence* InAnimSeq) const;
		FText GetSkeletalMeshNeedsReimportErrorText() const;
		FText GetTargetAssetChangedErrorText() const;

		UMLDeformerInputInfo* GetEditorInputInfo() const { return EditorInputInfo.Get(); }	
		void UpdateEditorInputInfo();

		void TriggerInputAssetChanged(bool RefreshVizSettings=false);

		void InitBoneIncludeListToAnimatedBonesOnly();
		void InitCurveIncludeListToAnimatedCurvesOnly();
		int32 GetNumCurvesOnSkeletalMesh(USkeletalMesh* SkelMesh) const;

		UNeuralNetwork* LoadNeuralNetworkFromOnnx(const FString& Filename) const;
		int32 GetCurrentTrainingFrame() const { return CurrentTrainingFrame; }
		void CheckTrainingDataFrameChanged();

		/**
		 * Debug draw specific morph targets using lines and points.
		 * This can show the user what deltas are included in which morph target.
		 * @param PDI A pointer to the draw interface.
		 * @param MorphDeltas A buffer of deltas for ALL morph targets. The size of the buffer must be a multiple of Model->GetBaseNumVerts().
		 *        So the layout of this buffer is [Morph0_Deltas][Morph1_Deltas][Morph2_Deltas] etc.
		 * @param DeltaThreshold Deltas with a length  larger or equal to the given threshold value will be colored differently than the ones smaller than this threshold.
		 * @param MorphTargetIndex The morph target number to visualize.
		 * @param DrawOffset An offset to perform the debug draw at.
		 */
		void DrawMorphTarget(FPrimitiveDrawInterface* PDI, const TArray<FVector3f>& MorphDeltas, float DeltaThreshold, int32 MorphTargetIndex, const FVector& DrawOffset);

	protected:
		void DeleteEditorActors();
		bool IsEditorReadyForTrainingBasicChecks();

		/** Zero all deltas with a length equal to, or smaller than the threshold value. */
		void ZeroDeltasByThreshold(TArray<FVector3f>& Deltas, float Threshold);

		/**
		 * Generate engine morph targets from a set of deltas.
		 * @param OutMorphTargets The output array with generated morph targets. This array will be reset, and then filled with generated morph targets.
		 * @param Deltas The per vertex deltas for all morph targets, as one big buffer. Each morph target has 'GetNumBaseMeshVerts()' number of deltas.
		 * @param NamePrefix The morph target name prefix. If set to "MorphTarget_" the names will be "MorphTarget_000", "MorphTarget_001", "MorphTarget_002", etc.
		 * @param LOD The LOD index to generate the morphs for.
		 * @param DeltaThreshold Only include deltas with a length larger than this threshold in the morph targets.
		 */
		void CreateEngineMorphTargets(TArray<UMorphTarget*>& OutMorphTargets, const TArray<FVector3f>& Deltas, const FString& NamePrefix = TEXT("MorphTarget_"), int32 LOD = 0, float DeltaThreshold = 0.01f);

		/** 
		 * Compress morph targets into GPU based morph buffers.
		 * @param OutMorphBuffers The output compressed GPU based morph buffers. If this buffer is already initialized it will be released first.
		 * @param MorphTargets The morph targets to compress into GPU friendly buffers.
		 * @param LOD The LOD index to generate the morphs for.
		 * @param MorphErrorTolerance The error tolerance for the delta compression, in cm. Higher values compress better but can result in artifacts.
		 */
		void CompressEngineMorphTargets(FMorphTargetVertexInfoBuffers& OutMorphBuffers, const TArray<UMorphTarget*>& MorphTargets, int32 LOD = 0, float MorphErrorTolerance = 0.01f);

		FMLDeformerEditorActor* GetVisualizationModeBaseActor() const;

		const UAnimSequence* GetAnimSequence() const;

		double CalcTimelinePosition() const;

		void UpdateRanges();
	protected:
		/** The runtime model associated with this editor model. */
		TObjectPtr<UMLDeformerModel> Model = nullptr;

		/** The set of actors that can appear inside the editor viewport. */
		TArray<FMLDeformerEditorActor*> EditorActors;

		/** A pointer to the editor toolkit. */
		FMLDeformerEditorToolkit* Editor = nullptr;

		/** A pointer to the sampler. */
		FMLDeformerSampler* Sampler = nullptr;

		/**
		 * The input info as currently setup in the editor.
		 * This is different from the runtime model's input info, as that is the one that was used to train with.
		 */
		TObjectPtr<UMLDeformerInputInfo> EditorInputInfo = nullptr;

		/** The heatmap material. */
		TObjectPtr<UMaterial> HeatMapMaterial = nullptr;

		/** The heatmap deformer graph. */
		TObjectPtr<UMeshDeformer> HeatMapDeformerGraph = nullptr;

		/** The delegate handle to the post edit property event. */
		FDelegateHandle PostEditPropertyDelegateHandle;

		/** The current training frame. */
		int32 CurrentTrainingFrame = -1;

		/**
		 * Are we ready for training? 
		 * The training button in the editor will be enabled or disabled based on this on default.
		 */
		bool bIsReadyForTraining = false;	

		/** Do we need to resample all input/output data? */
		bool bNeedToResampleInputOutputs = true;

		/** The range we are currently viewing */
		TRange<double> ViewRange;

		/** The working range of this model, encompassing the view range */
		TRange<double> WorkingRange;

		/** The playback range of this model for each timeframe */
		TRange<double> PlaybackRange;

		FFrameTime ScrubPosition;

		bool bDisplayFrames = true;
	};

	template<class TrainingModelClass>
	ETrainingResult TrainModel(FMLDeformerEditorModel* EditorModel)
	{
		// Find class, which will include our Python class, generated from the python script.
		TArray<UClass*> TrainingModels;
		GetDerivedClasses(TrainingModelClass::StaticClass(), TrainingModels);
		if (TrainingModels.IsEmpty())
		{
			// We didn't define a derived class in Python.
			return ETrainingResult::FailPythonError;
		}

		// Perform the training.
		// This will trigger the Python class train function to be called.
		TrainingModelClass* TrainingModel = Cast<TrainingModelClass>(TrainingModels.Last()->GetDefaultObject());
		TrainingModel->Init(EditorModel);
		const int32 ReturnCode = TrainingModel->Train();
		return static_cast<ETrainingResult>(ReturnCode);
	}
}	// namespace UE::MLDeformer
