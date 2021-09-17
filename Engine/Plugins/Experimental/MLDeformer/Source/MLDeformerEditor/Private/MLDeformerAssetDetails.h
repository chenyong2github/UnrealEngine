// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"

class FMLDeformerEditorData;
class IDetailLayoutBuilder;
class USkeleton;

class FMLDeformerAssetDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** ILayoutDetails interface. */
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;

	void SetTargetMeshErrorText(const FText& Text) { TargetMeshErrorText = Text; }

private:
	bool FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton);

private:
	/** Associated detail layout builder. */
	IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;

	/** A pointer to the editor data. */
	FMLDeformerEditorData* EditorData = nullptr;

	/** The error text to show in the target mesh category. Empty if no error. */
	FText TargetMeshErrorText;
};
