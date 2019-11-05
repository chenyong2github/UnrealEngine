// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "NiagaraParameterCollectionViewModel.h"
#include "NiagaraScriptInputCollectionViewModel.h"
#include "NiagaraScriptOutputCollectionViewModel.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraParameterViewModel.h"
#include "NiagaraEditorStyle.h"
#include "IDetailChildrenBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "NiagaraEditorModule.h"
#include "Modules/ModuleManager.h"
#include "INiagaraEditorTypeUtilities.h"
#include "Widgets/Layout/SBox.h"
#include "NiagaraScript.h"
#include "NiagaraParameterCollectionCustomNodeBuilder.h"
#include "NiagaraMetaDataCustomNodeBuilder.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptDetails"

class SAddParameterButton : public SCompoundWidget
{	
	SLATE_BEGIN_ARGS(SAddParameterButton)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<INiagaraParameterCollectionViewModel> InViewModel)
	{
		CollectionViewModel = InViewModel;

		ChildSlot
		.HAlign(HAlign_Right)
		[
			SAssignNew(ComboButton, SComboButton)
			.HasDownArrow(false)
			.ButtonStyle(FEditorStyle::Get(), "RoundButton")
			.ForegroundColor(FSlateColor::UseForeground())
			.OnGetMenuContent(this, &SAddParameterButton::GetAddParameterMenuContent)
			.Visibility(CollectionViewModel.ToSharedRef(), &INiagaraParameterCollectionViewModel::GetAddButtonVisibility)
			.ContentPadding(FMargin(2.0f, 1.0f, 0.0f, 1.0f))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0, 1, 2, 1)
				.AutoWidth()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FEditorStyle::GetBrush("Plus"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(CollectionViewModel.ToSharedRef(), &INiagaraParameterCollectionViewModel::GetAddButtonText)
					.Visibility(this, &SAddParameterButton::OnGetAddParameterTextVisibility)
					.ShadowOffset(FVector2D(1, 1))
				]
			]
		];
	}

private:
	EVisibility OnGetAddParameterTextVisibility() const
	{
		return IsHovered() || ComboButton->IsOpen() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	TSharedRef<SWidget> GetAddParameterMenuContent() const
	{
		FMenuBuilder AddMenuBuilder(true, nullptr);
		for (TSharedPtr<FNiagaraTypeDefinition> AvailableType : CollectionViewModel->GetAvailableTypes())
		{
			AddMenuBuilder.AddMenuEntry
			(
				AvailableType->GetNameText(),
				FText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(CollectionViewModel.ToSharedRef(), &INiagaraParameterCollectionViewModel::AddParameter, AvailableType))
			);
		}
		return AddMenuBuilder.MakeWidget();
	}

	
private:
	TSharedPtr<INiagaraParameterCollectionViewModel> CollectionViewModel;
	TSharedPtr<SComboButton> ComboButton;
};

TSharedRef<IDetailCustomization> FNiagaraScriptDetails::MakeInstance(TWeakPtr<FNiagaraScriptViewModel> ScriptViewModel)
{
	return MakeShared<FNiagaraScriptDetails>(ScriptViewModel.Pin());
}

FNiagaraScriptDetails::FNiagaraScriptDetails(TSharedPtr<FNiagaraScriptViewModel> InScriptViewModel)
	: ScriptViewModel(InScriptViewModel)
{}

void FNiagaraScriptDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	static const FName InputParamCategoryName = TEXT("NiagaraScript_InputParams");
	static const FName OutputParamCategoryName = TEXT("NiagaraScript_OutputParams");
	static const FName ScriptCategoryName = TEXT("Script");
	static const FName MetadataCategoryName = TEXT("Metadata");

	TSharedPtr<IPropertyHandle> NumericOutputPropertyHandle = DetailBuilder.GetProperty(TEXT("NumericOutputTypeSelectionMode"));
	if (NumericOutputPropertyHandle.IsValid())
	{
		NumericOutputPropertyHandle->MarkHiddenByCustomization();
	}

	DetailBuilder.EditCategory(ScriptCategoryName);

	UNiagaraScript* StandaloneScript = ScriptViewModel->GetStandaloneScript();
	bool bAddParameters = false;
	TSharedPtr<INiagaraParameterCollectionViewModel> InputCollectionViewModel;
	TSharedPtr<INiagaraParameterCollectionViewModel> OutputCollectionViewModel;

	if (StandaloneScript)
	{
		InputCollectionViewModel = ScriptViewModel->GetInputCollectionViewModel();
		OutputCollectionViewModel = ScriptViewModel->GetOutputCollectionViewModel();
		bAddParameters = true;
	}

	if (InputCollectionViewModel.IsValid())
	{
		IDetailCategoryBuilder& InputParamCategory = DetailBuilder.EditCategory(InputParamCategoryName, LOCTEXT("InputParamCategoryName", "Input Parameters"));
		if (bAddParameters)
		{
			InputParamCategory.HeaderContent(
				SNew(SBox)
				.Padding(FMargin(2, 2, 0, 2))
				.VAlign(VAlign_Center)
				[
					SNew(SAddParameterButton, InputCollectionViewModel.ToSharedRef())
				]);
		}
		InputParamCategory.AddCustomBuilder(MakeShared<FNiagaraParameterCollectionCustomNodeBuilder>(InputCollectionViewModel.ToSharedRef()));
	}

	if (OutputCollectionViewModel.IsValid())
	{
		IDetailCategoryBuilder& OutputParamCategory = DetailBuilder.EditCategory(OutputParamCategoryName, LOCTEXT("OutputParamCategoryName", "Output Parameters"));
		if (bAddParameters)
		{
			OutputParamCategory.HeaderContent(
				SNew(SBox)
				.Padding(FMargin(2, 2, 0, 2))
				.VAlign(VAlign_Center)
				[
					SNew(SAddParameterButton, OutputCollectionViewModel.ToSharedRef())
				]);
		}
		OutputParamCategory.AddCustomBuilder(MakeShared<FNiagaraParameterCollectionCustomNodeBuilder>(OutputCollectionViewModel.ToSharedRef()));
	}
	
	// Disable the metadata header in Script Details panel
	// TODO: Delete when it isn't useful anymore (when the parameters rework is done)
	/*
	IDetailCategoryBuilder& MetadataDetailCategory = DetailBuilder.EditCategory(MetadataCategoryName, LOCTEXT("MetadataParamCategoryName", "Variable Metadata"));
	MetadataDetailCategory.HeaderContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(5, 0, 5, 0)
		.HAlign(EHorizontalAlignment::HAlign_Right)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.ContentPadding(1)
			.ToolTipText(LOCTEXT("RefreshMetadataToolTip", "Refresh the view according to the latest Editor Sort Priority values"))
			.OnClicked(this, &FNiagaraScriptDetails::OnRefreshMetadata)
			.Content()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.Refresh"))
			]
		]
	);

	MetaDataBuilder = MakeShared<FNiagaraMetaDataCustomNodeBuilder>();
	MetaDataBuilder->Initialize(ScriptViewModel->GetGraphViewModel()->GetGraph());
	MetadataDetailCategory.AddCustomBuilder(MetaDataBuilder.ToSharedRef());
	*/
}

FReply FNiagaraScriptDetails::OnRefreshMetadata()
{
	if (MetaDataBuilder.IsValid())
	{
		MetaDataBuilder->Rebuild();
	}
    return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
