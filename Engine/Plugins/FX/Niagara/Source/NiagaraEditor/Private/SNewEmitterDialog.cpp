// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNewEmitterDialog.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorStyle.h"
#include "SNiagaraTemplateAssetPicker.h"
#include "SNiagaraNewAssetDialog.h"

#include "AssetData.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

typedef SItemSelector<FText, FAssetData> SNiagaraAssetItemSelector;

#define LOCTEXT_NAMESPACE "SNewEmitterDialog"

void SNewEmitterDialog::Construct(const FArguments& InArgs)
{
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.SelectionMode = ESelectionMode::SingleToggle;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.Filter.ClassNames.Add(UNiagaraEmitter::StaticClass()->GetFName());
	AssetPickerConfig.GetCurrentSelectionDelegates.Add(&GetSelectedEmitterAssetsFromPicker);
	AssetPickerConfig.OnAssetsActivated.BindSP(this, &SNewEmitterDialog::OnEmitterAssetsActivated);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TSharedRef<SWidget> AssetPicker = ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig);

	SNiagaraNewAssetDialog::Construct(SNiagaraNewAssetDialog::FArguments(), UNiagaraEmitter::StaticClass()->GetFName(), LOCTEXT("AssetTypeName", "emitter"),
		{
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromTemplateLabel", "Create a new emitter from an emitter template"),
				LOCTEXT("TemplatesPickerHeader", "Select a Template Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedEmitterTemplateAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SAssignNew(TemplateAssetPicker, SNiagaraTemplateAssetPicker, UNiagaraEmitter::StaticClass())
				.OnTemplateAssetActivated(this, &SNewEmitterDialog::OnTemplateAssetActivated)),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromOtherEmitterLabel", "Copy an existing emitter from your project content"),
				LOCTEXT("ProjectEmitterPickerHeader", "Select a Project Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedProjectEmiterAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				AssetPicker),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("InheritFromOtherEmitterLabel", "Inherit from an existing emitter in your project content"),
				LOCTEXT("InheritProjectEmitterPickerHeader", "Select a Parent Project Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewEmitterDialog::GetSelectedProjectEmiterAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed::CreateSP(this, &SNewEmitterDialog::InheritanceOptionConfirmed),
				AssetPicker),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateEmptyLabel", "Create an empty emitter with no modules or renderers (Advanced)"),
				LOCTEXT("EmptyLabel", "Empty Emitter"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker(),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoOptionsLabel", "No Options"))
				])
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
	OutSelectedAssets.Append(GetSelectedEmitterAssetsFromPicker.Execute());
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

void SNewEmitterDialog::OnEmitterAssetsActivated(const TArray<FAssetData>& ActivatedAssets, EAssetTypeActivationMethod::Type ActivationMethod)
{
	if ((ActivationMethod == EAssetTypeActivationMethod::DoubleClicked || ActivationMethod == EAssetTypeActivationMethod::Opened) && ActivatedAssets.Num() == 1)
	{
		// Input handling issues with the list view widget can allow items to be activated but not added to the selection so cache this here
		// so it can be included in the selection set.
		ActivatedProjectAsset = ActivatedAssets[0];
		ConfirmSelection();
	}
}

void SNewEmitterDialog::InheritanceOptionConfirmed()
{
	bUseInheritance = true;
}

#undef LOCTEXT_NAMESPACE