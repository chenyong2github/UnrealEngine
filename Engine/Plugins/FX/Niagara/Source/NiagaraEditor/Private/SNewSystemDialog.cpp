// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNewSystemDialog.h"
#include "NiagaraSystem.h"
#include "NiagaraEditorStyle.h"
#include "SNiagaraAssetPickerList.h"

#include "AssetData.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "EditorStyleSet.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

typedef SItemSelector<FText, FAssetData> SNiagaraAssetItemSelector;

#define LOCTEXT_NAMESPACE "SNewSystemDialog"

void SNewSystemDialog::Construct(const FArguments& InArgs)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	SNiagaraNewAssetDialog::Construct(SNiagaraNewAssetDialog::FArguments(), UNiagaraSystem::StaticClass()->GetFName(), LOCTEXT("AssetTypeName", "system"),
		{
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromSelectedEmittersLabel", "New system from selected emitter(s)"),
				LOCTEXT("CreateFromSelectedEmittersDescription", "Choose a mix of emitters (inherited) and emitter templates (no inheritance)"),
				LOCTEXT("ProjectEmittersLabel", "Select Emitters to Add"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewSystemDialog::GetSelectedProjectEmiterAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(0, 0, 0, 10)
				[
					SAssignNew(EmitterAssetPicker, SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
					.bTemplateOnly(false)
					.bAllowMultiSelect(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.NewAssetDialog.SubHeaderText")
						.Text(LOCTEXT("SelectedEmittersLabel", "Emitters to Add:"))
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
						.IsEnabled(this, &SNewSystemDialog::IsAddEmittersToSelectionButtonEnabled)
						.OnClicked(this, &SNewSystemDialog::AddEmittersToSelectionButtonClicked)
						.ToolTipText(LOCTEXT("AddSelectedEmitterToolTip", "Add the selected emitter to the collection\n of emitters to be added to the new system."))
						.Content()
						[
							SNew(SBox)
							.WidthOverride(32.0f)
							.HeightOverride(16.0f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.TextStyle(FEditorStyle::Get(), "NormalText.Important")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
								.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(SelectedEmitterBox, SWrapBox)
					.UseAllottedWidth(true)
				]),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromTemplateLabel", "New system from a template"),
				LOCTEXT("CreateFromTemplateDescription", "The new system will be derived from a system template"),
				LOCTEXT("TemplateLabel", "Select a System Template"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewSystemDialog::GetSelectedSystemTemplateAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SAssignNew(TemplateAssetPicker, SNiagaraAssetPickerList, UNiagaraSystem::StaticClass())
				.OnTemplateAssetActivated(this, &SNewSystemDialog::OnTemplateAssetActivated)
				.bTemplateOnly(true)),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateFromOtherSystemLabel", "Copy existing system"),
				LOCTEXT("CreateFromOtherSystemDescription", "Copies an existing system from your project content and maintains any inheritance of the included emitters"),
				LOCTEXT("ProjectSystemsLabel", "Select a Project System"),
				SNiagaraNewAssetDialog::FOnGetSelectedAssetsFromPicker::CreateSP(this, &SNewSystemDialog::GetSelectedProjectSystemAssets),
				SNiagaraNewAssetDialog::FOnSelectionConfirmed(),
				SAssignNew(SystemAssetPicker, SNiagaraAssetPickerList, UNiagaraSystem::StaticClass())
				.OnTemplateAssetActivated(this, &SNewSystemDialog::OnSystemAssetsActivated)
				.bTemplateOnly(false)),
			SNiagaraNewAssetDialog::FNiagaraNewAssetDialogOption(
				LOCTEXT("CreateEmptyLabel", "Create empty system"),
				LOCTEXT("CreateEmptyDescription", "Create an empty system with no emitters or emitter templates"),
				LOCTEXT("EmptyLabel", "Empty System"),
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

TOptional<FAssetData> SNewSystemDialog::GetSelectedSystemAsset() const
{
	const TArray<FAssetData>& AllSelectedAssets = GetSelectedAssets();
	TArray<FAssetData> SelectedSystemAssets;
	for (const FAssetData& SelectedAsset : AllSelectedAssets)
	{
		if (SelectedAsset.AssetClass == UNiagaraSystem::StaticClass()->GetFName())
		{
			SelectedSystemAssets.Add(SelectedAsset);
		}
	}
	if (SelectedSystemAssets.Num() == 1)
	{
		return TOptional<FAssetData>(SelectedSystemAssets[0]);
	}
	return TOptional<FAssetData>();
}

TArray<FAssetData> SNewSystemDialog::GetSelectedEmitterAssets() const
{
	const TArray<FAssetData>& AllSelectedAssets = GetSelectedAssets();
	TArray<FAssetData> ConfirmedSelectedEmitterAssets;
	for (const FAssetData& SelectedAsset : AllSelectedAssets)
	{
		if (SelectedAsset.AssetClass == UNiagaraEmitter::StaticClass()->GetFName())
		{
			ConfirmedSelectedEmitterAssets.Add(SelectedAsset);
		}
	}
	return ConfirmedSelectedEmitterAssets;
}

void SNewSystemDialog::GetSelectedSystemTemplateAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(TemplateAssetPicker->GetSelectedAssets());
	if (ActivatedTemplateSystemAsset.IsValid())
	{
		OutSelectedAssets.AddUnique(ActivatedTemplateSystemAsset);
	}
}

void SNewSystemDialog::GetSelectedProjectSystemAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(SystemAssetPicker->GetSelectedAssets());
	if (ActivatedProjectSystemAsset.IsValid())
	{
		OutSelectedAssets.AddUnique(ActivatedProjectSystemAsset);
	}
}

void SNewSystemDialog::GetSelectedProjectEmiterAssets(TArray<FAssetData>& OutSelectedAssets)
{
	OutSelectedAssets.Append(SelectedEmitterAssets);
}

void SNewSystemDialog::OnTemplateAssetActivated(const FAssetData& ActivatedTemplateAsset)
{
	// Input handling issues with the list view widget can allow items to be activated but not added to the selection so cache this here
	// so it can be included in the selection set.
	ActivatedTemplateSystemAsset = ActivatedTemplateAsset;
	ConfirmSelection();
}

void SNewSystemDialog::OnSystemAssetsActivated(const FAssetData& ActivatedTemplateAsset)
{
	// Input handling issues with the list view widget can allow items to be activated but not added to the selection so cache this here
	// so it can be included in the selection set.
	ActivatedProjectSystemAsset = ActivatedTemplateAsset;
	ConfirmSelection();
}

bool SNewSystemDialog::IsAddEmittersToSelectionButtonEnabled() const
{
	return EmitterAssetPicker->GetSelectedAssets().Num() > 0;
}

FReply SNewSystemDialog::AddEmittersToSelectionButtonClicked()
{
	TArray<FAssetData> SelectedEmitterAssetsFromPicker = EmitterAssetPicker->GetSelectedAssets();
	AddEmitterAssetsToSelection(SelectedEmitterAssetsFromPicker);
	return FReply::Handled();
}

void SNewSystemDialog::AddEmitterAssetsToSelection(const TArray<FAssetData>& EmitterAssets)
{
	for (const FAssetData& SelectedEmitterAsset : EmitterAssets)
	{
		TSharedPtr<SWidget> SelectedEmitterWidget;
		SelectedEmitterBox->AddSlot()
			.Padding(FMargin(0, 0, 5, 0))
			[
				SAssignNew(SelectedEmitterWidget, SBorder)
				.BorderImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.NewAssetDialog.SubBorder"))
				.BorderBackgroundColor(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.NewAssetDialog.SubBorderColor"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(5, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::FromName(SelectedEmitterAsset.AssetName))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
						.OnClicked(this, &SNewSystemDialog::RemoveEmitterFromSelectionButtonClicked, SelectedEmitterAsset)
						.ToolTipText(LOCTEXT("RemoveSelectedEmitterToolTip", "Remove the selected emitter from the collection\n of emitters to be added to the new system."))
						[
							SNew(STextBlock)
							//.TextStyle(FEditorStyle::Get(), "NormalText.Important")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf057"))) /*times-circle*/)
							.ColorAndOpacity(FLinearColor(.8f, .2f, .2f, 1.0f))
						]
					]
				]
			];
		SelectedEmitterAssets.Add(SelectedEmitterAsset);
		SelectedEmitterAssetWidgets.Add(SelectedEmitterWidget);
	}
}

FReply SNewSystemDialog::RemoveEmitterFromSelectionButtonClicked(FAssetData EmitterAsset)
{
	int32 SelectionIndex = SelectedEmitterAssets.IndexOfByKey(EmitterAsset);
	if (SelectionIndex != INDEX_NONE)
	{
		SelectedEmitterAssets.RemoveAt(SelectionIndex);
		SelectedEmitterBox->RemoveSlot(SelectedEmitterAssetWidgets[SelectionIndex].ToSharedRef());
		SelectedEmitterAssetWidgets.RemoveAt(SelectionIndex);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE