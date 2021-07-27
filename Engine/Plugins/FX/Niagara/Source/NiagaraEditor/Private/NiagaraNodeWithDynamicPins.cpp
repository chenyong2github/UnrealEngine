// Copyright Epic Games, Inc. All Rights Reserved.


#include "NiagaraNodeWithDynamicPins.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraGraph.h"
#include "Framework/Commands/UIAction.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "Framework/Application/SlateApplication.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraConstants.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeWithDynamicPins"

const FName UNiagaraNodeWithDynamicPins::AddPinSubCategory("DynamicAddPin");

void UNiagaraNodeWithDynamicPins::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	// Check if an add pin was connected and convert it to a typed connection.
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	if (Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && Pin->PinType.PinSubCategory == AddPinSubCategory && Pin->LinkedTo.Num() > 0)
	{
		FNiagaraTypeDefinition LinkedPinType = Schema->PinToTypeDefinition(Pin->LinkedTo[0]);
		Pin->PinType = Schema->TypeDefinitionToPinType(LinkedPinType);

		FName NewPinName;
		FText NewPinFriendlyName;
		FNiagaraParameterHandle LinkedPinHandle(Pin->LinkedTo[0]->PinName);
		FNiagaraNamespaceMetadata LinkedPinNamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(LinkedPinHandle.GetHandleParts());
		if (LinkedPinNamespaceMetadata.IsValid())
		{
			// If the linked pin had valid namespace metadata then it's a parameter pin and we only want the name portion of the parameter.
			NewPinName = LinkedPinHandle.GetHandleParts().Last();
		}
		else 
		{
			NewPinName = Pin->LinkedTo[0]->PinName;
			NewPinFriendlyName = Pin->LinkedTo[0]->PinFriendlyName;
		}
		
		Pin->PinName = NewPinName;
		Pin->PinFriendlyName = NewPinFriendlyName;

		CreateAddPin(Pin->Direction);
		OnNewTypedPinAdded(Pin);
		MarkNodeRequiresSynchronization(__FUNCTION__, true);
		//GetGraph()->NotifyGraphChanged();
	}
}

UEdGraphPin* GetAddPin(TArray<UEdGraphPin*> Pins, EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == Direction &&
			Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && 
			Pin->PinType.PinSubCategory == UNiagaraNodeWithDynamicPins::AddPinSubCategory)
		{
			return Pin;
		}
	}
	return nullptr;
}

bool UNiagaraNodeWithDynamicPins::AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType) const
{
	return InType.GetScriptStruct() != nullptr
		&& InType != FNiagaraTypeDefinition::GetGenericNumericDef()
		&& !InType.IsInternalType();
}

UEdGraphPin* UNiagaraNodeWithDynamicPins::RequestNewTypedPin(EEdGraphPinDirection Direction, const FNiagaraTypeDefinition& Type)
{
	TStringBuilder<128> DefaultName;
	if (Direction == EGPD_Input)
	{
		FPinCollectorArray InPins;
		GetInputPins(InPins);
		DefaultName << TEXT("Input ") << InPins.Num();
	}
	else
	{
		FPinCollectorArray OutPins;
		GetOutputPins(OutPins);
		DefaultName << TEXT("Output ") << OutPins.Num();
	}
	return RequestNewTypedPin(Direction, Type, DefaultName.ToString());
}

UEdGraphPin* UNiagaraNodeWithDynamicPins::RequestNewTypedPin(EEdGraphPinDirection Direction, const FNiagaraTypeDefinition& Type, const FName InName)
{
	FScopedTransaction Transaction(LOCTEXT("NewPinAdded", "Added new pin"));
	
	Modify();
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	UEdGraphPin* AddPin = GetAddPin(GetAllPins(), Direction);
	checkf(AddPin != nullptr, TEXT("Add pin is missing"));
	AddPin->Modify();
	AddPin->PinType = Schema->TypeDefinitionToPinType(Type);
	AddPin->PinName = InName;

	CreateAddPin(Direction);
	// we pass the pointer in as reference in case we want to reallocate so the overriding node has a chance to restore the pointer
	OnNewTypedPinAdded(AddPin);

	checkf(AddPin != nullptr && AddPin->IsPendingKill() == false, 
		TEXT("The pin was invalidated. Most likely due to reallocation in OnNewTypedPinAdded and failure to restore the pin pointer"));
	
	MarkNodeRequiresSynchronization(__FUNCTION__, true);

	return AddPin;
}

void UNiagaraNodeWithDynamicPins::CreateAddPin(EEdGraphPinDirection Direction)
{
	if (!AllowDynamicPins())
	{
		return;
	}
	CreatePin(Direction, FEdGraphPinType(UEdGraphSchema_Niagara::PinCategoryMisc, AddPinSubCategory, nullptr, EPinContainerType::None, false, FEdGraphTerminalType()), TEXT("Add"));
}

bool UNiagaraNodeWithDynamicPins::IsAddPin(const UEdGraphPin* Pin) const
{
	return Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc && 
		Pin->PinType.PinSubCategory == UNiagaraNodeWithDynamicPins::AddPinSubCategory;
}

bool UNiagaraNodeWithDynamicPins::CanRenamePin(const UEdGraphPin* Pin) const
{
	return IsAddPin(Pin) == false;
}

bool UNiagaraNodeWithDynamicPins::CanRemovePin(const UEdGraphPin* Pin) const
{
	return IsAddPin(Pin) == false;
}

bool UNiagaraNodeWithDynamicPins::CanMovePin(const UEdGraphPin* Pin, int32 DirectionToMove) const
{
	return IsAddPin(Pin) == false;
}

void UNiagaraNodeWithDynamicPins::MoveDynamicPin(UEdGraphPin* Pin, int32 DirectionToMove)
{
	FPinCollectorArray SameDirectionPins;
	if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
	{
		GetInputPins(SameDirectionPins);
	}
	else
	{
		GetOutputPins(SameDirectionPins);
	}

	for (int32 i = 0; i < SameDirectionPins.Num(); i++)
	{
		if (SameDirectionPins[i] == Pin)
		{
			if (i + DirectionToMove >= 0 && i + DirectionToMove < SameDirectionPins.Num())
			{
				Modify();
				UEdGraphPin* PinOld = SameDirectionPins[i + DirectionToMove];
				if (PinOld)
					PinOld->Modify();
				Pin->Modify();

				int32 RealPinIdx = INDEX_NONE;
				int32 SwapRealPinIdx = INDEX_NONE;
				Pins.Find(Pin, RealPinIdx);
				Pins.Find(PinOld, SwapRealPinIdx);
				
				Pins[SwapRealPinIdx] = Pin;
				Pins[RealPinIdx] = PinOld;
				//GetGraph()->NotifyGraphChanged();

				MarkNodeRequiresSynchronization(__FUNCTION__, true);
				break;
			}
		}
	}
}

bool UNiagaraNodeWithDynamicPins::IsValidPinToCompile(UEdGraphPin* Pin) const
{
	return !IsAddPin(Pin) && Super::IsValidPinToCompile(Pin);
}

void UNiagaraNodeWithDynamicPins::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);
	if (Context->Pin != nullptr)
	{
		FToolMenuSection& Section = Menu->AddSection("EditPin", LOCTEXT("EditPinMenuHeader", "Edit Pin"));
		if (CanRenamePinFromContextMenu(Context->Pin))
		{
			UEdGraphPin* Pin = const_cast<UEdGraphPin*>(Context->Pin);
			TSharedRef<SWidget> RenameWidget =
				SNew(SBox)
				.WidthOverride(100)
				.Padding(FMargin(5, 0, 0, 0))
				[
					SNew(SEditableTextBox)
					.Text_UObject(this, &UNiagaraNodeWithDynamicPins::GetPinNameText, Pin)
					.OnTextCommitted_UObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::PinNameTextCommitted, Pin)
					.OnVerifyTextChanged_UObject(this, &UNiagaraNodeWithDynamicPins::VerifyEditablePinName, Context->Pin)
				];
			Section.AddEntry(FToolMenuEntry::InitWidget("RenameWidget", RenameWidget, LOCTEXT("NameMenuItem", "Name")));
		}
		else if (CanRenamePin(Context->Pin))
		{
			Section.AddMenuEntry(
				NAME_None,
				LOCTEXT("RenameDynamicPin", "Rename pin"),
				LOCTEXT("RenameDynamicPinToolTip", "Rename this pin."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::RenameDynamicPinFromMenu, const_cast<UEdGraphPin*>(Context->Pin))));
		}

		if (CanRemovePin(Context->Pin))
		{
			Section.AddMenuEntry(
				"RemoveDynamicPin",
				LOCTEXT("RemoveDynamicPin", "Remove pin"),
				LOCTEXT("RemoveDynamicPinToolTip", "Remove this pin and any connections."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::RemoveDynamicPinFromMenu, const_cast<UEdGraphPin*>(Context->Pin))));
		}
		
		FPinCollectorArray SameDirectionPins;
		if (Context->Pin->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			GetInputPins(SameDirectionPins);
		}
		else
		{
			GetOutputPins(SameDirectionPins);
		}
		int32 PinIdx = INDEX_NONE;
		SameDirectionPins.Find(const_cast<UEdGraphPin*>(Context->Pin), PinIdx);

		if (PinIdx != 0 && CanMovePin(Context->Pin, -1))
		{
			Section.AddMenuEntry(
				"MoveDynamicPinUp",
				LOCTEXT("MoveDynamicPinUp", "Move pin up"),
				LOCTEXT("MoveDynamicPinToolTipUp", "Move this pin and any connections one slot up."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::MoveDynamicPinFromMenu, const_cast<UEdGraphPin*>(Context->Pin), -1)));
		}
		if (PinIdx >= 0 && CanMovePin(Context->Pin, 1) && PinIdx < SameDirectionPins.Num() - 1)
		{
			Section.AddMenuEntry(
				"MoveDynamicPinDown",
				LOCTEXT("MoveDynamicPinDown", "Move pin down"),
				LOCTEXT("MoveDynamicPinToolTipDown", "Move this pin and any connections one slot down."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateUObject(const_cast<UNiagaraNodeWithDynamicPins*>(this), &UNiagaraNodeWithDynamicPins::MoveDynamicPinFromMenu, const_cast<UEdGraphPin*>(Context->Pin), 1)));
		}
		
	}
}

void UNiagaraNodeWithDynamicPins::CollectAddPinActions(FGraphActionListBuilderBase& OutActions, bool& bOutCreateRemainingActions, UEdGraphPin* Pin)
{
	TArray<FNiagaraTypeDefinition> Types(FNiagaraTypeRegistry::GetRegisteredTypes());
	Types.Sort([](const FNiagaraTypeDefinition& A, const FNiagaraTypeDefinition& B) { return (A.GetNameText().ToLower().ToString() < B.GetNameText().ToLower().ToString()); });

	for (const FNiagaraTypeDefinition& RegisteredType : Types)
	{
		bool bAllowType = false;
		bAllowType = AllowNiagaraTypeForAddPin(RegisteredType);

		if (bAllowType)
		{
			FNiagaraVariable Var(RegisteredType, FName(*RegisteredType.GetName()));
			FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);

			FText Category = FNiagaraEditorUtilities::GetVariableTypeCategory(Var);
			const FText DisplayName = RegisteredType.GetNameText();
			const FText Tooltip = FText::Format(LOCTEXT("AddButtonTypeEntryToolTipFormat", "Add a new {0} pin"), RegisteredType.GetNameText());
			TSharedPtr<FNiagaraMenuAction> Action(new FNiagaraMenuAction(
				Category, DisplayName, Tooltip, 0, FText::GetEmpty(),
				FNiagaraMenuAction::FOnExecuteStackAction::CreateUObject(this, &UNiagaraNodeWithDynamicPins::AddParameter, Var, (const UEdGraphPin*)Pin)));

			OutActions.AddAction(Action);
		}
	}

	bOutCreateRemainingActions = false;
}

void UNiagaraNodeWithDynamicPins::AddParameter(FNiagaraVariable Parameter, const UEdGraphPin* AddPin)
{
	if (this->IsA<UNiagaraNodeParameterMapBase>())
	{
		// Parameter map type nodes create new parameters when adding pins.
		FScopedTransaction AddNewPinTransaction(LOCTEXT("AddNewPinTransaction", "Add pin to node"));
		
		UNiagaraGraph* Graph = GetNiagaraGraph();
		checkf(Graph != nullptr, TEXT("Failed to get niagara graph when adding pin!"));

		// Resolve the unique parameter name before adding to the graph if the current parameter name is not reserved.
		if (FNiagaraConstants::FindEngineConstant(Parameter) == nullptr)
		{
			if(Graph->GetAllMetaData().Contains(Parameter) == false)
			{
				Parameter.SetName(Graph->MakeUniqueParameterName(Parameter.GetName()));
			}
		}

		Graph->Modify();
		Graph->AddParameter(Parameter);

		Modify();
		UEdGraphPin* Pin = this->RequestNewTypedPin(AddPin->Direction, Parameter.GetType(), Parameter.GetName());
	}
	else
	{
		RequestNewTypedPin(AddPin->Direction, Parameter.GetType(), Parameter.GetName());
	}
}

void UNiagaraNodeWithDynamicPins::AddParameter(const UNiagaraScriptVariable* ScriptVar, const UEdGraphPin* AddPin)
{
	const FNiagaraVariable& Parameter = ScriptVar->Variable;
	if (this->IsA<UNiagaraNodeParameterMapBase>())
	{
		// Parameter map type nodes create new parameters when adding pins.
		FScopedTransaction AddNewPinTransaction(LOCTEXT("AddNewPinTransaction", "Add pin to node"));

		UNiagaraGraph* Graph = GetNiagaraGraph();
		checkf(Graph != nullptr, TEXT("Failed to get niagara graph when adding pin!"));

		Graph->Modify();
		Graph->AddParameter(ScriptVar);

		Modify();
		UEdGraphPin* Pin = this->RequestNewTypedPin(AddPin->Direction, Parameter.GetType(), Parameter.GetName());
	}
	else
	{
		RequestNewTypedPin(AddPin->Direction, Parameter.GetType(), Parameter.GetName());
	}
}

void UNiagaraNodeWithDynamicPins::RemoveDynamicPin(UEdGraphPin* Pin)
{
	RemovePin(Pin);
	MarkNodeRequiresSynchronization(__FUNCTION__, true);
}

FText UNiagaraNodeWithDynamicPins::GetPinNameText(UEdGraphPin* Pin) const
{
	return FText::FromName(Pin->PinName);
}


void UNiagaraNodeWithDynamicPins::PinNameTextCommitted(const FText& Text, ETextCommit::Type CommitType, UEdGraphPin* Pin)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FScopedTransaction RemovePinTransaction(LOCTEXT("RenamePinTransaction", "Rename pin"));
		Modify();
		FString PinOldName = Pin->PinName.ToString();
		Pin->PinName = *Text.ToString();
		OnPinRenamed(Pin, PinOldName);
		MarkNodeRequiresSynchronization(__FUNCTION__, true);
	}
}

void UNiagaraNodeWithDynamicPins::RenameDynamicPinFromMenu(UEdGraphPin* Pin)
{
	SetIsPinRenamePending(Pin, true);
}

void UNiagaraNodeWithDynamicPins::RemoveDynamicPinFromMenu(UEdGraphPin* Pin)
{
	FScopedTransaction RemovePinTransaction(LOCTEXT("RemovePinTransaction", "Remove pin"));
	RemoveDynamicPin(Pin);
}

void UNiagaraNodeWithDynamicPins::MoveDynamicPinFromMenu(UEdGraphPin* Pin, int32 DirectionToMove)
{
	FScopedTransaction MovePinTransaction(LOCTEXT("MovePinTransaction", "Moved pin"));
	MoveDynamicPin(Pin, DirectionToMove);
}

#undef LOCTEXT_NAMESPACE
