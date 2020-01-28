// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNewEmitterDialog.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorStyle.h"
#include "SNiagaraAssetPickerList.h"
#include "SNiagaraNewAssetDialog.h"

#include "AssetData.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

typedef SItemSelector<FText, FAssetData> SNiagaraAssetItemSelector;

#define LOCTEXT_NAMESPACE "SNewEmitterDialog"

void SNewEmitterDialog::Construct(const FArguments& InArgs)
{
	SNiagaraNewAssetDialog::Construct(SNiagaraNewAssetDialog::FArguments(), UNiagaraEmitter::StaticClass()->GetFName(), LOCTEXT("AssetTypeName", "emitter"),
		{
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromTemplateLabel", "New emitter from a template"),
				LOCTEXT("CreateFromTemplateDescription", "Create a new emitter from an emitter template (no inheritance)"),
				LOCTEXT("TemplatesPickerHeader", "Select a Template Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedEmitterTemplateAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SAssignNew(TemplateAssetPicker, SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
				.OnTemplateAssetActivated(this, &SNewEmitterDialog::OnTemplateAssetActivated)
				.bTemplateOnly(true)),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("InheritFromOtherEmitterLabel", "Inherit from an existing emitter"),
				LOCTEXT("InheritFromOtherEmitterDescription", "Create an inheritance chain between the new emitter and an existing emitter"),
				LOCTEXT("InheritProjectEmitterPickerHeader", "Select a Parent Project Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedProjectEmiterAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed::CreateSP(this, &SNewEmitterDialog::InheritanceOptionConfirmed),
				SAssignNew(TemplateAssetPicker, SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
				.bTemplateOnly(false)),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromOtherEmitterLabel", "Copy existing emitter"),
				LOCTEXT("CreateFromOtherEmitterDescription", "Copies an existing emitter from your project content"),
				LOCTEXT("ProjectEmitterPickerHeader", "Select a Project Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedProjectEmiterAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SAssignNew(TemplateAssetPicker, SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
				.OnTemplateAssetActivated(this, &SNewEmitterDialog::OnEmitterAssetsActivated)
				.bTemplateOnly(false))
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
	if (ActivatedTemplateAsset.IsValid())
	{
		OutSelectedAssets.AddUnique(ActivatedTemplateAsset);
	}
}

void SNewEmitterDialog::GetSelectedProjectEmiterAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(TemplateAssetPicker->GetSelectedAssets());
	if (ActivatedProjectAsset.IsValid())
	{
		OutSelectedAssets.AddUnique(ActivatedProjectAsset);
	}
}

void SNewEmitterDialog::OnTemplateAssetActivated(const FAssetData& InActivatedTemplateAsset)
{
	// Input handling issues with the list view widget can allow items to be activated but not added to the selection so cache this here
	// so it can be included in the selection set.
	ActivatedTemplateAsset = InActivatedTemplateAsset;
	ConfirmSelection();
}

void SNewEmitterDialog::OnEmitterAssetsActivated(const FAssetData& InActivatedTemplateAsset)
{
	// Input handling issues with the list view widget can allow items to be activated but not added to the selection so cache this here
	// so it can be included in the selection set.
	ActivatedProjectAsset = InActivatedTemplateAsset;
	ConfirmSelection();
}

void SNewEmitterDialog::InheritanceOptionConfirmed()
{
	bUseInheritance = true;
}

#undef LOCTEXT_NAMESPACE