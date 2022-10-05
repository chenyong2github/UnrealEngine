// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bindings/MVVMConversionFunctionHelper.h"

#include "BlueprintTypePromotion.h"
#include "Components/Widget.h"
#include "Containers/Deque.h"
#include "EdGraph/EdGraph.h"
#include "EngineLogs.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMViewModelBase.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "Misc/StringBuilder.h"
#include "UObject/MetaData.h"
#include "View/MVVMView.h"
#include "WidgetBlueprint.h"

namespace UE::MVVM::ConversionFunctionHelper
{
static const FName ConversionFunctionMetadataKey = "ConversionFunction";

namespace Private
{

	void BuildPropertyPath(TArray<FMVVMBlueprintPropertyPath>& Result, FMVVMBlueprintPropertyPath BasePath, UClass* Class, const UK2Node_VariableGet* CurrentNode)
	{
		bool bAddPath = false;
		const UEdGraphPin* OutputPin = CurrentNode->FindPinByPredicate([](UEdGraphPin* Pin) { return Pin->Direction == EGPD_Output; });
		if (OutputPin)
		{
			for (const UEdGraphPin* LinkedTo : OutputPin->LinkedTo)
			{
				if (const UK2Node_VariableGet* LinkToVariable = Cast<UK2Node_VariableGet>(LinkedTo->GetOwningNode()))
				{
					BasePath.SetBasePropertyPath(UE::MVVM::FMVVMConstFieldVariant(LinkToVariable->VariableReference.ResolveMember<FProperty>(Class)));
					BuildPropertyPath(Result, BasePath, Class, LinkToVariable);
				}
				else
				{
					bAddPath = true;
				}
			}
		}

		if (bAddPath)
		{
			Result.Add(BasePath);
		}
	}

	UEdGraph* FindExistingConversionFunctionWrapper(const UWidgetBlueprint* WidgetBlueprint, FName WrapperName)
	{
		const TObjectPtr<UEdGraph>* Result = WidgetBlueprint->FunctionGraphs.FindByPredicate([WrapperName](const UEdGraph* GraphPtr) { return GraphPtr->GetFName() == WrapperName; });
		return Result ? Result->Get() : nullptr;
	}
} // namespace Private

FName CreateWrapperName(const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination)
{
	TStringBuilder<256> StringBuilder;
	StringBuilder << TEXT("__");
	StringBuilder << Binding.GetFName();
	StringBuilder << (bSourceToDestination ? TEXT("_SourceToDest") : TEXT("_DestToSource"));

	return FName(StringBuilder.ToString());
}


UEdGraph* GetGraph(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, bool bSourceToDestination)
{
	check(WidgetBlueprint);
	const FName WrapperName = bSourceToDestination ? Binding.Conversion.SourceToDestinationWrapper : Binding.Conversion.DestinationToSourceWrapper;
	return Private::FindExistingConversionFunctionWrapper(WidgetBlueprint, WrapperName);
}

UK2Node_CallFunction* GetFunctionNode(UEdGraph* Graph)
{
	if (Graph == nullptr)
	{
		return nullptr;
	}

	TArray<UK2Node_CallFunction*> FunctionNodes;
	Graph->GetNodesOfClass<UK2Node_CallFunction>(FunctionNodes);

	if (FunctionNodes.Num() == 1)
	{
		return FunctionNodes[0];
	}

	for (UK2Node_CallFunction* FunctionNode : FunctionNodes)
	{
		// check if we've set any metadata on the nodes to figure out which one it is
		if (FunctionNode->GetPackage()->GetMetaData()->HasValue(FunctionNode, ConversionFunctionMetadataKey))
		{
			return FunctionNode;
		}
	}

	return nullptr;
}

void MarkAsConversionFunction(const UK2Node_CallFunction* FunctionNode, const FMVVMBlueprintViewBinding& Binding)
{
	check(FunctionNode != nullptr);

	FunctionNode->GetPackage()->GetMetaData()->SetValue(FunctionNode, ConversionFunctionMetadataKey, *Binding.BindingId.ToString());
}

namespace Private
{
	FMVVMBlueprintPropertyPath GetPropertyPathForPin(const UWidgetBlueprint* WidgetBlueprint, const UEdGraphPin* StartPin)
	{
		UMVVMWidgetBlueprintExtension_View* ExtensionView = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
		UMVVMBlueprintView* BlueprintView = ExtensionView->GetBlueprintView();

		TArray<FMemberReference> PathParts;
		TDeque<UEdGraphNode*> NodesToSearch;
		NodesToSearch.PushLast(StartPin->LinkedTo[0]->GetOwningNode());

		while (NodesToSearch.Num() > 0)
		{
			UEdGraphNode* Node = NodesToSearch[0];
			NodesToSearch.PopFirst();

			if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
			{
				PathParts.Add(Getter->VariableReference);
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

		Algo::Reverse(PathParts);

		FMVVMBlueprintPropertyPath ResultPath;

		FMemberReference RootRef = PathParts[0];
		PathParts.RemoveAt(0);

		if (const FObjectProperty* Property = CastField<FObjectProperty>(RootRef.ResolveMember<FProperty>(WidgetBlueprint->SkeletonGeneratedClass)))
		{
			if (Property->PropertyClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
			{
				if (const FMVVMBlueprintViewModelContext* ViewModel = BlueprintView->FindViewModel(Property->GetFName()))
				{
					ResultPath.SetViewModelId(ViewModel->GetViewModelId());
				}
			}
			else if (Property->PropertyClass->IsChildOf<UWidget>() || Property->PropertyClass->IsChildOf<UWidgetBlueprint>())
			{
				ResultPath.SetWidgetName(Property->GetFName());
			}
		}

		if (PathParts.Num() == 0)
		{
			return ResultPath;
		}

		for (const FMemberReference& MemberReference : PathParts)
		{
			if (UFunction* Function = MemberReference.ResolveMember<UFunction>(WidgetBlueprint->SkeletonGeneratedClass))
			{
				ResultPath.AppendBasePropertyPath(UE::MVVM::FMVVMConstFieldVariant(Function));
			}
			else if (FProperty* Property = MemberReference.ResolveMember<FProperty>(WidgetBlueprint->SkeletonGeneratedClass))
			{
				ResultPath.AppendBasePropertyPath(UE::MVVM::FMVVMConstFieldVariant(Property));
			}
		}

		return ResultPath;
	}
}

FMVVMBlueprintPropertyPath GetPropertyPathForArgument(const UWidgetBlueprint* WidgetBlueprint, const UK2Node_CallFunction* FunctionNode, FName ArgumentName)
{
	const UEdGraphPin* ArgumentPin = FunctionNode->FindPin(ArgumentName, EGPD_Input);
	if (ArgumentPin == nullptr || ArgumentPin->LinkedTo.Num() == 0)
	{
		return FMVVMBlueprintPropertyPath();
	}

	return Private::GetPropertyPathForPin(WidgetBlueprint, ArgumentPin);
}

TArray<FMVVMBlueprintPropertyPath> GetAllArgumentPathsForConversionNode(const UWidgetBlueprint* WidgetBlueprint, const UK2Node_CallFunction* FunctionNode)
{
	check(FunctionNode);

	TArray<FMVVMBlueprintPropertyPath> Paths;
	for (const UEdGraphPin* Pin : FunctionNode->GetAllPins())
	{
		FMVVMBlueprintPropertyPath Path = Private::GetPropertyPathForPin(WidgetBlueprint, Pin);
		if (!Path.IsEmpty())
		{
			Paths.Add(Path);
		}
	}

	return Paths;
}

} //namespace UE::MVVM