// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bindings/MVVMConversionFunctionHelper.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Components/Widget.h"
#include "Containers/Deque.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewConversionFunction.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "UObject/MetaData.h"

#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace UE::MVVM::ConversionFunctionHelper
{

namespace Private
{
	static const FLazyName ConversionFunctionMetadataKey = "ConversionFunction";
	static const FLazyName ConversionFunctionCategory = "Conversion Functions";

	UMVVMBlueprintView* GetView(const UBlueprint* Blueprint)
	{
		const TObjectPtr<UBlueprintExtension>* ExtensionViewPtr = Blueprint->GetExtensions().FindByPredicate([](const UBlueprintExtension* Other) { return Other && Other->GetClass() == UMVVMWidgetBlueprintExtension_View::StaticClass(); });
		if (ExtensionViewPtr)
		{
			return CastChecked<UMVVMWidgetBlueprintExtension_View>(*ExtensionViewPtr)->GetBlueprintView();
		}
		return nullptr;
	}

	void MarkAsConversionFunction(const UK2Node* FunctionNode, const UEdGraph* Graph)
	{
		check(FunctionNode != nullptr);
		FunctionNode->GetPackage()->GetMetaData()->SetValue(FunctionNode, ConversionFunctionMetadataKey.Resolve(), TEXT(""));
	}

	UK2Node_FunctionEntry* FindFunctionEntry(UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				return FunctionEntry;
			}
		}
		return nullptr;
	}
	
	UK2Node_FunctionResult* FindFunctionResult(UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionResult* FunctionResult = Cast<UK2Node_FunctionResult>(Node))
			{
				return FunctionResult;
			}
		}
		return nullptr;
	}

	TTuple<UEdGraph*, UK2Node_FunctionEntry*, UK2Node_FunctionResult*> CreateGraph(UBlueprint* Blueprint, FName GraphName, bool bIsEditable, bool bAddToBlueprint)
	{
		FName UniqueFunctionName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, GraphName.ToString());
		UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, UniqueFunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FunctionGraph->bEditable = bIsEditable;
		if (bAddToBlueprint)
		{
			Blueprint->FunctionGraphs.Add(FunctionGraph);
		}
		else
		{
			FunctionGraph->SetFlags(RF_Transient);
		}

		const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(FunctionGraph->GetSchema());
		Schema->MarkFunctionEntryAsEditable(FunctionGraph, bIsEditable);
		Schema->CreateDefaultNodesForGraph(*FunctionGraph);

		// function entry node
		FGraphNodeCreator<UK2Node_FunctionEntry> FunctionEntryCreator(*FunctionGraph);
		UK2Node_FunctionEntry* FunctionEntry = FunctionEntryCreator.CreateNode();
		FunctionEntry->FunctionReference.SetSelfMember(FunctionGraph->GetFName());
		FunctionEntry->AddExtraFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_Const | FUNC_Protected | FUNC_Final);
		FunctionEntry->bIsEditable = bIsEditable;
		FunctionEntry->MetaData.Category = FText::FromName(ConversionFunctionCategory.Resolve());
		FunctionEntry->NodePosX = -500;
		FunctionEntry->NodePosY = 0;
		FunctionEntryCreator.Finalize();

		FGraphNodeCreator<UK2Node_FunctionResult> FunctionResultCreator(*FunctionGraph);
		UK2Node_FunctionResult* FunctionResult = FunctionResultCreator.CreateNode();
		FunctionResult->FunctionReference.SetSelfMember(FunctionGraph->GetFName());
		FunctionResult->bIsEditable = bIsEditable;
		FunctionResult->NodePosX = 500;
		FunctionResult->NodePosY = 0;
		FunctionResultCreator.Finalize();

		return { FunctionGraph, FunctionEntry, FunctionResult };
	}
}

bool RequiresWrapper(const UFunction* ConversionFunction)
{
	if (ConversionFunction == nullptr)
	{
		return false;
	}

	TValueOrError<TArray<const FProperty*>, FText> ArgumentsResult = UE::MVVM::BindingHelper::TryGetArgumentsForConversionFunction(ConversionFunction);
	if (ArgumentsResult.HasValue())
	{
		return (ArgumentsResult.GetValue().Num() > 1);
	}
	return false;
}

FName CreateWrapperName(const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination)
{
	TStringBuilder<256> StringBuilder;
	StringBuilder << TEXT("__");
	StringBuilder << Binding.GetFName();
	StringBuilder << (bSourceToDestination ? TEXT("_SourceToDest") : TEXT("_DestToSource"));

	return FName(StringBuilder.ToString());
}

TPair<UEdGraph*, UK2Node*> CreateGraph(UBlueprint* Blueprint, FName GraphName, const UFunction* ConversionFunction, bool bTransient)
{
	bool bIsEditable = false;
	bool bAddToBlueprint = !bTransient;

	UEdGraph* FunctionGraph = nullptr;
	UK2Node_FunctionEntry* FunctionEntry = nullptr;
	UK2Node_FunctionResult* FunctionResult = nullptr;
	{
		TTuple<UEdGraph*, UK2Node_FunctionEntry*, UK2Node_FunctionResult*> NewGraph = Private::CreateGraph(Blueprint, GraphName, bIsEditable, bAddToBlueprint);
		FunctionGraph = NewGraph.Get<0>();
		FunctionEntry = NewGraph.Get<1>();
		FunctionResult = NewGraph.Get<2>();
	}

	const FProperty* ReturnProperty = UE::MVVM::BindingHelper::GetReturnProperty(ConversionFunction);
	check(ReturnProperty);

	// create return value pin
	{
		TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
		GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(ReturnProperty, PinInfo->PinType);
		PinInfo->PinName = ReturnProperty->GetFName();
		PinInfo->DesiredPinDirection = EGPD_Input;
		FunctionResult->UserDefinedPins.Add(PinInfo);
		FunctionResult->ReconstructNode();
	}
	
	UK2Node_CallFunction* CallFunctionNode = nullptr;
	{
		FGraphNodeCreator<UK2Node_CallFunction> CallFunctionCreator(*FunctionGraph);
		CallFunctionNode = CallFunctionCreator.CreateNode();
		CallFunctionNode->SetFromFunction(ConversionFunction);
		CallFunctionNode->NodePosX = 0;
		CallFunctionCreator.Finalize();
		Private::MarkAsConversionFunction(CallFunctionNode, FunctionGraph);
	}

	// Make link Entry -> CallFunction || Entry -> Return
	{
		UEdGraphPin* FunctionEntryThenPin = FunctionEntry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		UEdGraphPin* FunctionResultExecPin = FunctionResult->GetExecPin();
		
		if (!CallFunctionNode->IsNodePure())
		{
			UEdGraphPin* CallFunctionExecPin = CallFunctionNode->GetExecPin();
			UEdGraphPin* CallFunctionThenPin = CallFunctionNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);

			FunctionEntryThenPin->MakeLinkTo(CallFunctionExecPin);
			CallFunctionThenPin->MakeLinkTo(FunctionResultExecPin);

			CallFunctionNode->NodePosY = 0;
		}
		else
		{
			FunctionEntryThenPin->MakeLinkTo(FunctionResultExecPin);
			CallFunctionNode->NodePosY = 100;
		}
	}

	{
		UEdGraphPin* FunctionReturnPin = CallFunctionNode->FindPin(ReturnProperty->GetName(), EGPD_Output);
		UEdGraphPin* FunctionResultPin = FunctionResult->FindPin(ReturnProperty->GetFName(), EGPD_Input);
		check(FunctionResultPin && FunctionReturnPin);
		FunctionReturnPin->MakeLinkTo(FunctionResultPin);
	}

	return { FunctionGraph , CallFunctionNode };
}

UK2Node* GetWrapperNode(UEdGraph* Graph)
{
	if (Graph == nullptr)
	{
		return nullptr;
	}

	FName ConversionFunctionMetadataKey = Private::ConversionFunctionMetadataKey.Resolve();
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		// check if we've set any metadata on the nodes to figure out which one it is
		if (Cast<UK2Node>(Node) && Node->GetPackage()->GetMetaData()->HasValue(Node, ConversionFunctionMetadataKey))
		{
			return CastChecked<UK2Node>(Node);
		}
	}

	if (UK2Node_FunctionResult* FunctionResult = Private::FindFunctionResult(Graph))
	{
		for (UEdGraphPin* GraphPin : FunctionResult->Pins)
		{
			if (GraphPin->GetFName() != UEdGraphSchema_K2::PN_Execute && GraphPin->LinkedTo.Num() == 1)
			{
				if (UK2Node* Node = Cast<UK2Node>(GraphPin->LinkedTo[0]->GetOwningNode()))
				{
					Private::MarkAsConversionFunction(Node, Graph);
					return Node;
				}
			}
		}
	}

	return nullptr;
}

namespace Private
{
	FMVVMBlueprintPropertyPath GetPropertyPathForPin(const UBlueprint* Blueprint, const UEdGraphPin* StartPin, bool bSkipResolve)
	{
		if (StartPin->Direction != EGPD_Input || StartPin->PinName == UEdGraphSchema_K2::PN_Self)
		{
			return FMVVMBlueprintPropertyPath();
		}
		
		UMVVMBlueprintView* BlueprintView = GetView(Blueprint);
		if (BlueprintView == nullptr)
		{
			return FMVVMBlueprintPropertyPath();
		}

		TArray<FMemberReference> PathParts;
		TDeque<UEdGraphNode*> NodesToSearch;

		if (StartPin->LinkedTo.Num() > 0)
		{
			if (UEdGraphNode* Node = StartPin->LinkedTo[0]->GetOwningNode())
			{
				NodesToSearch.PushLast(Node);
			}
		}

		while (NodesToSearch.Num() > 0)
		{
			UEdGraphNode* Node = NodesToSearch[0];
			NodesToSearch.PopFirst();

			if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
			{
				PathParts.Insert(Getter->VariableReference, 0);
			}
			else if (UK2Node_CallFunction* Function = Cast<UK2Node_CallFunction>(Node))
			{
				PathParts.Insert(Function->FunctionReference, 0);
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0)
				{
					NodesToSearch.PushLast(Pin->LinkedTo[0]->GetOwningNode());
				}
			}
		}

		if (PathParts.Num() == 0)
		{
			return FMVVMBlueprintPropertyPath();
		}

		FMVVMBlueprintPropertyPath ResultPath;

		FMemberReference RootRef = PathParts[0];
		PathParts.RemoveAt(0);

		if (bSkipResolve)
		{
			// if the generated class hasn't yet been generated we can blindly forge ahead and try to figure out if it's a widget or a viewmodel
			if (const FMVVMBlueprintViewModelContext* ViewModel = BlueprintView->FindViewModel(RootRef.GetMemberName()))
			{
				ResultPath.SetViewModelId(ViewModel->GetViewModelId());
			}
			else
			{
				ResultPath.SetWidgetName(RootRef.GetMemberName());
			}
		}
		else
		{
			if (const FObjectProperty* Property = CastField<FObjectProperty>(RootRef.ResolveMember<FProperty>(Blueprint->SkeletonGeneratedClass)))
			{
				if (Property->PropertyClass->IsChildOf<UWidget>() || Property->PropertyClass->IsChildOf<UBlueprint>())
				{
					ResultPath.SetWidgetName(Property->GetFName());
				}
				else if (Property->PropertyClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
				{
					if (const FMVVMBlueprintViewModelContext* ViewModel = BlueprintView->FindViewModel(Property->GetFName()))
					{
						ResultPath.SetViewModelId(ViewModel->GetViewModelId());
					}
				}
			}
		}

		if (PathParts.Num() == 0)
		{
			return ResultPath;
		}

		for (const FMemberReference& MemberReference : PathParts)
		{
			if (UFunction* Function = MemberReference.ResolveMember<UFunction>(Blueprint->SkeletonGeneratedClass)) 
			{
				ResultPath.AppendPropertyPath(Blueprint, UE::MVVM::FMVVMConstFieldVariant(Function));
			}
			else if (const FProperty* Property = MemberReference.ResolveMember<FProperty>(Blueprint->SkeletonGeneratedClass))
			{
				ResultPath.AppendPropertyPath(Blueprint, UE::MVVM::FMVVMConstFieldVariant(Property));
			}
		}

		return ResultPath;
	}
}

FMVVMBlueprintPropertyPath GetPropertyPathForPin(const UBlueprint* Blueprint, const UEdGraphPin* Pin, bool bSkipResolve)
{
	if (Pin == nullptr || Pin->LinkedTo.Num() == 0)
	{
		return FMVVMBlueprintPropertyPath();
	}

	return Private::GetPropertyPathForPin(Blueprint, Pin, bSkipResolve);
}

void SetPropertyPathForPin(const UBlueprint* Blueprint, const FMVVMBlueprintPropertyPath& Path, UEdGraphPin* PathPin)
{
	if (PathPin == nullptr)
	{
		return;
	}

	UEdGraphNode* ConversionNode = PathPin->GetOwningNode();
	UEdGraph* FunctionGraph = ConversionNode ? ConversionNode->GetGraph() : nullptr;
	const UEdGraphSchema* Schema = FunctionGraph ? FunctionGraph->GetSchema() : nullptr;

	// Remove previous nodes
	{
		TArray<UEdGraphPin*> PinsToClear;
		TArray<UEdGraphNode*> NodesToRemove;
		PinsToClear.Add(PathPin);
		while (PinsToClear.Num() > 0)
		{
			UEdGraphPin* Pin = PinsToClear[0];
			PinsToClear.RemoveAt(0);

			PinsToClear.Append(Pin->LinkedTo);
			Pin->BreakAllPinLinks();

			// rename the node out of the way
			UEdGraphNode* Node = Pin->GetOwningNode();
			if (Node != ConversionNode)
			{
				NodesToRemove.AddUnique(Node);
			}
		}

		if (FunctionGraph)
		{
			for (UEdGraphNode* Node : NodesToRemove)
			{
				// Break the links, and relink to the other nodes.
				if (UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node))
				{
					UEdGraphPin* ThenPin = CallFunctionNode->GetThenPin();
					UEdGraphPin* ExecPin = CallFunctionNode->GetExecPin();
					if (ThenPin && ThenPin->LinkedTo.Num() == 1)
					{
						Schema->MovePinLinks(*ExecPin, *ThenPin->LinkedTo[0], false);
					}
				}
				FunctionGraph->RemoveNode(Node, true);
			}
		}
	}

	// Add new nodes
	if (FunctionGraph && ConversionNode && !Path.IsEmpty())
	{
		const int32 ArgumentIndex = ConversionNode->Pins.IndexOfByPredicate([PathPin](const UEdGraphPin* Other){ return Other == PathPin; });
		if (!ensure(ConversionNode->Pins.IsValidIndex(ArgumentIndex)))
		{
			return;
		}

		TArray<UE::MVVM::FMVVMConstFieldVariant> Fields = Path.GetFields(Blueprint->SkeletonGeneratedClass);
		float PosX = ConversionNode->NodePosX - 300 * (Fields.Num() + 1);
		const float PosY = ConversionNode->NodePosY + ArgumentIndex * 100;

		UEdGraphPin* PreviousExecPin = nullptr;
		{
			UEdGraphNode* FunctionEntry = Private::FindFunctionEntry(FunctionGraph);
			if (!ensure(FunctionEntry))
			{
				return;
			}
			PreviousExecPin = FunctionEntry->FindPinChecked(UEdGraphSchema_K2::PN_Then);
		}
		UEdGraphPin* PreviousDataPin = nullptr;
		UClass* PreviousClass = nullptr;

		// create the root property getter node, ie. the Widget/ViewModel
		{
			const FProperty* RootProperty = nullptr;
			bool bCreateSelfNodeForRootProperty = false;
			{
				if (Path.IsFromWidget())
				{
					RootProperty = Blueprint->SkeletonGeneratedClass->FindPropertyByName(Path.GetWidgetName());
					bCreateSelfNodeForRootProperty = Blueprint->GetFName() == Path.GetWidgetName();
				}
				else if (Path.IsFromViewModel())
				{
					UMVVMBlueprintView* View = Private::GetView(Blueprint);
					const FMVVMBlueprintViewModelContext* Context = View->FindViewModel(Path.GetViewModelId());
					RootProperty = Blueprint->SkeletonGeneratedClass->FindPropertyByName(Context->GetViewModelName());
				}

				if (RootProperty == nullptr)
				{
					ensureMsgf(bCreateSelfNodeForRootProperty, TEXT("Could not resolve root property!"));
					return;
				}
			}

			if (bCreateSelfNodeForRootProperty)
			{
				FGraphNodeCreator<UK2Node_Self> RootGetterCreator(*FunctionGraph);
				UK2Node_Self* RootSelfNode = RootGetterCreator.CreateNode();
				RootSelfNode->NodePosX = PosX;
				RootSelfNode->NodePosY = PosY;
				RootGetterCreator.Finalize();

				PreviousDataPin = RootSelfNode->FindPinChecked(UEdGraphSchema_K2::PSC_Self);
				PreviousClass = Blueprint->SkeletonGeneratedClass;
			}
			else
			{
				FGraphNodeCreator<UK2Node_VariableGet> RootGetterCreator(*FunctionGraph);
				UK2Node_VariableGet* RootGetterNode = RootGetterCreator.CreateNode();
				RootGetterNode->NodePosX = PosX;
				RootGetterNode->NodePosY = PosY;
				RootGetterNode->VariableReference.SetFromField<FProperty>(RootProperty, true, Blueprint->SkeletonGeneratedClass);
				RootGetterCreator.Finalize();

				PreviousDataPin = RootGetterNode->Pins[0];
				PreviousClass = CastField<FObjectProperty>(RootProperty)->PropertyClass;
			}

			PosX += 300;
		}

		// create all the subsequent nodes in the path
		for (int32 Index = 0; Index < Fields.Num(); ++Index)
		{
			if (!ensureMsgf(PreviousClass != nullptr, TEXT("Previous class not set!")))
			{
				return;
			}

			UEdGraphNode* NewNode = nullptr;
			const UE::MVVM::FMVVMConstFieldVariant& Field = Fields[Index];
			const bool bLastField = (Index == Fields.Num() - 1);
			if (Field.IsProperty())
			{
				const FProperty* Property = Field.GetProperty();

				// for structs in the middle of a path, we need to use a break node
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				if (!bLastField && StructProperty != nullptr)
				{
					FGraphNodeCreator<UK2Node_BreakStruct> BreakCreator(*FunctionGraph);
					UK2Node_BreakStruct* BreakNode = BreakCreator.CreateNode();
					BreakNode->AllocateDefaultPins();
					BreakCreator.Finalize();

					NewNode = BreakNode;

					PreviousClass = nullptr;
				}
				else if (PreviousClass != nullptr)
				{
					FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(*FunctionGraph);
					UK2Node_VariableGet* GetterNode = GetterCreator.CreateNode();
					GetterNode->SetFromProperty(Property, false, PreviousClass);
					GetterNode->AllocateDefaultPins();
					GetterCreator.Finalize();

					NewNode = GetterNode;
					if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
					{
						PreviousClass = ObjectProperty->PropertyClass;
					}
					else
					{
						PreviousClass = nullptr;
					}
				}
			}
			else if (Field.IsFunction())
			{
				const UFunction* Function = Field.GetFunction();

				FGraphNodeCreator<UK2Node_CallFunction> CallFunctionCreator(*FunctionGraph);
				UK2Node_CallFunction* FunctionNode = CallFunctionCreator.CreateNode();
				FunctionNode->SetFromFunction(Function);
				FunctionNode->AllocateDefaultPins();
				CallFunctionCreator.Finalize();

				NewNode = FunctionNode;

				const FProperty* ReturnProperty = UE::MVVM::BindingHelper::GetReturnProperty(Function);
				if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ReturnProperty))
				{
					PreviousClass = ObjectProperty->PropertyClass;
				}
				else
				{
					PreviousClass = nullptr;
				}
			}
			else
			{
				ensureMsgf(false, TEXT("Invalid path, empty field in path."));
				return;
			}

			// create new connections
			for (UEdGraphPin* Pin : NewNode->Pins)
			{
				if (Pin->Direction == EGPD_Input)
				{
					if (Pin->PinName == UEdGraphSchema_K2::PN_Execute)
					{
						Schema->MovePinLinks(*PreviousExecPin, *Pin, false);
					}
					else if (Schema->ArePinsCompatible(PreviousDataPin, Pin, PreviousClass))
					{
						Pin->MakeLinkTo(PreviousDataPin);
					}
				}
			}

			// then update our previous pin pointers
			for (UEdGraphPin* Pin : NewNode->Pins)
			{
				if (Pin->Direction == EGPD_Output)
				{
					if (Pin->PinName == UEdGraphSchema_K2::PN_Then)
					{
						PreviousExecPin = Pin;
					}
					else
					{
						PreviousDataPin = Pin;
					}
				}
			}

			NewNode->NodePosX = PosX;
			NewNode->NodePosY = PosY;

			PosX += 300;
		}

		// finish by linking to our ultimate destinations
		UEdGraphPin* LastExecPin = ConversionNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (LastExecPin == nullptr)
		{
			LastExecPin = Private::FindFunctionResult(FunctionGraph)->FindPin(UEdGraphSchema_K2::PN_Execute);
		}
		PreviousExecPin->MakeLinkTo(LastExecPin);
		PreviousDataPin->MakeLinkTo(PathPin);
	}
}

FMVVMBlueprintPropertyPath GetPropertyPathForArgument(const UBlueprint* WidgetBlueprint, const UK2Node_CallFunction* FunctionNode, FName ArgumentName, bool bSkipResolve)
{
	const UEdGraphPin* ArgumentPin = FunctionNode->FindPin(ArgumentName, EGPD_Input);
	return GetPropertyPathForPin(WidgetBlueprint, ArgumentPin, bSkipResolve);
}

TMap<FName, FMVVMBlueprintPropertyPath> GetAllArgumentPropertyPaths(const UBlueprint* Blueprint, const UK2Node_CallFunction* FunctionNode, bool bSkipResolve)
{
	check(FunctionNode);

	TMap<FName, FMVVMBlueprintPropertyPath> Paths;
	for (const UEdGraphPin* Pin : FunctionNode->GetAllPins())
	{
		FMVVMBlueprintPropertyPath Path = Private::GetPropertyPathForPin(Blueprint, Pin, bSkipResolve);
		if (!Path.IsEmpty())
		{
			Paths.Add(Pin->PinName, Path);
		}
	}

	return Paths;
}

TMap<FName, FMVVMBlueprintPropertyPath> GetAllArgumentPropertyPaths(const UBlueprint* Blueprint, const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination, bool bSkipResolve)
{
	if (UMVVMBlueprintViewConversionFunction* ConversionFunction = Binding.Conversion.GetConversionFunction(bSourceToDestination))
	{
		if (UEdGraph* ConversionFunctionGraph = ConversionFunction->GetWrapperGraph())
		{
			if (UK2Node_CallFunction* ConversionNode = Cast<UK2Node_CallFunction>(ConversionFunctionHelper::GetWrapperNode(ConversionFunctionGraph)))
			{
				return GetAllArgumentPropertyPaths(Blueprint, ConversionNode, bSkipResolve);
			}
		}
	}

	return TMap<FName, FMVVMBlueprintPropertyPath>();
}

} //namespace UE::MVVM
