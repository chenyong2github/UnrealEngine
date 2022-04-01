// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "MLDeformerEditorActor.h"

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

/** Training process return codes. */
UENUM()
enum class ETrainingResult : uint8
{
	Success = 0,	/** The training successfully finished. */
	Aborted,		/** The user has aborted the training process. */
	AbortedCantUse,	/** The user has aborted the training process and we can't use the resulting network. */
	FailOnData,		/** The input or output data to the network has issues, which means we cannot train. */
	FailPythonError,/** The python script has some error (see output log). */
	FailMissingPythonClass,	/** No training model class was implemented in Python, including a valid train function. */
};

namespace UE::MLDeformer
{
	class FMLDeformerEditorToolkit;
	class FMLDeformerEditorActor;
	class FMLDeformerSampler;

	/** The base class for the editor side of an UMLDeformerModel. */
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorModel
		: public FGCObject
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
		virtual int32 GetNumFrames() const PURE_VIRTUAL(FMLDeformerEditorModel::GetNumFrames, return 0;);
		virtual ETrainingResult Train() PURE_VIRTUAL(FMLDeformerEditorModel::Train, return ETrainingResult::Success;);
		virtual double GetTimeAtFrame(int32 FrameNumber) const PURE_VIRTUAL(FMLDeformerEditorModel::GetTimeAtFrame, return 0.0;);
		virtual int32 GetFrameAtTime(double TimeInSeconds) const PURE_VIRTUAL(FMLDeformerEditorModel::GetFrameAtTime, return 0;);

		// Optional overrides.
		virtual void Init(const InitSettings& Settings);
		virtual void CreateActors(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
		virtual void ClearWorld();
		virtual FMLDeformerEditorActor* CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const;
		virtual FMLDeformerSampler* CreateSampler() const;
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
		virtual void OnPlayButtonPressed();
		virtual void OnPreTraining() {}
		virtual void OnPostTraining(ETrainingResult TrainingResult);
		virtual void OnTrainingAborted() {}
		virtual bool IsPlayingAnim() const;
		virtual double CalcTimelinePosition() const;
		virtual void OnTimeSliderScrubPositionChanged(double NewScrubTime, bool bIsScrubbing);
		virtual void UpdateTestAnimPlaySpeed();
		virtual void ClampCurrentFrameIndex();
		virtual int32 GetNumFramesForTraining() const;
		virtual void SetTrainingFrame(int32 FrameNumber);
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
		virtual void SetComputeGraphDataProviders() const;
		virtual void SampleDeltas();
		virtual bool LoadTrainedNetwork() const;
		virtual bool IsTrained() const;
		virtual FMLDeformerEditorActor* GetTimelineEditorActor() const;
		virtual FString GetHeatMapMaterialPath() const;
		virtual FString GetHeatMapDeformerGraphPath() const;
		virtual FString GetDefaultDeformerGraphAssetPath() const;
		virtual FString GetTrainedNetworkOnnxFile() const;

		FMLDeformerEditorToolkit* GetEditor() const { return Editor; }
		UMLDeformerModel* GetModel() const { return Model.Get(); }
		UWorld* GetWorld() const;
		const TArray<FMLDeformerEditorActor*>& GetEditorActors() const { return EditorActors; }
		FMLDeformerEditorActor* FindEditorActor(int32 ActorTypeID) const;
		bool IsReadyForTraining() const { return bIsReadyForTraining; }
		FMLDeformerSampler* GetSampler() const { return Sampler; }
		void SetDataNormalized(bool IsNormalized) { bIsDataNormalized = IsNormalized; }
		bool IsDataNormalized() const { return bIsDataNormalized; }

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

		UNeuralNetwork* LoadNeuralNetworkFromOnnx(const FString& Filename) const;
		int32 GetCurrentTrainingFrame() const { return CurrentTrainingFrame; }
		void CheckTrainingDataFrameChanged();

	protected:
		void DeleteEditorActors();
		bool IsEditorReadyForTrainingBasicChecks();

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

		/** The current training frame. */
		int32 CurrentTrainingFrame = -1;

		/**
		 * Are we ready for training? 
		 * The training button in the editor will be enabled or disabled based on this on default.
		 */
		bool bIsReadyForTraining = false;	

		/** Did we already normalize the data needed for training? */
		bool bIsDataNormalized = false;
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
			return ETrainingResult::FailMissingPythonClass;
		}

		// Perform the training.
		// This will trigger the Python class train function to be called.
		TrainingModelClass* TrainingModel = Cast<TrainingModelClass>(TrainingModels.Last()->GetDefaultObject());
		TrainingModel->Init(EditorModel);
		const int32 ReturnCode = TrainingModel->Train();
		return static_cast<ETrainingResult>(ReturnCode);
	}
}	// namespace UE::MLDeformer
