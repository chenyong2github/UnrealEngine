// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNewEmitterDialog.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditor/Private/SNiagaraAssetPickerList.h"
#include "SNiagaraNewAssetDialog.h"

#include "AssetData.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

typedef SItemSelector<FText, FAssetData> SNiagaraAssetItemSelector;

#define LOCTEXT_NAMESPACE "SNewEmitterDialog"

void SNewEmitterDialog::Construct(const FArguments& InArgs)
{
	FNiagaraAssetPickerListViewOptions DisplayAllViewOptions;
	DisplayAllViewOptions.SetExpandTemplateAndLibraryAssets(true);
	DisplayAllViewOptions.SetCategorizeLibraryAssets(true);
	DisplayAllViewOptions.SetAddLibraryOnlyCheckbox(true);

	FNiagaraAssetPickerTabOptions TabOptions;
	TabOptions.ChangeTabState(ENiagaraScriptTemplateSpecification::Template, true);
	TabOptions.ChangeTabState(ENiagaraScriptTemplateSpecification::None, true);
	TabOptions.ChangeTabState(ENiagaraScriptTemplateSpecification::Behavior, true);

	SAssignNew(TemplateAssetPicker, SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
		.ViewOptions(DisplayAllViewOptions)
		.TabOptions(TabOptions);

	SAssignNew(CopyAssetPicker, SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
		.ViewOptions(DisplayAllViewOptions)
		.TabOptions(TabOptions);
	
	SNiagaraNewAssetDialog::Construct(SNiagaraNewAssetDialog::FArguments(), UNiagaraEmitter::StaticClass()->GetFName(), LOCTEXT("AssetTypeName", "emitter"),
		{
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromEmitterLabel", "New emitter"),
				LOCTEXT("CreateFromEmitterDescription", "Create a new emitter from a template or behavior emitter (no inheritance) or from a parent (inheritance)"),
				LOCTEXT("EmitterPickerHeader", "Select an Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedEmitterTemplateAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				TemplateAssetPicker.ToSharedRef(), TemplateAssetPicker->GetSearchBox() 
				),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromOtherEmitterLabel", "Copy existing emitter"),
				LOCTEXT("CreateFromOtherEmitterDescription", "Copies an existing emitter from your project content"),
				LOCTEXT("ProjectEmitterPickerHeader", "Select a Project Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedProjectEmiterAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				CopyAssetPicker.ToSharedRef(), CopyAssetPicker->GetSearchBox()
				)
		});
}

TOptional<FAssetData> SNewEmitterDialog::GetSelectedEmitterAsset()
{
	const TArray<FAssetData>& SelectedEmitterAssets = GetSelectedAssets();
	if (SelectedEmitterAssets.Num() > 0)
	{
		return TOptional<FAssetData>(SelectedEmitterAssets[0]);
	}
	return TOptional<FAssetData>();
}

bool SNewEmitterDialog::GetUseInheritance() const
{
	return bUseInheritance;
}

void SNewEmitterDialog::GetSelectedEmitterTemplateAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(TemplateAssetPicker->GetSelectedAssets());
}

void SNewEmitterDialog::GetSelectedParentEmitterAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(InheritAssetPicker->GetSelectedAssets());
}

void SNewEmitterDialog::GetSelectedProjectEmiterAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(CopyAssetPicker->GetSelectedAssets());
}


void SNewEmitterDialog::InheritanceOptionConfirmed()
{
	bUseInheritance = true;
}

#undef LOCTEXT_NAMESPACE
