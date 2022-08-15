// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerGeomCacheSampler.h"
#include "MLDeformerEditorActor.h"
#include "NeuralMorphModel.h"
#include "UObject/GCObject.h"

class UMLDeformerAsset;
class USkeletalMesh;
class UGeometryCache;
class UNeuralMorphModel;
class UNeuralMorphModelVizSettings;

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	class FNeuralMorphEditorModelActor;

	class NEURALMORPHMODELEDITOR_API FNeuralMorphEditorModel
		: public UE::MLDeformer::FMLDeformerEditorModel
	{
	public:
		virtual ~FNeuralMorphEditorModel() override;

		// We need to implement this static MakeInstance method.
		static FMLDeformerEditorModel* MakeInstance();

		// FGCObject overrides.
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("FNeuralMorphEditorModel"); }
		// ~END FGCObject overrides.
	
		// FMLDeformerEditorModel overrides.
		virtual FMLDeformerEditorActor* CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const override;
		virtual FMLDeformerSampler* CreateSampler() const;
		virtual void CreateTrainingGroundTruthActor(UWorld* World) override;
		virtual void CreateTestGroundTruthActor(UWorld* World) override;
		virtual void OnInputAssetsChanged() override;
		virtual int32 GetNumTrainingFrames() const override;
		virtual double GetTrainingTimeAtFrame(int32 FrameNumber) const override;
		virtual int32 GetTrainingFrameAtTime(double TimeInSeconds) const override;
		virtual double GetTestTimeAtFrame(int32 FrameNumber) const override;
		virtual int32 GetTestFrameAtTime(double TimeInSeconds) const override;
		virtual void UpdateIsReadyForTrainingState() override;
		virtual ETrainingResult Train() override;
		virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
		virtual FString GetTrainedNetworkOnnxFile() const override;
		virtual FString GetDefaultDeformerGraphAssetPath() const override;
		virtual FString GetHeatMapDeformerGraphPath() const override;
		virtual void OnPreTraining() override;
		virtual void OnPostTraining(ETrainingResult TrainingResult, bool bUsePartiallyTrainedWhenAborted) override;
		virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
		// ~END FMLDeformerEditorModel overrides.

		// Some helpers that cast to this model's variants of some classes.
		UNeuralMorphModel* GetNeuralMorphModel() const { return Cast<UNeuralMorphModel>(Model); }
		UNeuralMorphModelVizSettings* GetNeuralMorphModelVizSettings() const;
		FNeuralMorphEditorModelActor* FindNeuralMorphModelEditorActor(int32 TypeID) const;
		FMLDeformerGeomCacheSampler* GetGeomCacheSampler() const { return static_cast<FMLDeformerGeomCacheSampler*>(Sampler); }
	

	protected:
		void CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor);

		/**
		 * Initialize a set of engine morph targets and compress them to GPU friendly buffers.
		 * These morph targets are initialized from a set of deltas. Each morph target needs to have Model->GetNumBaseVerts() number of deltas.
		 * All deltas are concatenated in one big array. So all deltas of all vertices for the second morph target are appended to the deltas for 
		 * the first morph target, etc. In other words, the layout is: [morph0_deltas][morph1_deltas][morph2_deltas][...].
		 * @param Deltas The deltas for all morph targets concatenated. So the number of items in this array is a multiple of Model->GetNumBaseVerts().
		 */
		void InitEngineMorphTargets(const TArray<FVector3f>& Deltas);

	protected:
		/**
		 * The entire set of morph target deltas, 3 per vertex, for each morph target, as one flattened buffer.
		 * So the size of this buffer is: (NumVertsPerMorphTarget * NumMorphTargets).
		 */
		TArray<FVector3f> MorphTargetDeltasBackup;
	};
}	// namespace UE::NeuralMorphModel
