// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SNiagaraNewAssetDialog.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "AssetData.h"
#include "ContentBrowserDelegates.h"

class SNiagaraAssetPickerList;

/** A modal dialog to collect information needed to create a new niagara system. */
class SNewEmitterDialog : public SNiagaraNewAssetDialog
{
public:
	SLATE_BEGIN_ARGS(SNewEmitterDialog)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TOptional<FAssetData> GetSelectedEmitterAsset();

	bool GetUseInheritance() const;

private:
	void GetSelectedEmitterTemplateAssets(TArray<FAssetData>& OutSelectedAssets);

	void GetSelectedParentEmitterAssets(TArray<FAssetData>& OutSelectedAssets);

	void GetSelectedProjectEmiterAssets(TArray<FAssetData>& OutSelectedAssets);

	void InheritanceOptionConfirmed();

private:
	TSharedPtr<SNiagaraAssetPickerList> TemplateAssetPicker;
	TSharedPtr<SNiagaraAssetPickerList> InheritAssetPicker;
	TSharedPtr<SNiagaraAssetPickerList> CopyAssetPicker;

	bool bUseInheritance;
};