// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackFunctionInputValue.h"

#include "AssetRegistryModule.h"
#include "EditorFontGlyphs.h"
#include "INiagaraEditorTypeUtilities.h"
#include "IStructureDetailsView.h"
#include "NiagaraActions.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "SDropTarget.h"
#include "SNiagaraGraphActionWidget.h"
#include "SNiagaraParameterEditor.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/SNiagaraLibraryOnlyToggleHeader.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "NiagaraStackFunctionInputValue"

const float TextIconSize = 16;

bool SNiagaraStackFunctionInputValue::bLibraryOnly = true;

void SNiagaraStackFunctionInputValue::Construct(const FArguments& InArgs, UNiagaraStackFunctionInput* InFunctionInput)
{
	FunctionInput = InFunctionInput;
	FunctionInput->OnValueChanged().AddSP(this, &SNiagaraStackFunctionInputValue::OnInputValueChanged);

	ChildSlot
	[
		SNew(SDropTarget)
		.OnAllowDrop(this, &SNiagaraStackFunctionInputValue::OnFunctionInputAllowDrop)
		.OnDrop(this, &SNiagaraStackFunctionInputValue::OnFunctionInputDrop)
		.HorizontalImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"))
		.VerticalImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderVertical"))
		.BackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.DropTarget.BackgroundColor"))
		.BackgroundColorHover(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.DropTarget.BackgroundColorHover"))
		.IsEnabled_UObject(FunctionInput, &UNiagaraStackEntry::GetOwnerIsEnabled)
		.Content()
		[
			// Values
			SNew(SHorizontalBox)
			.IsEnabled(this, &SNiagaraStackFunctionInputValue::GetInputEnabled)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0, 0, 3, 0)
			[
				// Value Icon
				SNew(SBox)
				.WidthOverride(TextIconSize)
				.VAlign(VAlign_Center)
				.Visibility(this, &SNiagaraStackFunctionInputValue::GetInputIconVisibility)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(this, &SNiagaraStackFunctionInputValue::GetInputIconText)
					.ToolTipText(this, &SNiagaraStackFunctionInputValue::GetInputIconToolTip)
					.ColorAndOpacity(this, &SNiagaraStackFunctionInputValue::GetInputIconColor)
				]
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				// Value container and widgets.
				SAssignNew(ValueContainer, SBox)
				.ToolTipText_UObject(FunctionInput, &UNiagaraStackFunctionInput::GetValueToolTip)
				[
					ConstructValueWidgets()
				]
			]

			// Handle drop-down button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				SAssignNew(SetFunctionInputButton, SComboButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FSlateColor::UseForeground())
				.OnGetMenuContent(this, &SNiagaraStackFunctionInputValue::OnGetAvailableHandleMenu)
				.ContentPadding(FMargin(2))
				.Visibility(this, &SNiagaraStackFunctionInputValue::GetDropdownButtonVisibility)
				.MenuPlacement(MenuPlacement_BelowRightAnchor)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
			]

			// Reset Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				SNew(SButton)
				.IsFocusable(false)
				.ToolTipText(LOCTEXT("ResetToolTip", "Reset to the default value"))
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.ContentPadding(0)
				.Visibility(this, &SNiagaraStackFunctionInputValue::GetResetButtonVisibility)
				.OnClicked(this, &SNiagaraStackFunctionInputValue::ResetButtonPressed)
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
				]
			]

			// Reset to base Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3, 0, 0, 0)
			[
				SNew(SButton)
				.IsFocusable(false)
				.ToolTipText(LOCTEXT("ResetToBaseToolTip", "Reset this input to the value defined by the parent emitter"))
				.ButtonStyle(FEditorStyle::Get(), "NoBorder")
				.ContentPadding(0)
				.Visibility(this, &SNiagaraStackFunctionInputValue::GetResetToBaseButtonVisibility)
				.OnClicked(this, &SNiagaraStackFunctionInputValue::ResetToBaseButtonPressed)
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
					.ColorAndOpacity(FSlateColor(FLinearColor::Green))
				]
			]
		]
	];

	ValueModeForGeneratedWidgets = FunctionInput->GetValueMode();
}

void SNiagaraStackFunctionInputValue::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (FunctionInput->GetIsDynamicInputScriptReassignmentPending())
	{
		FunctionInput->SetIsDynamicInputScriptReassignmentPending(false);
		ShowReassignDynamicInputScriptMenu();
	}
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::ConstructValueWidgets()
{
	DisplayedLocalValueStruct.Reset();
	LocalValueStructParameterEditor.Reset();
	LocalValueStructDetailsView.Reset();

	switch (FunctionInput->GetValueMode())
	{
	case UNiagaraStackFunctionInput::EValueMode::Local:
	{
		return ConstructLocalValueStructWidget();
	}
	case UNiagaraStackFunctionInput::EValueMode::Linked:
	{
		return SNew(SNiagaraParameterName)
			.ReadOnlyTextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.ParameterName(this, &SNiagaraStackFunctionInputValue::GetLinkedValueHandleName)
			.OnDoubleClicked(this, &SNiagaraStackFunctionInputValue::OnLinkedInputDoubleClicked);
	}
	case UNiagaraStackFunctionInput::EValueMode::Data:
	{
		return SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text(this, &SNiagaraStackFunctionInputValue::GetDataValueText);
	}
	case UNiagaraStackFunctionInput::EValueMode::Dynamic:
	{
		TSharedRef<SWidget> DynamicInputText = SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text(this, &SNiagaraStackFunctionInputValue::GetDynamicValueText)
			.OnDoubleClicked(this, &SNiagaraStackFunctionInputValue::DynamicInputTextDoubleClicked);
		if (FunctionInput->IsScratchDynamicInput())
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					DynamicInputText
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "RoundButton")
					.OnClicked(this, &SNiagaraStackFunctionInputValue::ScratchButtonPressed)
					.ToolTipText(LOCTEXT("OpenInScratchToolTip", "Open this dynamic input in the scratch pad."))
					.ContentPadding(FMargin(1.0f, 0.0f))
					.Content()
					[
						SNew(SImage)
						.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Scratch"))
					]
				];
		}
		// the function script could be wiped (deleted scratch pad script or missing asset)
		else if (FunctionInput->GetDynamicInputNode()->FunctionScript && FunctionInput->GetDynamicInputNode()->FunctionScript->IsVersioningEnabled())
		{
			return SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .VAlign(VAlign_Center)
                [
                    DynamicInputText
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
	                SNew(SComboButton)
				    .HasDownArrow(false)
				    .ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				    .ForegroundColor(FSlateColor::UseForeground())
				    .OnGetMenuContent(this, &SNiagaraStackFunctionInputValue::GetVersionSelectorDropdownMenu)
				    .ContentPadding(FMargin(2))
				    .ToolTipText(LOCTEXT("VersionTooltip", "Change the version of this module script"))
				    .HAlign(HAlign_Center)
				    .VAlign(VAlign_Center)
				    .ButtonContent()
				    [
				        SNew(STextBlock)
				        .Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				        .ColorAndOpacity(this, &SNiagaraStackFunctionInputValue::GetVersionSelectorColor)
				        .Text(FEditorFontGlyphs::Random)
				    ]
                ];
		}
		else
		{
			return DynamicInputText;
		}
	}
	case UNiagaraStackFunctionInput::EValueMode::DefaultFunction:
	{
		return SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text(this, &SNiagaraStackFunctionInputValue::GetDefaultFunctionText);
	}
	case UNiagaraStackFunctionInput::EValueMode::Expression:
	{
		return SNew(SEditableTextBox)
			.IsReadOnly(false)
			.Text_UObject(FunctionInput, &UNiagaraStackFunctionInput::GetCustomExpressionText)
			.OnTextCommitted(this, &SNiagaraStackFunctionInputValue::OnExpressionTextCommitted);
	}
	case UNiagaraStackFunctionInput::EValueMode::InvalidOverride:
	{
		return SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text(LOCTEXT("InvalidOverrideText", "Invalid Script Value"));
	}
	case UNiagaraStackFunctionInput::EValueMode::UnsupportedDefault:
	{
		return SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text(LOCTEXT("UnsupportedDefault", "Custom Default"));
	}
	default:
		return SNullWidget::NullWidget;
	}
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::GetVersionSelectorDropdownMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	UNiagaraScript* Script = FunctionInput->GetDynamicInputNode()->FunctionScript;
	TArray<FNiagaraAssetVersion> AssetVersions = Script->GetAllAvailableVersions();
	for (FNiagaraAssetVersion& Version : AssetVersions)
	{
		if (!Version.bIsVisibleInVersionSelector)
    	{
    		continue;
    	}
		FVersionedNiagaraScriptData* ScriptData = Script->GetScriptData(Version.VersionGuid);
		bool bIsSelected = FunctionInput->GetDynamicInputNode()->SelectedScriptVersion == Version.VersionGuid;
		
		FText Tooltip = LOCTEXT("NiagaraSelectVersion_Tooltip", "Select this version to use for the dynamic input");
		if (!ScriptData->VersionChangeDescription.IsEmpty())
		{
			Tooltip = FText::Format(LOCTEXT("NiagaraSelectVersionChangelist_Tooltip", "Select this version to use for the dynamic input. Change description for this version:\n{0}"), ScriptData->VersionChangeDescription);
		}
		
		FUIAction UIAction(FExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::SwitchToVersion, Version),
        FCanExecuteAction(),
        FIsActionChecked::CreateLambda([bIsSelected]() { return bIsSelected; }));
		FText Format = (Version == Script->GetExposedVersion()) ? FText::FromString("{0}.{1}*") : FText::FromString("{0}.{1}");
		FText Label = FText::Format(Format, Version.MajorVersion, Version.MinorVersion);
		MenuBuilder.AddMenuEntry(Label, Tooltip, FSlateIcon(), UIAction, NAME_None, EUserInterfaceActionType::RadioButton);	
	}

	return MenuBuilder.MakeWidget();
}

void SNiagaraStackFunctionInputValue::SwitchToVersion(FNiagaraAssetVersion Version)
{
	FunctionInput->ChangeScriptVersion(Version.VersionGuid);
}

FSlateColor SNiagaraStackFunctionInputValue::GetVersionSelectorColor() const
{
	UNiagaraScript* Script = FunctionInput->GetDynamicInputNode()->FunctionScript;
	
	if (Script && Script->IsVersioningEnabled())
	{
		FVersionedNiagaraScriptData* ScriptData = Script->GetScriptData(FunctionInput->GetDynamicInputNode()->SelectedScriptVersion);
		if (ScriptData && ScriptData->Version < Script->GetExposedVersion())
		{
			return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.IconColor.VersionUpgrade");
		}
	}
	return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor");
}

void SNiagaraStackFunctionInputValue::SetToLocalValue()
{
	const UScriptStruct* LocalValueStruct = FunctionInput->GetInputType().GetScriptStruct();
	if (LocalValueStruct != nullptr)
	{
		TSharedRef<FStructOnScope> LocalValue = MakeShared<FStructOnScope>(LocalValueStruct);
		TArray<uint8> DefaultValueData;
		FNiagaraEditorUtilities::GetTypeDefaultValue(FunctionInput->GetInputType(), DefaultValueData);
		if (DefaultValueData.Num() == LocalValueStruct->GetStructureSize())
		{
			FMemory::Memcpy(LocalValue->GetStructMemory(), DefaultValueData.GetData(), DefaultValueData.Num());
			FunctionInput->SetLocalValue(LocalValue);
		}
	}
}

bool SNiagaraStackFunctionInputValue::GetInputEnabled() const
{
	return FunctionInput->GetHasEditCondition() == false || FunctionInput->GetEditConditionEnabled();
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::ConstructLocalValueStructWidget()
{
	LocalValueStructParameterEditor.Reset();
	LocalValueStructDetailsView.Reset();

	DisplayedLocalValueStruct = MakeShared<FStructOnScope>(FunctionInput->GetInputType().GetStruct());
	FNiagaraEditorUtilities::CopyDataTo(*DisplayedLocalValueStruct.Get(), *FunctionInput->GetLocalValueStruct().Get());
	if (DisplayedLocalValueStruct.IsValid())
	{
		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
		TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> TypeEditorUtilities = NiagaraEditorModule.GetTypeUtilities(FunctionInput->GetInputType());
		if (TypeEditorUtilities.IsValid() && TypeEditorUtilities->CanCreateParameterEditor())
		{
			TSharedPtr<SNiagaraParameterEditor> ParameterEditor = TypeEditorUtilities->CreateParameterEditor(FunctionInput->GetInputType());
			ParameterEditor->UpdateInternalValueFromStruct(DisplayedLocalValueStruct.ToSharedRef());
			ParameterEditor->SetOnBeginValueChange(SNiagaraParameterEditor::FOnValueChange::CreateSP(
				this, &SNiagaraStackFunctionInputValue::ParameterBeginValueChange));
			ParameterEditor->SetOnEndValueChange(SNiagaraParameterEditor::FOnValueChange::CreateSP(
				this, &SNiagaraStackFunctionInputValue::ParameterEndValueChange));
			ParameterEditor->SetOnValueChanged(SNiagaraParameterEditor::FOnValueChange::CreateSP(
				this, &SNiagaraStackFunctionInputValue::ParameterValueChanged, TWeakPtr<SNiagaraParameterEditor>(ParameterEditor)));

			LocalValueStructParameterEditor = ParameterEditor;

			return SNew(SBox)
				.HAlign(ParameterEditor->GetHorizontalAlignment())
				.VAlign(ParameterEditor->GetVerticalAlignment())
				[
					ParameterEditor.ToSharedRef()
				];
		}
		else
		{
			FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

			TSharedRef<IStructureDetailsView> StructureDetailsView = PropertyEditorModule.CreateStructureDetailView(
				FDetailsViewArgs(false, false, false, FDetailsViewArgs::HideNameArea, true),
				FStructureDetailsViewArgs(),
				nullptr);

			StructureDetailsView->SetStructureData(DisplayedLocalValueStruct);
			StructureDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SNiagaraStackFunctionInputValue::ParameterPropertyValueChanged);

			LocalValueStructDetailsView = StructureDetailsView;
			return StructureDetailsView->GetWidget().ToSharedRef();
		}
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}

void SNiagaraStackFunctionInputValue::OnInputValueChanged()
{
	if (ValueModeForGeneratedWidgets != FunctionInput->GetValueMode())
	{
		ValueContainer->SetContent(ConstructValueWidgets());
		ValueModeForGeneratedWidgets = FunctionInput->GetValueMode();
	}
	else
	{
		if (ValueModeForGeneratedWidgets == UNiagaraStackFunctionInput::EValueMode::Local)
		{
			if (DisplayedLocalValueStruct->GetStruct() == FunctionInput->GetLocalValueStruct()->GetStruct())
			{
				FNiagaraEditorUtilities::CopyDataTo(*DisplayedLocalValueStruct.Get(), *FunctionInput->GetLocalValueStruct().Get());
				if (LocalValueStructParameterEditor.IsValid())
				{
					LocalValueStructParameterEditor->UpdateInternalValueFromStruct(DisplayedLocalValueStruct.ToSharedRef());
				}
				if (LocalValueStructDetailsView.IsValid())
				{
					LocalValueStructDetailsView->SetStructureData(TSharedPtr<FStructOnScope>());
					LocalValueStructDetailsView->SetStructureData(DisplayedLocalValueStruct);
				}
			}
			else
			{
				ValueContainer->SetContent(ConstructLocalValueStructWidget());
			}
		}
	}
}

void SNiagaraStackFunctionInputValue::ParameterBeginValueChange()
{
	FunctionInput->NotifyBeginLocalValueChange();
}

void SNiagaraStackFunctionInputValue::ParameterEndValueChange()
{
	FunctionInput->NotifyEndLocalValueChange();
}

void SNiagaraStackFunctionInputValue::ParameterValueChanged(TWeakPtr<SNiagaraParameterEditor> ParameterEditor)
{
	TSharedPtr<SNiagaraParameterEditor> ParameterEditorPinned = ParameterEditor.Pin();
	if (ParameterEditorPinned.IsValid())
	{
		ParameterEditorPinned->UpdateStructFromInternalValue(DisplayedLocalValueStruct.ToSharedRef());
		FunctionInput->SetLocalValue(DisplayedLocalValueStruct.ToSharedRef());
	}
}

void SNiagaraStackFunctionInputValue::ParameterPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	FunctionInput->SetLocalValue(DisplayedLocalValueStruct.ToSharedRef());
}

FName SNiagaraStackFunctionInputValue::GetLinkedValueHandleName() const
{
	return FunctionInput->GetLinkedValueHandle().GetParameterHandleString();
}

FText SNiagaraStackFunctionInputValue::GetDataValueText() const
{
	if (FunctionInput->GetDataValueObject() != nullptr)
	{
		return FunctionInput->GetInputType().GetClass()->GetDisplayNameText();
	}
	else
	{
		return FText::Format(LOCTEXT("InvalidDataObjectFormat", "{0} (Invalid)"), FunctionInput->GetInputType().GetClass()->GetDisplayNameText());
	}
}

FText SNiagaraStackFunctionInputValue::GetDynamicValueText() const
{
	if (UNiagaraNodeFunctionCall* NodeFunctionCall = FunctionInput->GetDynamicInputNode())
	{
		if (!FunctionInput->GetIsExpanded())
		{
			FText CollapsedText = FunctionInput->GetCollapsedStateText();
			if (!CollapsedText.IsEmptyOrWhitespace())
			{
				return CollapsedText;
			}
		}
		FString FunctionName = NodeFunctionCall->FunctionScript ? NodeFunctionCall->FunctionScript->GetName() : NodeFunctionCall->Signature.Name.ToString();
		return FText::FromString(FName::NameToDisplayString(FunctionName, false));
	}
	else
	{
		return LOCTEXT("InvalidDynamicDisplayName", "(Invalid)");
	}
}

FText SNiagaraStackFunctionInputValue::GetDefaultFunctionText() const
{
	if (FunctionInput->GetDefaultFunctionNode() != nullptr)
	{
		return FText::FromString(FName::NameToDisplayString(FunctionInput->GetDefaultFunctionNode()->GetFunctionName(), false));
	}
	else
	{
		return LOCTEXT("InvalidDefaultFunctionDisplayName", "(Invalid)");
	}
}

void SNiagaraStackFunctionInputValue::OnExpressionTextCommitted(const FText& Name, ETextCommit::Type CommitInfo)
{
	FunctionInput->SetCustomExpression(Name.ToString());
}

FReply SNiagaraStackFunctionInputValue::DynamicInputTextDoubleClicked(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent)
{
	UNiagaraNodeFunctionCall* DynamicInputNode = FunctionInput->GetDynamicInputNode();
	if (DynamicInputNode->FunctionScript != nullptr)
	{
		if (DynamicInputNode->FunctionScript->IsAsset())
		{
			DynamicInputNode->FunctionScript->VersionToOpenInEditor = DynamicInputNode->SelectedScriptVersion;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(DynamicInputNode->FunctionScript);
			return FReply::Handled();
		}
		else
		{
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchPadScriptViewModel = FunctionInput->GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(DynamicInputNode->FunctionScript);
			if(ScratchPadScriptViewModel.IsValid())
			{
				FunctionInput->GetSystemViewModel()->GetScriptScratchPadViewModel()->FocusScratchPadScriptViewModel(ScratchPadScriptViewModel.ToSharedRef());
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

FReply SNiagaraStackFunctionInputValue::OnLinkedInputDoubleClicked(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent)
{
	FString ParamCollection;
	FString ParamName;
	FunctionInput->GetLinkedValueHandle().GetName().ToString().Split(TEXT("."), &ParamCollection, &ParamName);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> CollectionAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraParameterCollection::StaticClass()->GetFName(), CollectionAssets);

	for (FAssetData& CollectionAsset : CollectionAssets)
	{
		UNiagaraParameterCollection* Collection = CastChecked<UNiagaraParameterCollection>(CollectionAsset.GetAsset());
		if (Collection && Collection->GetNamespace() == *ParamCollection)
		{
			if (UNiagaraParameterCollectionInstance* NPCInst = FunctionInput->GetSystemViewModel()->GetSystem().GetParameterCollectionOverride(Collection))
			{
				//If we override this NPC then open the instance.
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NPCInst);
			}
			else
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Collection); 
			}
			
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

TSharedRef<SExpanderArrow> SNiagaraStackFunctionInputValue::CreateCustomNiagaraFunctionInputActionExpander(const FCustomExpanderData& ActionMenuData)
{
	return SNew(SNiagaraFunctionInputActionMenuExpander, ActionMenuData);
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::OnGetAvailableHandleMenu()
{
	SNiagaraFilterBox::FFilterOptions FilterOptions;
	FilterOptions.SetAddLibraryFilter(true);
	FilterOptions.SetAddSourceFilter(true);
	
	SAssignNew(FilterBox, SNiagaraFilterBox, FilterOptions)
	.bLibraryOnly(this, &SNiagaraStackFunctionInputValue::GetLibraryOnly)
	.OnLibraryOnlyChanged(this, &SNiagaraStackFunctionInputValue::SetLibraryOnly)
    .OnSourceFiltersChanged(this, &SNiagaraStackFunctionInputValue::TriggerRefresh);
	
	TSharedRef<SBorder> MenuWidget = SNew(SBorder)
	.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
	.Padding(5)
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
        [
			FilterBox.ToSharedRef()
        ]
		+SVerticalBox::Slot()
		[
			SNew(SBox)
			.WidthOverride(450)
			.HeightOverride(400)
			[
				SAssignNew(ActionSelector, SNiagaraMenuActionSelector)
				.Items(CollectActions())
				.OnGetCategoriesForItem(this, &SNiagaraStackFunctionInputValue::OnGetCategoriesForItem)
                .OnGetSectionsForItem(this, &SNiagaraStackFunctionInputValue::OnGetSectionsForItem)
                .OnCompareSectionsForEquality(this, &SNiagaraStackFunctionInputValue::OnCompareSectionsForEquality)
                .OnCompareSectionsForSorting(this, &SNiagaraStackFunctionInputValue::OnCompareSectionsForSorting)
                .OnCompareCategoriesForEquality(this, &SNiagaraStackFunctionInputValue::OnCompareCategoriesForEquality)
                .OnCompareCategoriesForSorting(this, &SNiagaraStackFunctionInputValue::OnCompareCategoriesForSorting)
                .OnCompareItemsForSorting(this, &SNiagaraStackFunctionInputValue::OnCompareItemsForSorting)
                .OnDoesItemMatchFilterText_Static(&FNiagaraEditorUtilities::DoesItemMatchFilterText)
                .OnGenerateWidgetForSection(this, &SNiagaraStackFunctionInputValue::OnGenerateWidgetForSection)
                .OnGenerateWidgetForCategory(this, &SNiagaraStackFunctionInputValue::OnGenerateWidgetForCategory)
                .OnGenerateWidgetForItem(this, &SNiagaraStackFunctionInputValue::OnGenerateWidgetForItem)
                .OnGetItemWeight_Static(&FNiagaraEditorUtilities::GetWeightForItem)
                .OnItemActivated(this, &SNiagaraStackFunctionInputValue::OnItemActivated)
                .AllowMultiselect(false)
                .OnDoesItemPassCustomFilter(this, &SNiagaraStackFunctionInputValue::DoesItemPassCustomFilter)
                .ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
                .ExpandInitially(false)
                .OnGetSectionData_Lambda([](const ENiagaraMenuSections& Section)
                {
                    if(Section == ENiagaraMenuSections::Suggested)
                    {
                        return SNiagaraMenuActionSelector::FSectionData(SNiagaraMenuActionSelector::FSectionData::List, true);
                    }

                    return SNiagaraMenuActionSelector::FSectionData(SNiagaraMenuActionSelector::FSectionData::Tree, false);
                })
			]
		]
	];

	SetFunctionInputButton->SetMenuContentWidgetToFocus(ActionSelector->GetSearchBox());
	return MenuWidget;
}

void SNiagaraStackFunctionInputValue::DynamicInputScriptSelected(UNiagaraScript* DynamicInputScript)
{
	FunctionInput->SetDynamicInput(DynamicInputScript);
}

void SNiagaraStackFunctionInputValue::CustomExpressionSelected()
{
	FText CustomHLSLComment = LOCTEXT("NewCustomExpressionComment", "Custom HLSL!");
	FunctionInput->SetCustomExpression(FHlslNiagaraTranslator::GetHlslDefaultForType(FunctionInput->GetInputType()) + TEXT(" /* ") + CustomHLSLComment.ToString() + TEXT(" */"));
}

void SNiagaraStackFunctionInputValue::CreateScratchSelected()
{
	FunctionInput->SetScratch();
}

void SNiagaraStackFunctionInputValue::ParameterHandleSelected(FNiagaraParameterHandle Handle)
{
	FunctionInput->SetLinkedValueHandle(Handle);
}

EVisibility SNiagaraStackFunctionInputValue::GetResetButtonVisibility() const
{
	return FunctionInput->CanReset() ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SNiagaraStackFunctionInputValue::GetDropdownButtonVisibility() const
{
	return FunctionInput->IsStaticParameter() ? EVisibility::Hidden : EVisibility::Visible;
}

FReply SNiagaraStackFunctionInputValue::ResetButtonPressed() const
{
	FunctionInput->Reset();
	return FReply::Handled();
}

EVisibility SNiagaraStackFunctionInputValue::GetResetToBaseButtonVisibility() const
{
	if (FunctionInput->HasBaseEmitter())
	{
		return FunctionInput->CanResetToBase() ? EVisibility::Visible : EVisibility::Hidden;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

FReply SNiagaraStackFunctionInputValue::ResetToBaseButtonPressed() const
{
	FunctionInput->ResetToBase();
	return FReply::Handled();
}

EVisibility SNiagaraStackFunctionInputValue::GetInputIconVisibility() const
{
	return FunctionInput->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local
		? EVisibility::Collapsed
		: EVisibility::Visible;
}

FText SNiagaraStackFunctionInputValue::GetInputIconText() const
{
	return FNiagaraStackEditorWidgetsUtilities::GetIconTextForInputMode(FunctionInput->GetValueMode());
}

FText SNiagaraStackFunctionInputValue::GetInputIconToolTip() const
{
	return FNiagaraStackEditorWidgetsUtilities::GetIconToolTipForInputMode(FunctionInput->GetValueMode());
}

FSlateColor SNiagaraStackFunctionInputValue::GetInputIconColor() const
{
	return FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForInputMode(FunctionInput->GetValueMode()));
}

FReply SNiagaraStackFunctionInputValue::OnFunctionInputDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FNiagaraParameterDragOperation>())
	{
		TSharedPtr<FNiagaraParameterDragOperation> InputDragDropOperation = StaticCastSharedPtr<FNiagaraParameterDragOperation>(DragDropOperation);
		TSharedPtr<FNiagaraParameterAction> Action = StaticCastSharedPtr<FNiagaraParameterAction>(InputDragDropOperation->GetSourceAction());
		if (Action.IsValid())
		{
			FunctionInput->SetLinkedValueHandle(FNiagaraParameterHandle(Action->GetParameter().GetName()));
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

bool SNiagaraStackFunctionInputValue::OnFunctionInputAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (FunctionInput && DragDropOperation->IsOfType<FNiagaraParameterDragOperation>())
	{
		if (FunctionInput->IsStaticParameter())
		{
			return false;
		}

		TSharedPtr<FNiagaraParameterDragOperation> InputDragDropOperation = StaticCastSharedPtr<FNiagaraParameterDragOperation>(DragDropOperation);
		TSharedPtr<FNiagaraParameterAction> Action = StaticCastSharedPtr<FNiagaraParameterAction>(InputDragDropOperation->GetSourceAction());
		if (Action->GetParameter().GetType() == FunctionInput->GetInputType() && FNiagaraStackGraphUtilities::ParameterAllowedInExecutionCategory(Action->GetParameter().GetName(), FunctionInput->GetExecutionCategoryName()))
		{
			return true;
		}
	}

	return false;
}

void ReassignDynamicInputScript(UNiagaraStackFunctionInput* FunctionInput, UNiagaraScript* NewDynamicInputScript)
{
	FunctionInput->ReassignDynamicInputScript(NewDynamicInputScript);
}

TArray<TSharedPtr<FNiagaraMenuAction_Generic>> SNiagaraStackFunctionInputValue::CollectDynamicInputActionsForReassign() const
{
	TArray<TSharedPtr<FNiagaraMenuAction_Generic>> DynamicInputActions;
	
	const FText CategoryName = LOCTEXT("DynamicInputValueCategory", "Dynamic Inputs");
	TArray<UNiagaraScript*> DynamicInputScripts;
	FunctionInput->GetAvailableDynamicInputs(DynamicInputScripts, true);
	for (UNiagaraScript* DynamicInputScript : DynamicInputScripts)
	{
		FVersionedNiagaraScriptData* ScriptData = DynamicInputScript->GetLatestScriptData();
		bool bIsInLibrary = ScriptData->LibraryVisibility == ENiagaraScriptLibraryVisibility::Library;
		const FText DisplayName = FNiagaraEditorUtilities::FormatScriptName(DynamicInputScript->GetFName(), bIsInLibrary);
		const FText Tooltip = FNiagaraEditorUtilities::FormatScriptDescription(ScriptData->Description, *DynamicInputScript->GetPathName(), bIsInLibrary);
		TTuple<EScriptSource, FText> Source = FNiagaraEditorUtilities::GetScriptSource(FAssetData(DynamicInputScript));
		
		TSharedPtr<FNiagaraMenuAction_Generic> DynamicInputAction(new FNiagaraMenuAction_Generic(
			FNiagaraMenuAction_Generic::FOnExecuteAction::CreateStatic(&ReassignDynamicInputScript, FunctionInput, DynamicInputScript),
			DisplayName, ScriptData->bSuggested ? ENiagaraMenuSections::Suggested : ENiagaraMenuSections::General, {CategoryName.ToString()}, Tooltip, ScriptData->Keywords
            ));
		DynamicInputAction->SourceData = FNiagaraActionSourceData(Source.Key, Source.Value, true);
		DynamicInputAction->bIsInLibrary = ScriptData->LibraryVisibility == ENiagaraScriptLibraryVisibility::Library;

		DynamicInputActions.Add(DynamicInputAction);
	}

	return DynamicInputActions;
}

void SNiagaraStackFunctionInputValue::ShowReassignDynamicInputScriptMenu()
{
	SNiagaraFilterBox::FFilterOptions FilterOptions;
	FilterOptions.SetAddLibraryFilter(true);
	FilterOptions.SetAddSourceFilter(true);
	
	SAssignNew(FilterBox, SNiagaraFilterBox, FilterOptions)
	.bLibraryOnly(this, &SNiagaraStackFunctionInputValue::GetLibraryOnly)
	.OnLibraryOnlyChanged(this, &SNiagaraStackFunctionInputValue::SetLibraryOnly)
	.OnSourceFiltersChanged(this, &SNiagaraStackFunctionInputValue::TriggerRefresh);
	
	TSharedRef<SBorder> MenuWidget = SNew(SBorder)
	.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
	.Padding(5)
	[
		SNew(SVerticalBox)		
		+SVerticalBox::Slot()
		.AutoHeight()
        [
			FilterBox.ToSharedRef()
        ]
		+SVerticalBox::Slot()
		[
			SNew(SBox)
			.WidthOverride(450)
			.HeightOverride(400)
			[
				SAssignNew(ActionSelector, SNiagaraMenuActionSelector)
				.Items(CollectDynamicInputActionsForReassign())
				.OnGetCategoriesForItem(this, &SNiagaraStackFunctionInputValue::OnGetCategoriesForItem)
                .OnGetSectionsForItem(this, &SNiagaraStackFunctionInputValue::OnGetSectionsForItem)
                .OnCompareSectionsForEquality(this, &SNiagaraStackFunctionInputValue::OnCompareSectionsForEquality)
                .OnCompareSectionsForSorting(this, &SNiagaraStackFunctionInputValue::OnCompareSectionsForSorting)
                .OnCompareCategoriesForEquality(this, &SNiagaraStackFunctionInputValue::OnCompareCategoriesForEquality)
                .OnCompareCategoriesForSorting(this, &SNiagaraStackFunctionInputValue::OnCompareCategoriesForSorting)
                .OnCompareItemsForSorting(this, &SNiagaraStackFunctionInputValue::OnCompareItemsForSorting)
                .OnDoesItemMatchFilterText_Static(&FNiagaraEditorUtilities::DoesItemMatchFilterText)
                .OnGenerateWidgetForSection(this, &SNiagaraStackFunctionInputValue::OnGenerateWidgetForSection)
                .OnGenerateWidgetForCategory(this, &SNiagaraStackFunctionInputValue::OnGenerateWidgetForCategory)
                .OnGenerateWidgetForItem(this, &SNiagaraStackFunctionInputValue::OnGenerateWidgetForItem)
                .OnGetItemWeight_Static(&FNiagaraEditorUtilities::GetWeightForItem)
                .OnItemActivated(this, &SNiagaraStackFunctionInputValue::OnItemActivated)
                .AllowMultiselect(false)
                .OnDoesItemPassCustomFilter(this, &SNiagaraStackFunctionInputValue::DoesItemPassCustomFilter)
                .ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
                .ExpandInitially(false)
                .OnGetSectionData_Lambda([](const ENiagaraMenuSections& Section)
                {
                    if(Section == ENiagaraMenuSections::Suggested)
                    {
                        return SNiagaraMenuActionSelector::FSectionData(SNiagaraMenuActionSelector::FSectionData::List, true);
                    }

                    return SNiagaraMenuActionSelector::FSectionData(SNiagaraMenuActionSelector::FSectionData::Tree, false);
                })
			]
		]
	];

	FGeometry ThisGeometry = GetCachedGeometry();
	bool bAutoAdjustForDpiScale = false; // Don't adjust for dpi scale because the push menu command is expecting an unscaled position.
	FVector2D MenuPosition = FSlateApplication::Get().CalculatePopupWindowPosition(ThisGeometry.GetLayoutBoundingRect(), MenuWidget->GetDesiredSize(), bAutoAdjustForDpiScale);
	FSlateApplication::Get().PushMenu(AsShared(), FWidgetPath(), MenuWidget, MenuPosition, FPopupTransitionEffect::ContextMenu);
}

bool SNiagaraStackFunctionInputValue::GetLibraryOnly() const
{
	return bLibraryOnly;
}

void SNiagaraStackFunctionInputValue::SetLibraryOnly(bool bInIsLibraryOnly)
{
	bLibraryOnly = bInIsLibraryOnly;
	ActionSelector->RefreshAllCurrentItems(true);
}

FReply SNiagaraStackFunctionInputValue::ScratchButtonPressed() const
{
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScratchDynamicInputViewModel = 
		FunctionInput->GetSystemViewModel()->GetScriptScratchPadViewModel()->GetViewModelForScript(FunctionInput->GetDynamicInputNode()->FunctionScript);
	if (ScratchDynamicInputViewModel.IsValid())
	{
		FunctionInput->GetSystemViewModel()->GetScriptScratchPadViewModel()->FocusScratchPadScriptViewModel(ScratchDynamicInputViewModel.ToSharedRef());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TArray<TSharedPtr<FNiagaraMenuAction_Generic>> SNiagaraStackFunctionInputValue::CollectActions()
{
	TArray<TSharedPtr<FNiagaraMenuAction_Generic>> OutAllActions;
	bool bIsDataInterfaceOrObject = FunctionInput->GetInputType().IsDataInterface() || FunctionInput->GetInputType().IsUObject();

	FNiagaraActionSourceData NiagaraSourceData(EScriptSource::Niagara, FText::FromString(TEXT("Niagara")), true);
	
	// Set a local value
	if(bIsDataInterfaceOrObject == false)
	{
		bool bCanSetLocalValue = FunctionInput->GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Local;

		const FText DisplayName = LOCTEXT("LocalValue", "New Local Value");
		const FText Tooltip = FText::Format(LOCTEXT("LocalValueToolTip", "Set a local editable value for this input."), DisplayName);
		TSharedPtr<FNiagaraMenuAction_Generic> SetLocalValueAction(new FNiagaraMenuAction_Generic(
			FNiagaraMenuAction_Generic::FOnExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::SetToLocalValue),
			FNiagaraMenuAction_Generic::FCanExecuteAction::CreateLambda([=]() { return bCanSetLocalValue; }),
            DisplayName, ENiagaraMenuSections::General, {}, Tooltip, FText()));
		SetLocalValueAction->SourceData = NiagaraSourceData;
		OutAllActions.Add(SetLocalValueAction);
	}

	// Add a dynamic input
	{
		const FText CategoryName = LOCTEXT("DynamicInputValueCategory", "Dynamic Inputs");
		TArray<UNiagaraScript*> DynamicInputScripts;
		FunctionInput->GetAvailableDynamicInputs(DynamicInputScripts, bLibraryOnly == false);

		for (UNiagaraScript* DynamicInputScript : DynamicInputScripts)
		{
			TTuple<EScriptSource, FText> Source = FNiagaraEditorUtilities::GetScriptSource(DynamicInputScript);
			
			FVersionedNiagaraScriptData* ScriptData = DynamicInputScript->GetLatestScriptData();
			bool bIsInLibrary = ScriptData->LibraryVisibility == ENiagaraScriptLibraryVisibility::Library;
			const FText DisplayName = FNiagaraEditorUtilities::FormatScriptName(DynamicInputScript->GetFName(), bIsInLibrary);
			const FText Tooltip = FNiagaraEditorUtilities::FormatScriptDescription(ScriptData->Description, *DynamicInputScript->GetPathName(), bIsInLibrary);

			TSharedPtr<FNiagaraMenuAction_Generic> DynamicInputAction(new FNiagaraMenuAction_Generic(
                FNiagaraMenuAction_Generic::FOnExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::DynamicInputScriptSelected, DynamicInputScript),
                DisplayName, ScriptData->bSuggested ? ENiagaraMenuSections::Suggested : ENiagaraMenuSections::General, {CategoryName.ToString()}, Tooltip, ScriptData->Keywords));
			
			DynamicInputAction->SourceData = FNiagaraActionSourceData(Source.Key, Source.Value, true);
			DynamicInputAction->bIsExperimental = ScriptData->bExperimental;
			DynamicInputAction->bIsInLibrary = bIsInLibrary;
			OutAllActions.Add(DynamicInputAction);
		}
	}

	// Link existing attribute
	TArray<FNiagaraParameterHandle> AvailableHandles;
	FunctionInput->GetAvailableParameterHandles(AvailableHandles);

	const FString RootCategoryName = FString("Link Inputs");
	const FText MapInputFormat = LOCTEXT("LinkInputFormat", "Link this input to {0}");
	for (const FNiagaraParameterHandle& AvailableHandle : AvailableHandles)
	{
		TArray<FName> HandleParts = AvailableHandle.GetHandleParts();
		FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(HandleParts);
		if (NamespaceMetadata.IsValid())
		{			
			// Only add handles which are in known namespaces to prevent collecting parameter handles
			// which are being used to configure modules and dynamic inputs in the stack graphs.
			const FText Category = NamespaceMetadata.DisplayName;
			const FText DisplayName = FText::FromName(AvailableHandle.GetParameterHandleString());
			const FText Tooltip = FText::Format(MapInputFormat, FText::FromName(AvailableHandle.GetParameterHandleString()));
			
			TSharedPtr<FNiagaraMenuAction_Generic> LinkAction(new FNiagaraMenuAction_Generic(
				FNiagaraMenuAction_Generic::FOnExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::ParameterHandleSelected, AvailableHandle),
				DisplayName, ENiagaraMenuSections::General, {RootCategoryName, Category.ToString()}, Tooltip, FText()));
			
			LinkAction->SetParameterVariable(FNiagaraVariable(FunctionInput->GetInputType(), AvailableHandle.GetParameterHandleString()));
			LinkAction->SourceData = NiagaraSourceData;

			OutAllActions.Add(LinkAction);
		}
	}

	// Read from new attribute
	{
		const FText CategoryName = LOCTEXT("MakeCategory", "Make");

		TArray<FName> AvailableNamespaces;
		FunctionInput->GetNamespacesForNewReadParameters(AvailableNamespaces);

		TArray<FString> InputNames;
		for (int32 i = FunctionInput->GetInputParameterHandlePath().Num() - 1; i >= 0; i--)
		{
			InputNames.Add(FunctionInput->GetInputParameterHandlePath()[i].GetName().ToString());
		}
		FName InputName = *FString::Join(InputNames, TEXT("_")).Replace(TEXT("."), TEXT("_"));

		for (const FName& AvailableNamespace : AvailableNamespaces)
		{
			FNiagaraParameterHandle HandleToRead(AvailableNamespace, InputName);
			bool bIsContained = AvailableHandles.Contains(HandleToRead);

			if(bIsContained)
			{
				TSet<FName> ExistingNames;
				for(const FNiagaraParameterHandle& Handle : AvailableHandles)
				{
					ExistingNames.Add(Handle.GetName());
				}

				// let's get a unique name as the previous parameter already existed
				HandleToRead = FNiagaraParameterHandle(AvailableNamespace, FNiagaraUtilities::GetUniqueName(InputName, ExistingNames));
			}
			
			FFormatNamedArguments Args;
			Args.Add(TEXT("AvailableNamespace"), FText::FromName(AvailableNamespace));

			const FText DisplayName = FText::Format(LOCTEXT("ReadLabelFormat", "Read from new {AvailableNamespace} parameter"), Args);
			const FText Tooltip = FText::Format(LOCTEXT("ReadToolTipFormat", "Read this input from a new parameter in the {AvailableNamespace} namespace."), Args);

			TSharedPtr<FNiagaraMenuAction_Generic> MakeAction(new FNiagaraMenuAction_Generic(
				FNiagaraMenuAction_Generic::FOnExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::ParameterHandleSelected, HandleToRead),         
		        DisplayName, ENiagaraMenuSections::General, {CategoryName.ToString()}, Tooltip, FText()));

			MakeAction->SourceData = NiagaraSourceData;

			OutAllActions.Add(MakeAction);
		}
	}

	if (bIsDataInterfaceOrObject == false)
	{
		// Leaving the internal usage of bIsDataInterfaceObject that the tooltip and disabling will work properly when they're moved out of a graph action menu.
		const FText DisplayName = LOCTEXT("ExpressionLabel", "New Expression");
		const FText Tooltip = bIsDataInterfaceOrObject
			? LOCTEXT("NoExpresionsForObjects", "Expressions can not be used to set object or data interface parameters.")
			: LOCTEXT("ExpressionToolTipl", "Resolve this variable with a custom expression.");

		TSharedPtr<FNiagaraMenuAction_Generic> ExpressionAction(new FNiagaraMenuAction_Generic(
                FNiagaraMenuAction_Generic::FOnExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::CustomExpressionSelected),
                FNiagaraMenuAction_Generic::FCanExecuteAction::CreateLambda([bIsDataInterfaceOrObject]() { return bIsDataInterfaceOrObject == false; }),
                DisplayName, ENiagaraMenuSections::General, {}, Tooltip, FText()));

		ExpressionAction->SourceData = NiagaraSourceData;

		OutAllActions.Add(ExpressionAction);
	}

	if (bIsDataInterfaceOrObject == false)
	{
		// Leaving the internal usage of bIsDataInterfaceObject that the tooltip and disabling will work properly when they're moved out of a graph action menu.
		const FText DisplayName = LOCTEXT("ScratchLabel", "New Scratch Dynamic Input");
		const FText Tooltip = bIsDataInterfaceOrObject
			? LOCTEXT("NoScratchForObjects", "Dynamic inputs can not be used to set object or data interface parameters.")
			: LOCTEXT("ScratchToolTipl", "Create a new dynamic input in the scratch pad.");

		TSharedPtr<FNiagaraMenuAction_Generic> CreateScratchAction(new FNiagaraMenuAction_Generic(
           FNiagaraMenuAction_Generic::FOnExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputValue::CreateScratchSelected),
           FNiagaraMenuAction_Generic::FCanExecuteAction::CreateLambda([bIsDataInterfaceOrObject]() { return bIsDataInterfaceOrObject == false; }),
           DisplayName, ENiagaraMenuSections::General, {}, Tooltip, FText()));

		CreateScratchAction->SourceData = NiagaraSourceData;

		OutAllActions.Add(CreateScratchAction);
	}

	if (FunctionInput->CanDeleteInput())
	{
		const FText DisplayName = LOCTEXT("DeleteInput", "Remove this input");
		const FText Tooltip = FText::Format(LOCTEXT("DeleteInputTooltip", "Remove input from module."), DisplayName);

		TSharedPtr<FNiagaraMenuAction_Generic> DeleteInputAction(new FNiagaraMenuAction_Generic(
                FNiagaraMenuAction_Generic::FOnExecuteAction::CreateUObject(FunctionInput, &UNiagaraStackFunctionInput::DeleteInput),
                FNiagaraMenuAction_Generic::FCanExecuteAction::CreateUObject(FunctionInput, &UNiagaraStackFunctionInput::CanDeleteInput),
                DisplayName, ENiagaraMenuSections::General, {}, Tooltip, FText()));

		DeleteInputAction->SourceData = NiagaraSourceData;
		OutAllActions.Add(DeleteInputAction);
	}

	return OutAllActions;
}

TArray<FString> SNiagaraStackFunctionInputValue::OnGetCategoriesForItem(
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	return Item->Categories;
}

TArray<ENiagaraMenuSections> SNiagaraStackFunctionInputValue::OnGetSectionsForItem(
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	if(Item->Section == ENiagaraMenuSections::Suggested)
	{
		return { ENiagaraMenuSections::General, ENiagaraMenuSections::Suggested };
	}
		
	return {Item->Section};
}

bool SNiagaraStackFunctionInputValue::OnCompareSectionsForEquality(const ENiagaraMenuSections& SectionA,
	const ENiagaraMenuSections& SectionB)
{
	return SectionA == SectionB;
}

bool SNiagaraStackFunctionInputValue::OnCompareSectionsForSorting(const ENiagaraMenuSections& SectionA,
	const ENiagaraMenuSections& SectionB)
{
	return SectionA < SectionB;
}

bool SNiagaraStackFunctionInputValue::OnCompareCategoriesForEquality(const FString& CategoryA, const FString& CategoryB)
{
	return CategoryA.Compare(CategoryB) == 0;
}

bool SNiagaraStackFunctionInputValue::OnCompareCategoriesForSorting(const FString& CategoryA, const FString& CategoryB)
{
	return CategoryA.Compare(CategoryB) == -1;
}

bool SNiagaraStackFunctionInputValue::OnCompareItemsForEquality(const TSharedPtr<FNiagaraMenuAction_Generic>& ItemA,
	const TSharedPtr<FNiagaraMenuAction_Generic>& ItemB)
{
	return ItemA->DisplayName.EqualTo(ItemB->DisplayName);
}

bool SNiagaraStackFunctionInputValue::OnCompareItemsForSorting(const TSharedPtr<FNiagaraMenuAction_Generic>& ItemA,
	const TSharedPtr<FNiagaraMenuAction_Generic>& ItemB)
{
	return ItemA->DisplayName.CompareTo(ItemB->DisplayName) == -1;
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::OnGenerateWidgetForSection(const ENiagaraMenuSections& Section)
{
	UEnum* SectionEnum = StaticEnum<ENiagaraMenuSections>();
	FText TextContent = SectionEnum->GetDisplayNameTextByValue((int64) Section);
	
	return SNew(STextBlock)
        .Text(TextContent)
        .TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetCategoryText");
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::OnGenerateWidgetForCategory(const FString& Category)
{
	FText TextContent = FText::FromString(Category);

	return SNew(SRichTextBlock)
        .Text(TextContent)
        .DecoratorStyleSet(&FEditorStyle::Get())
        .TextStyle(FNiagaraEditorStyle::Get(), "ActionMenu.HeadingTextBlock");
}

TSharedRef<SWidget> SNiagaraStackFunctionInputValue::OnGenerateWidgetForItem(
	const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	FCreateNiagaraWidgetForActionData ActionData(Item);
	ActionData.HighlightText = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &SNiagaraStackFunctionInputValue::GetFilterText));
	return SNew(SNiagaraActionWidget, ActionData).bShowTypeIfParameter(false);
}

bool SNiagaraStackFunctionInputValue::DoesItemPassCustomFilter(const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	bool bLibraryConditionFulfilled = (bLibraryOnly && Item->bIsInLibrary) || !bLibraryOnly;
	return FilterBox->IsSourceFilterActive(Item->SourceData.Source) && bLibraryConditionFulfilled;
}

void SNiagaraStackFunctionInputValue::OnItemActivated(const TSharedPtr<FNiagaraMenuAction_Generic>& Item)
{
	TSharedPtr<FNiagaraMenuAction_Generic> CurrentAction = StaticCastSharedPtr<FNiagaraMenuAction_Generic>(Item);

	if (CurrentAction.IsValid())
	{
		FSlateApplication::Get().DismissAllMenus();
		CurrentAction->Execute();
	}

	ActionSelector.Reset();
	FilterBox.Reset();
}

void SNiagaraStackFunctionInputValue::TriggerRefresh(const TMap<EScriptSource, bool>& SourceState)
{
	ActionSelector->RefreshAllCurrentItems();

	TArray<bool> States;
	SourceState.GenerateValueArray(States);

	int32 NumActive = 0;
	for(bool& State : States)
	{
		if(State == true)
		{
			NumActive++;
		}
	}

	ActionSelector->ExpandTree();
}

#undef LOCTEXT_NAMESPACE
