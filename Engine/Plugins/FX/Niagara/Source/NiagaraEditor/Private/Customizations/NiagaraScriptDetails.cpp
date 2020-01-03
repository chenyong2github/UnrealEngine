// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "NiagaraEditorModule.h"
#include "Modules/ModuleManager.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraScript.h"
#include "NiagaraParameterCollectionCustomNodeBuilder.h"
#include "NiagaraMetaDataCustomNodeBuilder.h"
#include "SItemSelector.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"

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

float ButtonWidth = 50;
float DisplayColorHeight = 16;
float DisplayColorWidth = 24;
float EditColorHeight = 16;
float EditColorWidth = 30;

class SNiagaraDetailsButton : public SButton
{
	SLATE_BEGIN_ARGS(SNiagaraDetailsButton) {}
		SLATE_EVENT(FOnClicked, OnClicked)
		SLATE_ARGUMENT(FText, Text);
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SButton::Construct(SButton::FArguments()
			.OnClicked(InArgs._OnClicked)
			.Content()
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(InArgs._Text)
				.Justification(ETextJustify::Center)
				.MinDesiredWidth(ButtonWidth)
			]);
	}
};

class SNiagaraScriptHighlightPicker : public SCompoundWidget
{
private:
	enum class EEditMode
	{
		Picker,
		EditCurrent,
		EditNew
	};

public:
	SLATE_BEGIN_ARGS(SNiagaraScriptHighlightPicker)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<IPropertyHandle> InPropertyHandle)
	{
		PropertyHandle = InPropertyHandle;
		PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &SNiagaraScriptHighlightPicker::RefreshDisplayedValue));
		EditMode = EEditMode::Picker;
		RefreshDisplayedValue();
		ChildSlot
		[
			SNew(SOverlay)
			// Highlight picker
			+ SOverlay::Slot()
			[
				ConstructPickerWidgets()
			]
			// Highlight Editor
			+ SOverlay::Slot()
			[
				ConstructEditWidgets()
			]
		];
	}

private:

	TSharedRef<SWidget> ConstructPickerWidgets()
	{
		return SNew(SHorizontalBox)
			// Hightlight picker combo
			+ SHorizontalBox::Slot()
			[
				SAssignNew(HighlightPickerCombo, SComboButton)
				.Visibility(this, &SNiagaraScriptHighlightPicker::GetPickerVisibility)
				.OnGetMenuContent(this, &SNiagaraScriptHighlightPicker::OnGetMenuContent)
				.ContentPadding(FMargin(4, 2, 4, 2))
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 5, 0)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(this, &SNiagaraScriptHighlightPicker::GetHighlightSlateColor)
						.Padding(0)
						[
							SNew(SBox)
							.WidthOverride(DisplayColorWidth)
							.HeightOverride(DisplayColorHeight)
						]
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
						.Text(this, &SNiagaraScriptHighlightPicker::GetHighlightText)
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				PropertyHandle->CreateDefaultPropertyButtonWidgets()
			];
	}

	TSharedRef<SWidget> ConstructEditWidgets()
	{
		return SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Visibility(this, &SNiagaraScriptHighlightPicker::GetEditVisibility)
			.Padding(10)
			[
				SNew(SVerticalBox)

				// Edit Header
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.DetailsHeadingText")
					.Text(this, &SNiagaraScriptHighlightPicker::GetEditModeText)
				]
				// Edit controls.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 10, 0, 0)
				[
					ConstructEditHighlightWidgets()
				]
				// Error display.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 5, 0, 0)
				[
					ConstructEditErrorWidgets()
				]
				// Edit buttons
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(0, 10, 0, 0)
				[
					ConstructEditButtons()
				]
			];
	}

	TSharedRef<SWidget> ConstructEditHighlightWidgets()
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 5, 0)
			[
				SAssignNew(ColorBlock, SColorBlock)
				.Color(this, &SNiagaraScriptHighlightPicker::GetHighlightColor)
				.ShowBackgroundForAlpha(true)
				.IgnoreAlpha(false)
				.OnMouseButtonDown(this, &SNiagaraScriptHighlightPicker::OnMouseButtonDownColorBlock)
				.Size(FVector2D(EditColorWidth, EditColorHeight))
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SEditableTextBox)
				.Text(this, &SNiagaraScriptHighlightPicker::GetHighlightText)
				.OnTextCommitted(this, &SNiagaraScriptHighlightPicker::HighlightTextCommitted)
			];
	};

	TSharedRef<SWidget> ConstructEditButtons()
	{
		return SNew(SHorizontalBox)
			// Add new button.
			+ SHorizontalBox::Slot()
			[
				SNew(SNiagaraDetailsButton)
				.Visibility(this, &SNiagaraScriptHighlightPicker::GetAddNewButtonVisibility)
				.Text(LOCTEXT("AddNewHighlight", "Add"))
				.OnClicked(this, &SNiagaraScriptHighlightPicker::AddNewRequested)
			]
			// Apply button
			+ SHorizontalBox::Slot()
			.Padding(5, 0, 0, 0)
			[
				SNew(SNiagaraDetailsButton)
				.Visibility(this, &SNiagaraScriptHighlightPicker::GetApplyButtonVisibility)
				.Text(LOCTEXT("ApplyToCurrent", "Apply"))
				.ToolTipText(LOCTEXT("ApplyToCurrentToolTip", "Applies the current highlight changes to this script."))
				.OnClicked(this, &SNiagaraScriptHighlightPicker::ApplyCurrentRequested)
			]
			// Apply all button
			+ SHorizontalBox::Slot()
			.Padding(5, 0, 0, 0)
			[
				SNew(SNiagaraDetailsButton)
				.Visibility(this, &SNiagaraScriptHighlightPicker::GetApplyButtonVisibility)
				.Text(LOCTEXT("ApplyToAll", "Apply All"))
				.ToolTipText(LOCTEXT("ApplyToAllCurrentToolTip", "Applies the current highlight changes to this script,\nand any other scripts that use this same highlight."))
				.OnClicked(this, &SNiagaraScriptHighlightPicker::ApplyAllRequested)
			]
			// Cancel button
			+ SHorizontalBox::Slot()
			.Padding(5, 0, 0, 0)
			[
				SNew(SNiagaraDetailsButton)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SNiagaraScriptHighlightPicker::AddOrEditCancelled)
			];
	}

	TSharedRef<SWidget> ConstructEditErrorWidgets()
	{
		return SNew(SHorizontalBox)
			.Visibility(this, &SNiagaraScriptHighlightPicker::GetErrorVisibility)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 5, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.Error"))
			]
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(this, &SNiagaraScriptHighlightPicker::GetErrorText)
				.AutoWrapText(true)
			];
	}

	FNiagaraScriptHighlight GetCurrentPropertyValue()
	{
		FNiagaraScriptHighlight CurrentPropertyValue;
		void* PropertyValuePtr;
		if (PropertyHandle->GetValueData(PropertyValuePtr) == FPropertyAccess::Success)
		{
			CurrentPropertyValue = *(FNiagaraScriptHighlight*)PropertyValuePtr;
		}
		return CurrentPropertyValue;
	}

	void RefreshDisplayedValue()
	{
		DisplayedHighlight = GetCurrentPropertyValue();
	}

	EVisibility GetPickerVisibility() const
	{
		return EditMode == EEditMode::Picker ? EVisibility::Visible : EVisibility::Collapsed;
	}

	EVisibility GetEditVisibility() const
	{
		return EditMode == EEditMode::EditCurrent || EditMode == EEditMode::EditNew ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetEditModeText() const
	{
		if (EditMode == EEditMode::EditNew)
		{
			return LOCTEXT("EditNewLabel", "Add a new highlight value");
		}
		else if (EditMode == EEditMode::EditCurrent)
		{
			return LOCTEXT("EditCurrentLabel", "Edit an existing highlight value");
		}
		else
		{
			return FText();
		}
	}

	void DeleteItem()
	{
		TSharedPtr<IPropertyHandleArray> ParentArray = PropertyHandle->GetParentHandle()->AsArray();
		if (ParentArray.IsValid())
		{
			int32 PropertyIndex = PropertyHandle->GetIndexInArray();
			if (PropertyIndex != INDEX_NONE)
			{
				ParentArray->DeleteItem(PropertyHandle->GetIndexInArray());
			}
		}
	}

	typedef SItemSelector<FText, FNiagaraScriptHighlight> SNiagaraScriptHighlightSelector;

	TSharedRef<SWidget> OnGetMenuContent()
	{
		return SNew(SBox)
			.MaxDesiredHeight(200)
			[
				SNew(SVerticalBox)
				// Existing Items
				+ SVerticalBox::Slot()
				.Padding(5, 10, 5, 5)
				[
					SNew(SNiagaraScriptHighlightSelector)
					.Items(FNiagaraEditorModule::Get().GetCachedScriptAssetHighlights())
					.ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
					.OnCompareItemsForSorting(this, &SNiagaraScriptHighlightPicker::CompareHighlights)
					.OnDoesItemMatchFilterText(this, &SNiagaraScriptHighlightPicker::DoesHightlightMatchFilterText)
					.OnGenerateWidgetForItem(this, &SNiagaraScriptHighlightPicker::ConstructHighlightWidget)
					.OnItemActivated(this, &SNiagaraScriptHighlightPicker::HighlightActivated)
				]
				// Add and edit buttons
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5, 0, 5, 5)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0, 0, 5, 0)
					[
						SNew(SNiagaraDetailsButton)
						.Text(LOCTEXT("AddNewText", "Add New"))
						.ToolTipText(LOCTEXT("AddNewToolTipText", "Add a new highlight."))
						.OnClicked(this, &SNiagaraScriptHighlightPicker::RequestNewHighlight)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SNiagaraDetailsButton)
						.Text(LOCTEXT("EditCurrentText", "Edit"))
						.ToolTipText(LOCTEXT("EditCurrentToolTipText", "Edit the current highlight."))
						.OnClicked(this, &SNiagaraScriptHighlightPicker::RequestEditHighlight)
					]
				]
			];
	}

	bool CompareHighlights(const FNiagaraScriptHighlight& HighlightA, const FNiagaraScriptHighlight& HighlightB) const
	{
		return HighlightA.DisplayName.CompareTo(HighlightB.DisplayName) > 0;
	}

	bool DoesHightlightMatchFilterText(const FText& FilterText, const FNiagaraScriptHighlight& Highlight)
	{
		return Highlight.DisplayName.ToString().Contains(FilterText.ToString());
	}

	TSharedRef<SWidget> ConstructHighlightWidget(const FNiagaraScriptHighlight& Highlight) const
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 5, 5, 5)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Highlight.Color)
				.Padding(0)
				[
					SNew(SBox)
					.WidthOverride(DisplayColorWidth)
					.HeightOverride(DisplayColorHeight)
				]
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Highlight.DisplayName)
			];
	}

	void HighlightActivated(const FNiagaraScriptHighlight& ActivatedHighlight)
	{
		HighlightPickerCombo->SetIsOpen(false);
		ApplyHighlightChange(ActivatedHighlight);
	}

	void ApplyHighlightChange(const FNiagaraScriptHighlight& NewHighlight)
	{
		void* PropertyValuePtr;
		if (PropertyHandle->GetValueData(PropertyValuePtr) == FPropertyAccess::Success)
		{
			TArray<UObject*> OuterObjects;
			PropertyHandle->GetOuterObjects(OuterObjects);
			FScopedTransaction EditHightlightTransaction(LOCTEXT("EditHighlight", "Edit Highlight"));
			for (UObject* OuterObject : OuterObjects)
			{
				OuterObject->Modify();
			}
			PropertyHandle->NotifyPreChange();
			FNiagaraScriptHighlight* CurrentScriptHighlight = (FNiagaraScriptHighlight*)PropertyValuePtr;
			*CurrentScriptHighlight = NewHighlight;
			DisplayedHighlight = NewHighlight;
			PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			PropertyHandle->NotifyFinishedChangingProperties();
		}
	}

	FReply RequestNewHighlight()
	{
		EditMode = EEditMode::EditNew;
		HighlightPickerCombo->SetIsOpen(false);
		return FReply::Handled();
	}

	FReply RequestEditHighlight()
	{
		EditMode = EEditMode::EditCurrent;
		HighlightPickerCombo->SetIsOpen(false);
		return FReply::Handled();
	}

	FSlateColor GetHighlightSlateColor() const
	{
		return DisplayedHighlight.Color;
	}

	FLinearColor GetHighlightColor() const
	{
		return DisplayedHighlight.Color;
	}

	FText GetHighlightText() const
	{
		return DisplayedHighlight.DisplayName;
	}

	void HighlightTextCommitted(const FText& Text, ETextCommit::Type CommitType)
	{
		DisplayedHighlight.DisplayName = Text;
	}

	FReply OnMouseButtonDownColorBlock(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		FColorPickerArgs PickerArgs;
		{
			PickerArgs.bUseAlpha = true;
			PickerArgs.bOnlyRefreshOnMouseUp = false;
			PickerArgs.bOnlyRefreshOnOk = false;
			PickerArgs.DisplayGamma = TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(GEngine, &UEngine::GetDisplayGamma));
			PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SNiagaraScriptHighlightPicker::HighlightColorCommitted);
			PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(this, &SNiagaraScriptHighlightPicker::HighlightColorCancelled);
			PickerArgs.InitialColorOverride = DisplayedHighlight.Color;
			PickerArgs.ParentWidget = ColorBlock;
		}

		OpenColorPicker(PickerArgs);
		return FReply::Handled();
	}

	void HighlightColorCommitted(FLinearColor CommittedColor)
	{
		DisplayedHighlight.Color = CommittedColor;
	}

	void HighlightColorCancelled(FLinearColor OriginalColor)
	{
		DisplayedHighlight.Color = OriginalColor;
	}

	EVisibility GetAddNewButtonVisibility() const
	{
		return EditMode == EEditMode::EditNew ? EVisibility::Visible : EVisibility::Collapsed;
	}

	EVisibility GetApplyButtonVisibility() const
	{
		return EditMode == EEditMode::EditCurrent ? EVisibility::Visible : EVisibility::Collapsed;
	}

	bool ValidateNewHighlight(const FNiagaraScriptHighlight& HighlightToCheck, TOptional<FNiagaraScriptHighlight> CurrentHightlightToIgnore, TOptional<FNiagaraScriptHighlight>& OutFoundExistingHighlight, FText& OutErrorMessage)
	{
		if (HighlightToCheck.DisplayName.IsEmptyOrWhitespace())
		{
			OutErrorMessage = LOCTEXT("HighlightWithEmptyDisplay", "Display name can not be empty or white space.");
			return false;
		}

		for (const FNiagaraScriptHighlight& ExistingHighlight : FNiagaraEditorModule::Get().GetCachedScriptAssetHighlights())
		{
			if (CurrentHightlightToIgnore.IsSet() && ExistingHighlight == CurrentHightlightToIgnore)
			{
				continue;
			}

			bool bColorMatches = ExistingHighlight.Color.Equals(HighlightToCheck.Color);
			bool bDisplayNameMatches = ExistingHighlight.DisplayName.CompareTo(HighlightToCheck.DisplayName) == 0;
			if (bColorMatches && bDisplayNameMatches)
			{
				// A match was found so return it so the color will match exactly.
				OutFoundExistingHighlight = ExistingHighlight;
				return true;
			}
			if (bColorMatches && bDisplayNameMatches == false)
			{
				// Only the color or display name matched which is invalid.
				OutErrorMessage = LOCTEXT("HightlightMismatchDisplayName", "This new highlight matches an existing highlight's color but has a different display name.");
				return false;
			}
			if (bColorMatches == false && bDisplayNameMatches)
			{
				// Only the color or display name matched which is invalid.
				OutErrorMessage = LOCTEXT("HightlightMismatchColor", "This new highlight matches an existing highlight's display name but has a different color.");
				return false;
			}
		}
		// No matches were found which is also valid.
		OutFoundExistingHighlight.Reset();
		return true;
	}

	FReply AddNewRequested()
	{
		TOptional<FNiagaraScriptHighlight> ExistingHighlight;
		FText ValidateErrorMessage;
		if (ValidateNewHighlight(DisplayedHighlight, TOptional<FNiagaraScriptHighlight>(), ExistingHighlight, ValidateErrorMessage))
		{
			ApplyHighlightChange(ExistingHighlight.IsSet() ? ExistingHighlight.GetValue() : DisplayedHighlight);
			EditMode = EEditMode::Picker;
			bShowError = false;
			ErrorMessage = FText();
		}
		else
		{
			bShowError = true;
			ErrorMessage = ValidateErrorMessage;
		}
		return FReply::Handled();
	}

	FReply ApplyCurrentRequested()
	{
		TOptional<FNiagaraScriptHighlight> ExistingHighlight;
		FText ValidateErrorMessage;
		if (ValidateNewHighlight(DisplayedHighlight, TOptional<FNiagaraScriptHighlight>(), ExistingHighlight, ValidateErrorMessage))
		{
			ApplyHighlightChange(ExistingHighlight.IsSet() ? ExistingHighlight.GetValue() : DisplayedHighlight);
			EditMode = EEditMode::Picker;
			bShowError = false;
			ErrorMessage = FText();
		}
		else
		{
			bShowError = true;
			ErrorMessage = ValidateErrorMessage;
		}
		return FReply::Handled();
	}

	FReply ApplyAllRequested()
	{
		TOptional<FNiagaraScriptHighlight> ExistingHighlight;
		FText ValidateErrorMessage;
		FNiagaraScriptHighlight CurrentHighlight = GetCurrentPropertyValue();
		if (ValidateNewHighlight(DisplayedHighlight, CurrentHighlight, ExistingHighlight, ValidateErrorMessage))
		{
			TArray<FAssetData> MatchingScriptAssets;
			FNiagaraEditorModule::Get().GetScriptAssetsMatchingHighlight(CurrentHighlight, MatchingScriptAssets);

			FScopedTransaction ApplyAllTransaction(LOCTEXT("ApplyAllTransaction", "Apply highlight change to all scripts."));

			FNiagaraScriptHighlight NewHighlight = ExistingHighlight.IsSet() ? ExistingHighlight.GetValue() : DisplayedHighlight;
			ApplyHighlightChange(NewHighlight);

			for (const FAssetData& MatchingScriptAsset : MatchingScriptAssets)
			{
				UNiagaraScript* Script = CastChecked<UNiagaraScript>(MatchingScriptAsset.GetAsset());
				Script->Modify();
				for (FNiagaraScriptHighlight& Highlight : Script->Highlights)
				{
					if (Highlight == CurrentHighlight)
					{
						Highlight = NewHighlight;
					}
				}
			}

			EditMode = EEditMode::Picker;
			bShowError = false;
			ErrorMessage = FText();
		}
		else
		{
			bShowError = true;
			ErrorMessage = ValidateErrorMessage;
		}
		return FReply::Handled();
	}

	FReply AddOrEditCancelled()
	{
		EditMode = EEditMode::Picker;
		bShowError = false;
		ErrorMessage = FText();
		RefreshDisplayedValue();
		return FReply::Handled();
	}

	EVisibility GetErrorVisibility() const
	{
		return bShowError ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetErrorText() const
	{
		return ErrorMessage;
	}

private:
	TSharedPtr<IPropertyHandle> PropertyHandle;
	FNiagaraScriptHighlight DisplayedHighlight;
	EEditMode EditMode;
	TSharedPtr<SComboButton> HighlightPickerCombo;
	TSharedPtr<SColorBlock> ColorBlock;
	bool bShowError;
	FText ErrorMessage;
};

void FNiagaraScriptHighlightDetails::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.WholeRowContent()
		[
			SNew(SNiagaraScriptHighlightPicker, PropertyHandle)
		];
}


#undef LOCTEXT_NAMESPACE
