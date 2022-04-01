// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMFunctionGraphHelper.h"

#include "EngineLogs.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "View/MVVMView.h"


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

} //namespace