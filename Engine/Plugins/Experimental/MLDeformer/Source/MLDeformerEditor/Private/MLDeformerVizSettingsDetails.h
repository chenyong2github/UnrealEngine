// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "PropertyHandle.h"

class IDetailLayoutBuilder;
class USkeleton;
class UMLDeformerAsset;

class FMLDeformerVizSettingsDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface. */
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;

	IDetailLayoutBuilder* GetDetailLayoutBuilder() const { return DetailLayoutBuilder; }

private:
	bool FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton);
	void OnResetToDefaultDeformerGraph(TSharedPtr<IPropertyHandle> PropertyHandle);
	bool IsResetToDefaultDeformerGraphVisible(TSharedPtr<IPropertyHandle> PropertyHandle);
	UMLDeformerAsset* GetMLDeformerAsset() const;

private:
	/** Associated detail layout builder. */
	IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;
};
