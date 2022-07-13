// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerGeomCacheSampler.h"
#include "VertexDeltaModel.h"
#include "MLDeformerEditorActor.h"
#include "UObject/GCObject.h"

class UMLDeformerAsset;
class USkeletalMesh;
class UGeometryCache;
class UVertexDeltaModel;
class UVertexDeltaModelVizSettings;

namespace UE::VertexDeltaModel
{
	using namespace UE::MLDeformer;

	class FVertexDeltaEditorModelActor;
	class FVertexDeltaModelSampler;

	class VERTEXDELTAMODELEDITOR_API FVertexDeltaEditorModel 
		: public UE::MLDeformer::FMLDeformerEditorModel
	{
	public:
		virtual ~FVertexDeltaEditorModel() override;

		// We need to implement this static MakeInstance method.
		static FMLDeformerEditorModel* MakeInstance();

		// FGCObject overrides.
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("FVertexDeltaEditorModel"); }
		// ~END FGCObject overrides.
	
		// FMLDeformerEditorModel overrides.
		virtual void Init(const InitSettings& Settings) override;
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
		FString GetTrainedNetworkOnnxFile() const override;
		// ~END FMLDeformerEditorModel overrides.

		// Some helpers that cast to this model's variants of some classes.
		UVertexDeltaModel* GetVertexDeltaModel() const { return Cast<UVertexDeltaModel>(Model); }
		UVertexDeltaModelVizSettings* GetVertexDeltaModelVizSettings() const;
		FVertexDeltaEditorModelActor* FindVertexDeltaModelEditorActor(int32 TypeID) const;
		FMLDeformerGeomCacheSampler* GetGeomCacheSampler() const { return static_cast<FMLDeformerGeomCacheSampler*>(Sampler); }
	
		void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);

	protected:
		void CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor);
	};
}	// namespace UE::VertexDeltaModel
