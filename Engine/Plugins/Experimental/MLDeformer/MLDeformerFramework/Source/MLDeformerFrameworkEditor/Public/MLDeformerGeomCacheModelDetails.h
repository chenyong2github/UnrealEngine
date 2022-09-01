// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModelDetails.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UMLDeformerGeomCacheModel;

namespace UE::MLDeformer
{
	class FMLDeformerGeomCacheEditorModel;
	class FMLDeformerEditorModel;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerGeomCacheModelDetails
		: public FMLDeformerModelDetails
	{
	public:
		// FMLDeformerModelDetails overrides.
		virtual bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects) override;
		virtual void AddTargetMesh() override;
		virtual void AddAnimSequenceErrors() override;
		// ~END FMLDeformerModelDetails overrides.

	protected:
		TObjectPtr<UMLDeformerGeomCacheModel> GeomCacheModel = nullptr;
		FMLDeformerGeomCacheEditorModel* GeomCacheEditorModel = nullptr;
	};
}	// namespace UE::MLDeformer
