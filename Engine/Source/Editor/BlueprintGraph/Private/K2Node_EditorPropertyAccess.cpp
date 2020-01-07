// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_EditorPropertyAccess.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_EditorPropertyAccess"

namespace EditorPropertyAccessHelper
{

const FName ObjectPinName = "Object";
const FName PropertyNamePinName = "PropertyName";
const FName PropertyValuePinName = "PropertyValue";

}

void UK2Node_EditorPropertyAccessBase::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	// Add execution pins
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Add Object pin
	UEdGraphPin* ObjectPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), EditorPropertyAccessHelper::ObjectPinName);
	SetPinToolTip(*ObjectPin, LOCTEXT("ObjectPinDescription", "The object you want to access a property value from"));

	// Add Property Name pin
	UEdGraphPin* PropertyNamePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, EditorPropertyAccessHelper::PropertyNamePinName);
	SetPinToolTip(*PropertyNamePin, LOCTEXT("PropertyNamePinDescription", "The name of the property to access from the object"));

	// Add Property Value pin
	AllocatePropertyValuePin();

	// Add Result Pin
	UEdGraphPin* ResultPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, UEdGraphSchema_K2::PN_ReturnValue);
	ResultPin->PinFriendlyName = LOCTEXT("ResultPinFriendlyName", "Success?");
	SetPinToolTip(*ResultPin, LOCTEXT("ResultPinDescription", "Whether the property value was found"));
}

void UK2Node_EditorPropertyAccessBase::SetPinToolTip(UEdGraphPin& MutatablePin, const FText& PinDescription) const
{
	MutatablePin.PinToolTip = UEdGraphSchema_K2::TypeToText(MutatablePin.PinType).ToString();

	const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
	if (K2Schema)
	{
		MutatablePin.PinToolTip += TEXT(" ");
		MutatablePin.PinToolTip += K2Schema->GetPinDisplayName(&MutatablePin).ToString();
	}

	MutatablePin.PinToolTip += FString(TEXT("\n")) + PinDescription.ToString();
}

void UK2Node_EditorPropertyAccessBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that 
	// actions might have to be updated (or deleted) if their object-key is  
	// mutated (or removed)... here we use the node's class (so if the node 
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();
	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first   
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the 
	// registrar would only accept actions corresponding to that asset)
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_EditorPropertyAccessBase::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Utilities);
}

bool UK2Node_EditorPropertyAccessBase::CanPasteHere(const UEdGraph* TargetGraph) const
{
	bool bCanPaste = Super::CanPasteHere(TargetGraph);
	if (bCanPaste)
	{
		bCanPaste &= FBlueprintEditorUtils::IsEditorUtilityBlueprint(FBlueprintEditorUtils::FindBlueprintForGraphChecked(TargetGraph));
	}
	return bCanPaste;
}

bool UK2Node_EditorPropertyAccessBase::IsActionFilteredOut(const FBlueprintActionFilter& Filter)
{
	bool bIsFilteredOut = Super::IsActionFilteredOut(Filter);
	if (!bIsFilteredOut)
	{
		for (UEdGraph* TargetGraph : Filter.Context.Graphs)
		{
			bIsFilteredOut |= !CanPasteHere(TargetGraph);
		}
	}
	return bIsFilteredOut;
}

bool UK2Node_EditorPropertyAccessBase::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin->PinName == EditorPropertyAccessHelper::PropertyValuePinName && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			OutReason = TEXT("Cannot connect the property value to an execution pin");
			return true;
		}
	}

	return false;
}

UEdGraphPin* UK2Node_EditorPropertyAccessBase::GetThenPin()const
{
	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_Then);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

UEdGraphPin* UK2Node_EditorPropertyAccessBase::GetObjectPin() const
{
	UEdGraphPin* Pin = FindPinChecked(EditorPropertyAccessHelper::ObjectPinName);
	check(Pin->Direction == EGPD_Input);
	return Pin;
}

UEdGraphPin* UK2Node_EditorPropertyAccessBase::GetPropertyNamePin() const
{
	UEdGraphPin* Pin = FindPinChecked(EditorPropertyAccessHelper::PropertyNamePinName);
	check(Pin->Direction == EGPD_Input);
	return Pin;
}

UEdGraphPin* UK2Node_EditorPropertyAccessBase::GetResultPin() const
{
	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

void UK2Node_EditorPropertyAccessBase::RefreshPropertyValuePin()
{
	UEdGraphPin* PropertyValuePin = GetPropertyValuePin();

	FEdGraphPinType NewPinType;
	if (PropertyValuePin->LinkedTo.Num() > 0)
	{
		NewPinType = PropertyValuePin->LinkedTo[0]->PinType;
	}
	else
	{
		NewPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
	}

	if (PropertyValuePin->PinType != NewPinType)
	{
		PropertyValuePin->PinType = NewPinType;

		GetGraph()->NotifyGraphChanged();

		UBlueprint* Blueprint = GetBlueprint();
		if (!Blueprint->bBeingCompiled)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			Blueprint->BroadcastChanged();
		}
	}
}

FSlateIcon UK2Node_EditorPropertyAccessBase::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

void UK2Node_EditorPropertyAccessBase::PostReconstructNode()
{
	Super::PostReconstructNode();

	RefreshPropertyValuePin();
}

void UK2Node_EditorPropertyAccessBase::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	const UEdGraphPin* ObjectPin = GetObjectPin();
	const UEdGraphPin* PropertyNamePin = GetPropertyNamePin();
	const UEdGraphPin* PropertyValuePin = GetPropertyValuePin();
	if (!ObjectPin || !PropertyNamePin || !PropertyValuePin)
	{
		MessageLog.Error(*LOCTEXT("MissingPins", "Missing pins in @@").ToString(), this);
		return;
	}

	if (ObjectPin->LinkedTo.Num() == 0 && !ObjectPin->DefaultObject)
	{
		MessageLog.Error(*LOCTEXT("UnsetObject", "No object set on @@").ToString(), this);
	}

	if (PropertyNamePin->LinkedTo.Num() == 0 && FName(*PropertyNamePin->DefaultValue).IsNone())
	{
		MessageLog.Error(*LOCTEXT("UnsetPropertyName", "No property name set on @@").ToString(), this);
	}
}

void UK2Node_EditorPropertyAccessBase::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);

	if (Pin->PinName == EditorPropertyAccessHelper::PropertyValuePinName)
	{
		RefreshPropertyValuePin();
	}
}

void UK2Node_EditorPropertyAccessBase::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const FName FunctionName = GetUnderlyingFunctionName();

	// Add a CallFunction node for the underlying function
	UK2Node_CallFunction* CallFunctionNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallFunctionNode->FunctionReference.SetExternalMember(FunctionName, UKismetSystemLibrary::StaticClass());
	CallFunctionNode->AllocateDefaultPins();

	// Re-wire the execution pins
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallFunctionNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *CallFunctionNode->GetThenPin());

	// Re-wire the Object pin to the function input
	{
		UEdGraphPin* FunctionObjectPin = CallFunctionNode->FindPinChecked(TEXT("Object"));
		CompilerContext.MovePinLinksToIntermediate(*GetObjectPin(), *FunctionObjectPin);
	}

	// Re-wire the PropertyName pin to the function input
	{
		UEdGraphPin* FunctionPropertyNamePin = CallFunctionNode->FindPinChecked(TEXT("PropertyName"));
		CompilerContext.MovePinLinksToIntermediate(*GetPropertyNamePin(), *FunctionPropertyNamePin);
	}

	// Re-wire the ValuePtr pin to the function output, and copy its type over
	{
		UEdGraphPin* PropertyValuePin = GetPropertyValuePin();
		UEdGraphPin* FunctionValuePtrPin = CallFunctionNode->FindPinChecked(TEXT("ValuePtr"));
		CompilerContext.MovePinLinksToIntermediate(*PropertyValuePin, *FunctionValuePtrPin);
		FunctionValuePtrPin->PinType = PropertyValuePin->PinType;
	}

	// Re-wire the result pin
	{
		UEdGraphPin* FunctionReturnPin = CallFunctionNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
		CompilerContext.MovePinLinksToIntermediate(*GetResultPin(), *FunctionReturnPin);
	}

	// Disconnect this node
	BreakAllNodeLinks();
}

void UK2Node_GetEditorProperty::AllocatePropertyValuePin()
{
	UEdGraphPin* PropertyValuePin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, EditorPropertyAccessHelper::PropertyValuePinName);
	SetPinToolTip(*PropertyValuePin, LOCTEXT("GetEditorProperty_PropertyValueDescription", "The returned property value, if found"));
}

UEdGraphPin* UK2Node_GetEditorProperty::GetPropertyValuePin() const
{
	UEdGraphPin* Pin = FindPinChecked(EditorPropertyAccessHelper::PropertyValuePinName);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

FText UK2Node_GetEditorProperty::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("GetEditorProperty_NodeTitle", "Get Editor Property");
}

FText UK2Node_GetEditorProperty::GetTooltipText() const
{
	return LOCTEXT("GetEditorProperty_NodeTooltip", "Attempts to retrieve the value of a named property from the given object");
}

FName UK2Node_GetEditorProperty::GetUnderlyingFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, GetEditorProperty);
}

void UK2Node_SetEditorProperty::AllocatePropertyValuePin()
{
	UEdGraphPin* PropertyValuePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, EditorPropertyAccessHelper::PropertyValuePinName);
	SetPinToolTip(*PropertyValuePin, LOCTEXT("SetEditorProperty_PropertyValueDescription", "The property value to set"));
}

UEdGraphPin* UK2Node_SetEditorProperty::GetPropertyValuePin() const
{
	UEdGraphPin* Pin = FindPinChecked(EditorPropertyAccessHelper::PropertyValuePinName);
	check(Pin->Direction == EGPD_Input);
	return Pin;
}

FText UK2Node_SetEditorProperty::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SetEditorProperty_NodeTitle", "Set Editor Property");
}

FText UK2Node_SetEditorProperty::GetTooltipText() const
{
	return LOCTEXT("SetEditorProperty_NodeTooltip", "Attempts to set the value of a named property on the given object");
}

FName UK2Node_SetEditorProperty::GetUnderlyingFunctionName() const
{
	return GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, SetEditorProperty);
}

#undef LOCTEXT_NAMESPACE
