// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformerEditorActor.h"

class UMLDeformerComponent;
class UGeometryCacheComponent;

namespace UE::NeuralMorphModel
{
	using namespace UE::MLDeformer;

	class NEURALMORPHMODELEDITOR_API FNeuralMorphEditorModelActor
		: public FMLDeformerEditorActor
	{
	public:
		FNeuralMorphEditorModelActor(const FConstructSettings& Settings);

		void SetGeometryCacheComponent(UGeometryCacheComponent* Component) { GeomCacheComponent = Component; }
		UGeometryCacheComponent* GetGeometryCacheComponent() const { return GeomCacheComponent; }

		// FMLDeformerEditorActor overrides.
		virtual void SetVisibility(bool bIsVisible) override;
		virtual bool IsVisible() const override;
		virtual bool HasVisualMesh() const override;
		virtual void SetPlayPosition(float TimeInSeconds, bool bAutoPause=true) override;
		virtual float GetPlayPosition() const override;
		virtual void SetPlaySpeed(float PlaySpeed) override;
		virtual void Pause(bool bPaused) override;
		virtual FBox GetBoundingBox() const override;
		// ~END FMLDeformerEditorActor overrides.

	protected:
		/** The geometry cache component (can be nullptr). */
		TObjectPtr<UGeometryCacheComponent> GeomCacheComponent = nullptr;
	};
}	// namespace UE::NeuralMorphModel
