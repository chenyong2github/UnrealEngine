// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "PropertyHandle.h"

class IDetailCategoryBuilder;
class IDetailLayoutBuilder;
class USkeleton;
class UMLDeformerModel;
class UMLDeformerVizSettings;

namespace UE::MLDeformer
{
	class FMLDeformerEditorModel;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerVizSettingsDetails
		: public IDetailCustomization
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
		static TSharedRef<IDetailCustomization> MakeInstance();

		// ILayoutDetails overrides.
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
		// ~END ILayoutDetails overrides.

		IDetailLayoutBuilder* GetDetailLayoutBuilder() const { return DetailLayoutBuilder; }

	protected:
		bool FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton);
		void OnResetToDefaultDeformerGraph(TSharedPtr<IPropertyHandle> PropertyHandle);
		bool IsResetToDefaultDeformerGraphVisible(TSharedPtr<IPropertyHandle> PropertyHandle);

		virtual bool UpdateMemberPointers(const TArray<TWeakObjectPtr<UObject>>& Objects);
		virtual void CreateCategories();
		virtual void AddTestSequenceErrors() {}
		virtual void AddDeformerGraphErrors() {}
		virtual void AddGroundTruth() {}
		virtual void AddAdditionalSettings() {}

	protected:
		/** Associated detail layout builder. */
		IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;

		TObjectPtr<UMLDeformerModel> Model = nullptr;
		TObjectPtr<UMLDeformerVizSettings> VizSettings = nullptr;
		FMLDeformerEditorModel* EditorModel = nullptr;

		IDetailCategoryBuilder* SharedCategoryBuilder = nullptr;
		IDetailCategoryBuilder* TestAssetsCategory = nullptr;
		IDetailCategoryBuilder* LiveSettingsCategory = nullptr;
		IDetailCategoryBuilder* TrainingMeshesCategoryBuilder = nullptr;
	};
}	// namespace UE::MLDeformer
