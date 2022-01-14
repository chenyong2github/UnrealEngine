// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "AssetRegistry/AssetData.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class USkeleton;
class UMLDeformerAsset;

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
	FReply OnFilterAnimatedBonesOnly(UMLDeformerAsset* DeformerAsset) const;
	FReply OnFilterAnimatedCurvesOnly(UMLDeformerAsset* DeformerAsset) const;

private:
	/** Associated detail layout builder. */
	IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;

	/** The error text to show in the target mesh category. Empty if no error. */
	FText TargetMeshErrorText;
};
