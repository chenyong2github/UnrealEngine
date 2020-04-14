// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackFunctionInputValue.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "NiagaraEditorModule.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraActions.h"
#include "SNiagaraParameterEditor.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IStructureDetailsView.h"
#include "SDropTarget.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "NiagaraParameterCollection.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystem.h"
#include "SNiagaraGraphActionWidget.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorFontGlyphs.h"
#include "Widgets/SNiagaraLibraryOnlyToggleHeader.h"
#include "Widgets/SNiagaraParameterName.h"
#include "NiagaraEditorSettings.h"

#define LOCTEXT_NAMESPACE "NiagaraStackFunctionInputValue"

const float TextIconSize = 16;

bool SNiagaraStackFunctionInputValue::bLibraryOnly = true;

void SNiagaraStackFunctionInputValue::Construct(const FArguments& InArgs, UNiagaraStackFunctionInput* InFunctionInput)
{
	FunctionInput = InFunctionInput;
	FunctionInput->OnValueChanged().AddSP(this, &SNiagaraStackFunctionInputValue::OnInputValueChanged);

	FMargin ItemPadding = FMargin(0);
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
	if (FunctionInput->GetDynamicInputNode() != nullptr)
	{
		return FText::FromString(FName::NameToDisplayString(FunctionInput->GetDynamicInputNode()->GetFunctionName(), false));
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
	TSharedPtr<SNiagaraLibraryOnlyToggleHeader> LibraryOnlyToggle;
	TSharedPtr<SGraphActionMenu> GraphActionMenu;

	TSharedRef<SBorder> MenuWidget = SNew(SBorder)
	.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
	.Padding(5)
	[
		SNew(SBox)
		.WidthOverride(300)
		.HeightOverride(400)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(1.0f)
			[
				SAssignNew(LibraryOnlyToggle, SNiagaraLibraryOnlyToggleHeader)
				.HeaderLabelText(LOCTEXT("FunctionInputValueTitle", "Edit value"))
				.LibraryOnly(this, &SNiagaraStackFunctionInputValue::GetLibraryOnly)
				.LibraryOnlyChanged(this, &SNiagaraStackFunctionInputValue::SetLibraryOnly)
			]
			+SVerticalBox::Slot()
			.FillHeight(15)
			[
				SAssignNew(GraphActionMenu, SGraphActionMenu)
				.OnActionSelected(this, &SNiagaraStackFunctionInputValue::OnActionSelected)
				.OnCollectAllActions(this, &SNiagaraStackFunctionInputValue::CollectAllActions)
				.AutoExpandActionMenu(false)
				.ShowFilterTextBox(true)
				.OnCreateCustomRowExpander_Static(&CreateCustomNiagaraFunctionInputActionExpander)
				.OnCreateWidgetForAction_Lambda([](const FCreateWidgetForActionData* InData)
				{
					return SNew(SNiagaraGraphActionWidget, InData);
				})
			]
		]
	];

	LibraryOnlyToggle->SetActionMenu(GraphActionMenu.ToSharedRef());
	SetFunctionInputButton->SetMenuContentWidgetToFocus(GraphActionMenu->GetFilterTextBox()->AsShared());
	return MenuWidget;
}

void SNiagaraStackFunctionInputValue::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || SelectedActions.Num() == 0)
	{
		for (int32 ActionIndex = 0; ActionIndex < SelectedActions.Num(); ActionIndex++)
		{
			TSharedPtr<FNiagaraMenuAction> CurrentAction = StaticCastSharedPtr<FNiagaraMenuAction>(SelectedActions[ActionIndex]);

			if (CurrentAction.IsValid())
			{
				FSlateApplication::Get().DismissAllMenus();
				CurrentAction->ExecuteAction();
			}
		}
	}
}

void SNiagaraStackFunctionInputValue::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
{
	bool bIsDataInterfaceOrObject = FunctionInput->GetInputType().IsDataInterface() || FunctionInput->GetInputType().IsUObject();

	// Set a local value
	if(bIsDataInterfaceOrObject == false)
	{
		bool bCanSetLocalValue = FunctionInput->GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Local;

		const FText NameText = LOCTEXT("LocalValue", "New Local Value");
		const FText Tooltip = FText::Format(LOCTEXT("LocalValueToolTip", "Set a local editable value for this input."), NameText);
		TSharedPtr<FNiagaraMenuAction> SetLocalValueAction(
			new FNiagaraMenuAction(FText(), NameText, Tooltip, 0, FText(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraStackFunctionInputValue::SetToLocalValue),
				FNiagaraMenuAction::FCanExecuteStackAction::CreateLambda([=]() { return bCanSetLocalValue; })));
		OutAllActions.AddAction(SetLocalValueAction);
	}


	// Add a dynamic input
	{
		const FText CategoryName = LOCTEXT("DynamicInputValueCategory", "Dynamic Inputs");
		TArray<UNiagaraScript*> DynamicInputScripts;
		FunctionInput->GetAvailableDynamicInputs(DynamicInputScripts, bLibraryOnly == false);
		for (UNiagaraScript* DynamicInputScript : DynamicInputScripts)
		{
			const FText DynamicInputText = FNiagaraEditorUtilities::FormatScriptName(DynamicInputScript->GetFName(), DynamicInputScript->bExposeToLibrary);
			const FText Tooltip = FNiagaraEditorUtilities::FormatScriptDescription(DynamicInputScript->Description, *DynamicInputScript->GetPathName(), DynamicInputScript->bExposeToLibrary);
			TSharedPtr<FNiagaraMenuAction> DynamicInputAction(new FNiagaraMenuAction(CategoryName, DynamicInputText, Tooltip, 0, DynamicInputScript->Keywords,
				FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraStackFunctionInputValue::DynamicInputScriptSelected, DynamicInputScript)));

			DynamicInputAction->IsExperimental = DynamicInputScript->bExperimental;
			OutAllActions.AddAction(DynamicInputAction);
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
			TSharedPtr<FNiagaraMenuAction> LinkAction(new FNiagaraMenuAction(Category, DisplayName, Tooltip, 0, FText(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraStackFunctionInputValue::ParameterHandleSelected, AvailableHandle)));
			LinkAction->SetParamterVariable(FNiagaraVariable(FunctionInput->GetInputType(), AvailableHandle.GetParameterHandleString()));
			OutAllActions.AddAction(LinkAction, RootCategoryName);
		}
	}

	// Read from new attribute
	{
		const FText CategoryName = LOCTEXT("MakeCategory", "Make");

		TArray<FName> AvailableNamespaces;
		FunctionInput->GetNamespacesForNewParameters(AvailableNamespaces);

		TArray<FString> InputNames;
		for (int32 i = FunctionInput->GetInputParameterHandlePath().Num() - 1; i >= 0; i--)
		{
			InputNames.Add(FunctionInput->GetInputParameterHandlePath()[i].GetName().ToString());
		}
		FName InputName = *FString::Join(InputNames, TEXT("_"));

		for (const FName AvailableNamespace : AvailableNamespaces)
		{
			FNiagaraParameterHandle HandleToRead(AvailableNamespace, InputName);
			bool bCanExecute = AvailableHandles.Contains(HandleToRead) == false;

			FFormatNamedArguments Args;
			Args.Add(TEXT("AvailableNamespace"), FText::FromName(AvailableNamespace));

			const FText DisplayName = FText::Format(LOCTEXT("ReadLabelFormat", "Read from new {AvailableNamespace} parameter"), Args);
			const FText Tooltip = FText::Format(LOCTEXT("ReadToolTipFormat", "Read this input from a new parameter in the {AvailableNamespace} namespace."), Args);
			TSharedPtr<FNiagaraMenuAction> MakeAction(
				new FNiagaraMenuAction(CategoryName, DisplayName, Tooltip, 0, FText(),
					FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraStackFunctionInputValue::ParameterHandleSelected, HandleToRead),
					FNiagaraMenuAction::FCanExecuteStackAction::CreateLambda([=]() { return bCanExecute; })));
			OutAllActions.AddAction(MakeAction);
		}
	}

	if (bIsDataInterfaceOrObject == false)
	{
		// Leaving the internal usage of bIsDataInterfaceObject that the tooltip and disabling will work properly when they're moved out of a graph action menu.
		const FText DisplayName = LOCTEXT("ExpressionLabel", "New Expression");
		const FText Tooltip = bIsDataInterfaceOrObject
			? LOCTEXT("NoExpresionsForObjects", "Expressions can not be used to set object or data interface parameters.")
			: LOCTEXT("ExpressionToolTipl", "Resolve this variable with a custom expression.");
		TSharedPtr<FNiagaraMenuAction> ExpressionAction(new FNiagaraMenuAction(FText(), DisplayName, Tooltip, 0, FText(),
			FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraStackFunctionInputValue::CustomExpressionSelected),
			FNiagaraMenuAction::FCanExecuteStackAction::CreateLambda([bIsDataInterfaceOrObject]() { return bIsDataInterfaceOrObject == false; })));
		OutAllActions.AddAction(ExpressionAction);
	}

	if (bIsDataInterfaceOrObject == false)
	{
		// Leaving the internal usage of bIsDataInterfaceObject that the tooltip and disabling will work properly when they're moved out of a graph action menu.
		const FText CreateDisplayName = LOCTEXT("ScratchLabel", "New Scratch Dynamic Input");
		const FText CreateTooltip = bIsDataInterfaceOrObject
			? LOCTEXT("NoScratchForObjects", "Dynamic inputs can not be used to set object or data interface parameters.")
			: LOCTEXT("ScratchToolTipl", "Create a new dynamic input in the scratch pad.");
		TSharedPtr<FNiagaraMenuAction> CreateScratchAction(new FNiagaraMenuAction(FText(), CreateDisplayName, CreateTooltip, 0, FText(),
			FNiagaraMenuAction::FOnExecuteStackAction::CreateSP(this, &SNiagaraStackFunctionInputValue::CreateScratchSelected),
			FNiagaraMenuAction::FCanExecuteStackAction::CreateLambda([bIsDataInterfaceOrObject]() { return bIsDataInterfaceOrObject == false; })));
		OutAllActions.AddAction(CreateScratchAction);
	}

	if (FunctionInput->CanDeleteInput())
	{
		const FText NameText = LOCTEXT("DeleteInput", "Remove this input");
		const FText Tooltip = FText::Format(LOCTEXT("DeleteInputTooltip", "Remove input from module."), NameText);
		TSharedPtr<FNiagaraMenuAction> SetLocalValueAction(
			new FNiagaraMenuAction(FText::GetEmpty(), NameText, Tooltip, 0, FText::GetEmpty(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateUObject(FunctionInput, &UNiagaraStackFunctionInput::DeleteInput),
				FNiagaraMenuAction::FCanExecuteStackAction::CreateUObject(FunctionInput, &UNiagaraStackFunctionInput::CanDeleteInput)));
		OutAllActions.AddAction(SetLocalValueAction);
	}
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
	switch (FunctionInput->GetValueMode())
	{
	case UNiagaraStackFunctionInput::EValueMode::Linked:
		return FEditorFontGlyphs::Link;
	case UNiagaraStackFunctionInput::EValueMode::Data:
		return FEditorFontGlyphs::Database;
	case UNiagaraStackFunctionInput::EValueMode::Dynamic:
		return FEditorFontGlyphs::Line_Chart;
	case UNiagaraStackFunctionInput::EValueMode::Expression:
		return FEditorFontGlyphs::Terminal;
	case UNiagaraStackFunctionInput::EValueMode::DefaultFunction:
		return FEditorFontGlyphs::Plug;
	case UNiagaraStackFunctionInput::EValueMode::InvalidOverride:
		return FEditorFontGlyphs::Question;
	case UNiagaraStackFunctionInput::EValueMode::UnsupportedDefault:
		return FEditorFontGlyphs::Star;
	default:
		return FText::FromString(FString(TEXT("\xf128") /* fa-question */));
	}
}

FText SNiagaraStackFunctionInputValue::GetInputIconToolTip() const
{
	static const FText InvalidText = LOCTEXT("InvalidInputIconToolTip", "Unsupported value.  Check the graph for issues.");
	switch (FunctionInput->GetValueMode())
	{
	case UNiagaraStackFunctionInput::EValueMode::Linked:
		return LOCTEXT("LinkInputIconToolTip", "Linked Value");
	case UNiagaraStackFunctionInput::EValueMode::Data:
		return LOCTEXT("DataInterfaceInputIconToolTip", "Data Value");
	case UNiagaraStackFunctionInput::EValueMode::Dynamic:
		return LOCTEXT("DynamicInputIconToolTip", "Dynamic Value");
	case UNiagaraStackFunctionInput::EValueMode::Expression:
		return LOCTEXT("ExpressionInputIconToolTip", "Custom Expression");
	case UNiagaraStackFunctionInput::EValueMode::DefaultFunction:
		return LOCTEXT("DefaultFunctionIconToolTip", "Script Defined Default Function");
	case UNiagaraStackFunctionInput::EValueMode::InvalidOverride:
		return LOCTEXT("InvalidOverrideIconToolTip", "Invalid Script State");
	case UNiagaraStackFunctionInput::EValueMode::UnsupportedDefault:
		return LOCTEXT("UnsupportedDefaultIconToolTip", "Script Defined Custom Default");
	default:
		return InvalidText;
	}
}

FSlateColor SNiagaraStackFunctionInputValue::GetInputIconColor() const
{
	switch (FunctionInput->GetValueMode())
	{
	case UNiagaraStackFunctionInput::EValueMode::Linked:
		return FLinearColor(FColor::Purple);
	case UNiagaraStackFunctionInput::EValueMode::Data:
		return FLinearColor(FColor::Yellow);
	case UNiagaraStackFunctionInput::EValueMode::Dynamic:
		return FLinearColor(FColor::Cyan);
	case UNiagaraStackFunctionInput::EValueMode::Expression:
		return FLinearColor(FColor::Green);
	case UNiagaraStackFunctionInput::EValueMode::InvalidOverride:
	case UNiagaraStackFunctionInput::EValueMode::UnsupportedDefault:
	case UNiagaraStackFunctionInput::EValueMode::DefaultFunction:
	default:
		return FLinearColor(FColor::White);
	}
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

void SNiagaraStackFunctionInputValue::CollectDynamicInputActionsForReassign(FGraphActionListBuilderBase& DynamicInputActions) const
{
	const FText CategoryName = LOCTEXT("DynamicInputValueCategory", "Dynamic Inputs");
	TArray<UNiagaraScript*> DynamicInputScripts;
	FunctionInput->GetAvailableDynamicInputs(DynamicInputScripts, bLibraryOnly == false);
	for (UNiagaraScript* DynamicInputScript : DynamicInputScripts)
	{
		const FText DynamicInputText = FNiagaraEditorUtilities::FormatScriptName(DynamicInputScript->GetFName(), DynamicInputScript->bExposeToLibrary);
		const FText Tooltip = FNiagaraEditorUtilities::FormatScriptDescription(DynamicInputScript->Description, *DynamicInputScript->GetPathName(), DynamicInputScript->bExposeToLibrary);
		TSharedPtr<FNiagaraMenuAction> DynamicInputAction(new FNiagaraMenuAction(CategoryName, DynamicInputText, Tooltip, 0, DynamicInputScript->Keywords,
			FNiagaraMenuAction::FOnExecuteStackAction::CreateStatic(&ReassignDynamicInputScript, FunctionInput, DynamicInputScript)));
		DynamicInputActions.AddAction(DynamicInputAction);
	}
}

void SNiagaraStackFunctionInputValue::ShowReassignDynamicInputScriptMenu()
{
	TSharedPtr<SNiagaraLibraryOnlyToggleHeader> LibraryOnlyToggle;
	TSharedPtr<SGraphActionMenu> GraphActionMenu;

	TSharedRef<SBorder> MenuWidget = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(300)
			.HeightOverride(400)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.Padding(1.0f)
				[
					SAssignNew(LibraryOnlyToggle, SNiagaraLibraryOnlyToggleHeader)
					.HeaderLabelText(LOCTEXT("ReassignDynamicInputLabel", "Select a new dynamic input"))
					.LibraryOnly(this, &SNiagaraStackFunctionInputValue::GetLibraryOnly)
					.LibraryOnlyChanged(this, &SNiagaraStackFunctionInputValue::SetLibraryOnly)
				]
				+SVerticalBox::Slot()
				.FillHeight(15)
				[
					SAssignNew(GraphActionMenu, SGraphActionMenu)
					.OnActionSelected(this, &SNiagaraStackFunctionInputValue::OnActionSelected)
					.OnCollectAllActions(this, &SNiagaraStackFunctionInputValue::CollectDynamicInputActionsForReassign)
					.AutoExpandActionMenu(true)
					.ShowFilterTextBox(true)
					.OnCreateCustomRowExpander_Static(&CreateCustomNiagaraFunctionInputActionExpander)
				]
			]
		];

	LibraryOnlyToggle->SetActionMenu(GraphActionMenu.ToSharedRef());

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

#undef LOCTEXT_NAMESPACE
