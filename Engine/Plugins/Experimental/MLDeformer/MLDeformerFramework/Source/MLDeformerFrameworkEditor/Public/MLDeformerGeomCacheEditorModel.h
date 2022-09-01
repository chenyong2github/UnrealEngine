// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerEditorModel.h"

class UMLDeformerGeomCacheModel;
class UMLDeformerGeomCacheVizSettings;

namespace UE::MLDeformer
{
	class FMLDeformerGeomCacheActor;
	class FMLDeformerGeomCacheSampler;

	/**
	 * An editor model that is based on a geometry cache as training input.
	 */
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerGeomCacheEditorModel
		: public FMLDeformerEditorModel
	{
	public:
		// FGCObject overrides.
		virtual FString GetReferencerName() const override { return TEXT("FMLDeformerGeomCacheEditorModel"); }
		// ~END FGCObject overrides.

		// FMLDeformerEditorModel overrides.
		virtual FMLDeformerEditorActor* CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const override;
		virtual FMLDeformerSampler* CreateSampler() const override;
		virtual void CreateTrainingGroundTruthActor(UWorld* World) override;
		virtual void CreateTestGroundTruthActor(UWorld* World) override;
		virtual int32 GetNumTrainingFrames() const override;
		virtual double GetTrainingTimeAtFrame(int32 FrameNumber) const override;
		virtual int32 GetTrainingFrameAtTime(double TimeInSeconds) const override;
		virtual double GetTestTimeAtFrame(int32 FrameNumber) const override;
		virtual int32 GetTestFrameAtTime(double TimeInSeconds) const override;
		virtual void UpdateIsReadyForTrainingState() override;
		virtual void OnPropertyChanged(FPropertyChangedEvent& PropertyChangedEvent) override;
		virtual void OnInputAssetsChanged() override;
		virtual ETrainingResult Train() override;
		// ~END FMLDeformerEditorModel overrides.
		
		// Helpers.
		FMLDeformerGeomCacheSampler* GetGeomCacheSampler() const;
		UMLDeformerGeomCacheModel* GetGeomCacheModel() const;
		UMLDeformerGeomCacheVizSettings* GetGeomCacheVizSettings() const;
		FMLDeformerGeomCacheActor* FindGeomCacheEditorActor(int32 ID) const;

	protected:
		void CreateGeomCacheActor(UWorld* World, int32 ActorID, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor, const FText& LabelText, bool bIsTrainingActor);
	};
}	// namespace UE::MLDeformer
