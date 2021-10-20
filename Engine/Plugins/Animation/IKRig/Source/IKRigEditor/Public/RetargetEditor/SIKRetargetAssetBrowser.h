// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ContentBrowserDelegates.h"
#include "Widgets/Layout/SBox.h"


class FIKRetargetEditorController;

class SIKRetargetAssetBrowser : public SBox
{
public:
	SLATE_BEGIN_ARGS(SIKRetargetAssetBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FIKRetargetEditorController> InEditorController);

private:

	void AddAssetBrowser();

	void OnPathChange(const FString& NewPath);

	FReply OnExportButtonClicked() const;
	bool IsExportButtonEnabled() const;
	
	void OnAssetDoubleClicked(const FAssetData& AssetData);
	bool OnShouldFilterAsset(const struct FAssetData& AssetData);

	/** Used to get the currently selected assets */
	FGetCurrentSelectionDelegate GetCurrentSelectionDelegate;
		
	/** editor controller */
	TWeakPtr<FIKRetargetEditorController> EditorController;

	/** the animation asset browser */
	TSharedPtr<SBox> AssetBrowserBox;

	/** the path to export assets to */
	FString BatchOutputPath = "";

	friend FIKRetargetEditorController;
};
