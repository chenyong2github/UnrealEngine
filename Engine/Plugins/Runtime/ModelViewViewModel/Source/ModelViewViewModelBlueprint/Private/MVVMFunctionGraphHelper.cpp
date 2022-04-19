// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMFunctionGraphHelper.h"

#include "BlueprintTypePromotion.h"
#include "EngineLogs.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "View/MVVMView.h"
#include "MVVMViewModelBase.h"


namespace UE::MVVM::FunctionGraphHelper
{

UEdGraph* CreateFunctionGraph(FKismetCompilerContext& InContext, FStringView InFunctionName, EFunctionFlags ExtraFunctionFlag, const FStringView Category, bool bIsEditable)
{
	FName UniqueFunctionName = FBlueprintEditorUtils::FindUniqueKismetName(InContext.Blueprint, InFunctionName.GetData());
	UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(InContext.Blueprint, UniqueFunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(FunctionGraph->GetSchema());
	Schema->MarkFunctionEntryAsEditable(FunctionGraph, bIsEditable);
	Schema->CreateDefaultNodesForGraph(*FunctionGraph);

	// function entry node
	FGraphNodeCreator<UK2Node_FunctionEntry> FunctionEntryCreator(*FunctionGraph);
	UK2Node_FunctionEntry* FunctionEntry = FunctionEntryCreator.CreateNode();
	FunctionEntry->FunctionReference.SetSelfMember(FunctionGraph->GetFName());
	FunctionEntry->AddExtraFlags(ExtraFunctionFlag);
	FunctionEntry->bIsEditable = bIsEditable;
	FunctionEntry->MetaData.Category = FText::FromStringView(Category);
	FunctionEntryCreator.Finalize();

	return FunctionGraph;
}


UEdGraph* CreateIntermediateFunctionGraph(FKismetCompilerContext& InContext, FStringView InFunctionName, EFunctionFlags ExtraFunctionFlag, const FStringView Category, bool bIsEditable)
{
	UEdGraph* FunctionGraph = InContext.SpawnIntermediateFunctionGraph(InFunctionName.GetData());
	const UEdGraphSchema* Schema = FunctionGraph->GetSchema();
	Schema->CreateDefaultNodesForGraph(*FunctionGraph);

	// function entry node
	UK2Node_FunctionEntry* FunctionEntry = CastChecked<UK2Node_FunctionEntry>(FunctionGraph->Nodes[0]);
	check(FunctionEntry);
	{
		FunctionEntry->AddExtraFlags(ExtraFunctionFlag);
		FunctionEntry->bIsEditable = bIsEditable;
		FunctionEntry->MetaData.Category = FText::FromStringView(Category);

		// Add function input
		FunctionEntry->AllocateDefaultPins();
	}
	return FunctionGraph;
}


bool AddFunctionArgument(UEdGraph* InFunctionGraph, TSubclassOf<UObject> InArgument, FName InArgumentName)
{
	for (UEdGraphNode* Node : InFunctionGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
		{
			TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
			PinInfo->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinInfo->PinType.PinSubCategoryObject = InArgument.Get();
			PinInfo->PinName = InArgumentName;
			PinInfo->DesiredPinDirection = EGPD_Output;
			FunctionEntry->UserDefinedPins.Add(PinInfo);

			FunctionEntry->ReconstructNode();
			return true;
		}
	}
	return false;
}


bool AddFunctionArgument(UEdGraph* InFunctionGraph, FProperty* InArgument, FName InArgumentName)
{
	for (UEdGraphNode* Node : InFunctionGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

			TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
			K2Schema->ConvertPropertyToPinType(InArgument, PinInfo->PinType);
			PinInfo->PinName = InArgumentName;
			PinInfo->DesiredPinDirection = EGPD_Output;
			FunctionEntry->UserDefinedPins.Add(PinInfo);

			FunctionEntry->ReconstructNode();
			return true;
		}
	}
	return false;
}


bool GenerateViewModelSetter(FKismetCompilerContext& InContext, UEdGraph* InFunctionGraph, FName InViewModelName)
{
	check(InFunctionGraph);

	UK2Node_FunctionEntry* FunctionEntry = nullptr;
	for (UEdGraphNode* Node : InFunctionGraph->Nodes)
	{
		FunctionEntry = Cast<UK2Node_FunctionEntry>(Node);
		if (FunctionEntry)
		{
			break;
		}
	}

	if (FunctionEntry == nullptr)
	{
		return false;
	}

	const UEdGraphSchema* Schema = InFunctionGraph->GetSchema();
	if (Schema == nullptr)
	{
		return false;
	}

	bool bResult = true;
	UK2Node_CallFunction* CallSetViewModelNode = InContext.SpawnIntermediateNode<UK2Node_CallFunction>(FunctionEntry, InFunctionGraph);
	{
		CallSetViewModelNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UMVVMView, SetViewModel), UMVVMView::StaticClass());
		CallSetViewModelNode->AllocateDefaultPins();

		UEdGraphPin* NamePin = CallSetViewModelNode->FindPin(TEXT("ViewModelName"), EGPD_Input);
		bResult = bResult && NamePin != nullptr;
		if (ensure(NamePin))
		{
			bool bMarkAsModified = false;
			Schema->TrySetDefaultValue(*NamePin, InViewModelName.ToString(), bMarkAsModified);
		}
	}
	UK2Node_CallFunction* CallGetExtensionlNode = InContext.SpawnIntermediateNode<UK2Node_CallFunction>(FunctionEntry, InFunctionGraph);
	{
		CallGetExtensionlNode->FunctionReference.SetSelfMember(TEXT("GetExtension"));
		CallGetExtensionlNode->AllocateDefaultPins();

		UEdGraphPin* ExtensionTypePin = CallGetExtensionlNode->FindPin(TEXT("ExtensionType"), EGPD_Input);
		bResult = bResult && ExtensionTypePin != nullptr;
		if (ensure(ExtensionTypePin))
		{
			bool bMarkAsModified = false;
			Schema->TrySetDefaultObject(*ExtensionTypePin, UMVVMView::StaticClass(), bMarkAsModified);
		}
	}
	UK2Node_DynamicCast* DynCastNode = InContext.SpawnIntermediateNode<UK2Node_DynamicCast>(FunctionEntry, InFunctionGraph);
	{
		DynCastNode->TargetType = UMVVMView::StaticClass();
		DynCastNode->SetPurity(true);
		DynCastNode->AllocateDefaultPins();
	}
	// Entry -> SetViewModel
	{
		UEdGraphPin* ThenPin = FunctionEntry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* EntryViewModelPin = FunctionEntry->FindPin(TEXT("ViewModel"), EGPD_Output);
		UEdGraphPin* ExecPin = CallSetViewModelNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		UEdGraphPin* SetViewModelPin = CallSetViewModelNode->FindPin(TEXT("ViewModel"), EGPD_Input);
		if (ensure(ThenPin && ExecPin))
		{
			ensure(Schema->TryCreateConnection(ThenPin, ExecPin));
		}
		if (ensure(EntryViewModelPin && SetViewModelPin))
		{
			ensure(Schema->TryCreateConnection(EntryViewModelPin, SetViewModelPin));
		}
		bResult = bResult && ThenPin && ExecPin && EntryViewModelPin && SetViewModelPin;
	}
	// GetExtension -> Cast
	{
		UEdGraphPin* ResultPin = CallGetExtensionlNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
		UEdGraphPin* ObjectPin = DynCastNode->GetCastSourcePin();
		if (ensure(ResultPin && ObjectPin))
		{
			ensure(Schema->TryCreateConnection(ResultPin, ObjectPin));
		}
		bResult = bResult && ResultPin && ObjectPin;
	}
	// Cast -> SetViewModel
	{
		UEdGraphPin* ResultPin = DynCastNode->GetCastResultPin();
		UEdGraphPin* ObjectPin = CallSetViewModelNode->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
		if (ensure(ResultPin && ObjectPin))
		{
			ensure(Schema->TryCreateConnection(ResultPin, ObjectPin));
		}
		bResult = bResult && ResultPin && ObjectPin;
	}

	return bResult;
}


bool GenerateViewModelFieldNotifySetter(FKismetCompilerContext& InContext, UEdGraph* InFunctionGraph, FProperty* InProperty, FName InInputPinName)
{
	check(InFunctionGraph);

	UK2Node_FunctionEntry* FunctionEntry = nullptr;
	for (UEdGraphNode* Node : InFunctionGraph->Nodes)
	{
		FunctionEntry = Cast<UK2Node_FunctionEntry>(Node);
		if (FunctionEntry)
		{
			break;
		}
	}

	if (FunctionEntry == nullptr)
	{
		return false;
	}

	const UEdGraphSchema* Schema = InFunctionGraph->GetSchema();
	if (Schema == nullptr)
	{
		return false;
	}

	UK2Node_VariableSet* SetVariableNode = InContext.SpawnIntermediateNode<UK2Node_VariableSet>(FunctionEntry, InFunctionGraph);
	{
		//SetVariableNode->SetFromProperty(InProperty, true, nullptr);
		SetVariableNode->VariableReference.SetSelfMember(InProperty->GetFName());
		SetVariableNode->AllocateDefaultPins();
	}

	UK2Node_CallFunction* CallBroadcast = InContext.SpawnIntermediateNode<UK2Node_CallFunction>(FunctionEntry, InFunctionGraph);
	{
		CallBroadcast->FunctionReference.SetExternalMember("K2_BroadcastFieldValueChanged", UMVVMViewModelBase::StaticClass());
		CallBroadcast->AllocateDefaultPins();

		UEdGraphPin* NamePin = CallBroadcast->FindPin(TEXT("FieldId"), EGPD_Input);
		ensure(NamePin);
		if (!NamePin)
		{
			return false;
		}

		FFieldNotificationId NewPropertyNotifyId;
		NewPropertyNotifyId.FieldName = InProperty->GetFName();

		FString ValueString;
		FFieldNotificationId::StaticStruct()->ExportText(ValueString, &NewPropertyNotifyId, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);

		bool bMarkAsModified = false;
		Schema->TrySetDefaultValue(*NamePin, ValueString, bMarkAsModified);
	}

	UK2Node_VariableGet* GetVariableNode = InContext.SpawnIntermediateNode<UK2Node_VariableGet>(FunctionEntry, InFunctionGraph);
	{
		//GetVariableNode->SetFromProperty(InProperty, true, nullptr);
		GetVariableNode->VariableReference.SetSelfMember(InProperty->GetFName());
		GetVariableNode->AllocateDefaultPins();
	}

	UK2Node_IfThenElse* BranchNode = InContext.SpawnIntermediateNode<UK2Node_IfThenElse>(FunctionEntry, InFunctionGraph);
	{
		BranchNode->AllocateDefaultPins();
	}

	UFunction* CompareFunction = nullptr;
	{
		TArray<UEdGraphPin*> PromotableOperatorPinsToConsider;
		UEdGraphPin* EntryValuePin = FunctionEntry->FindPin(InInputPinName, EGPD_Output);
		if (ensure(EntryValuePin))
		{
			PromotableOperatorPinsToConsider.Add(EntryValuePin);
		}
		UEdGraphPin* GetVariableNode_Property = GetVariableNode->FindPin(InProperty->GetFName(), EGPD_Output);
		if (ensure(GetVariableNode_Property))
		{
			PromotableOperatorPinsToConsider.Add(GetVariableNode_Property);
		}
		UEdGraphPin* BranchCondition = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
		if (ensure(BranchCondition))
		{
			PromotableOperatorPinsToConsider.Add(BranchCondition);
		}

		CompareFunction = FTypePromotion::FindBestMatchingFunc(TEXT("NotEqual"), PromotableOperatorPinsToConsider);
	}

	bool bResult = true;

	bool bDoBranchTest = CompareFunction != nullptr;
	if (bDoBranchTest)
	{
		UK2Node_PromotableOperator* CompareOperator = InContext.SpawnIntermediateNode<UK2Node_PromotableOperator>(FunctionEntry, InFunctionGraph);
		{
			CompareOperator->SetFromFunction(CompareFunction);
			CompareOperator->AllocateDefaultPins();
		}

		// Entry -> Branch
		{
			UEdGraphPin* ThenPin = FunctionEntry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* ExecPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (ensure(ThenPin && ExecPin))
			{
				ensure(Schema->TryCreateConnection(ThenPin, ExecPin));
			}
			bResult = bResult && ThenPin && ExecPin;
		}

		// Entry -> Compare, Get -> Compare, Compare -> Branch
		{
			UEdGraphPin* EntryValuePin = FunctionEntry->FindPin(InInputPinName, EGPD_Output);
			UEdGraphPin* TopInputPin = CompareOperator->FindPin(TEXT("A"), EGPD_Input);
			if (ensure(EntryValuePin && TopInputPin))
			{
				ensure(Schema->TryCreateConnection(EntryValuePin, TopInputPin));
			}
			bResult = bResult && EntryValuePin && TopInputPin;

			UEdGraphPin* GetPin = GetVariableNode->FindPin(InProperty->GetFName(), EGPD_Output);
			UEdGraphPin* BottomInputPin = CompareOperator->FindPin(TEXT("B"), EGPD_Input);
			if (ensure(GetPin && BottomInputPin))
			{
				ensure(Schema->TryCreateConnection(GetPin, BottomInputPin));
			}
			bResult = bResult && GetPin && BottomInputPin;

			UEdGraphPin* OutputPin = CompareOperator->GetOutputPin();
			UEdGraphPin* BranchCondition = BranchNode->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input);
			if (ensure(BranchCondition && OutputPin))
			{
				ensure(Schema->TryCreateConnection(OutputPin, BranchCondition));
			}
			bResult = bResult && BranchCondition && OutputPin;
		}

		// Branch -> Set
		{
			UEdGraphPin* ThenPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* ExecPin = SetVariableNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (ensure(ThenPin && ExecPin))
			{
				ensure(Schema->TryCreateConnection(ThenPin, ExecPin));
			}
			bResult = bResult && ThenPin && ExecPin;
		}
	}
	else
	{
		// Entry -> Set
		{
			UEdGraphPin* ThenPin = FunctionEntry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* ExecPin = SetVariableNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (ensure(ThenPin && ExecPin))
			{
				ensure(Schema->TryCreateConnection(ThenPin, ExecPin));
			}
			bResult = bResult && ThenPin && ExecPin;
		}
	}

	// Entry -> Set {Set value}
	{
		UEdGraphPin* EntryValuePin = FunctionEntry->FindPin(InInputPinName, EGPD_Output);
		UEdGraphPin* SetValuePin = SetVariableNode->FindPin(InProperty->GetFName(), EGPD_Input);
		if (ensure(EntryValuePin && SetValuePin))
		{
			ensure(Schema->TryCreateConnection(EntryValuePin, SetValuePin));
		}
		bResult = bResult && EntryValuePin && SetValuePin;
	}

	// Set -> Broadcast
	{
		UEdGraphPin* ThenPin = SetVariableNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* ExecPin = CallBroadcast->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (ensure(ThenPin && ExecPin))
		{
			ensure(Schema->TryCreateConnection(ThenPin, ExecPin));
		}
		bResult = bResult && ThenPin && ExecPin;
	}

	return bResult;
}


bool GenerateViewModelFielNotifyBroadcast(FKismetCompilerContext& InContext, UEdGraph* InFunctionGraph, FProperty* InProperty)
{
	check(InFunctionGraph);

	UK2Node_FunctionEntry* FunctionEntry = nullptr;
	for (UEdGraphNode* Node : InFunctionGraph->Nodes)
	{
		FunctionEntry = Cast<UK2Node_FunctionEntry>(Node);
		if (FunctionEntry)
		{
			break;
		}
	}

	if (FunctionEntry == nullptr)
	{
		return false;
	}

	const UEdGraphSchema* Schema = InFunctionGraph->GetSchema();
	if (Schema == nullptr)
	{
		return false;
	}

	UK2Node_CallFunction* CallBroadcast = InContext.SpawnIntermediateNode<UK2Node_CallFunction>(FunctionEntry, InFunctionGraph);
	{
		CallBroadcast->FunctionReference.SetExternalMember("K2_BroadcastFieldValueChanged", UMVVMViewModelBase::StaticClass());
		CallBroadcast->AllocateDefaultPins();

		UEdGraphPin* NamePin = CallBroadcast->FindPin(TEXT("FieldId"), EGPD_Input);
		ensure(NamePin);
		if (!NamePin)
		{
			return false;
		}

		FFieldNotificationId NewPropertyNotifyId;
		NewPropertyNotifyId.FieldName = InProperty->GetFName();

		FString ValueString;
		FFieldNotificationId::StaticStruct()->ExportText(ValueString, &NewPropertyNotifyId, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);

		bool bMarkAsModified = false;
		Schema->TrySetDefaultValue(*NamePin, ValueString, bMarkAsModified);
	}

	bool bResult = true;

	// Entry -> Broadcast
	{
		UEdGraphPin* ThenPin = FunctionEntry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* ExecPin = CallBroadcast->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (ensure(ThenPin && ExecPin))
		{
			ensure(Schema->TryCreateConnection(ThenPin, ExecPin));
		}
		bResult = bResult && ThenPin && ExecPin;
	}

	return bResult;
}

} //namespace