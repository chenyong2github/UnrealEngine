// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_WebAPIOperation.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintTypePromotion.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_StructOperation.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "WebAPIOperationObject.h"
#include "Algo/AllOf.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "K2Node_WebAPIAsyncOperation"

struct FGetPinName
{
	static const FName& GetMessagePinName() { static const FName MessagePinName(TEXT("Message")); return MessagePinName; }
	static const FName& GetSuccessPinName() { static const FName SuccessPinName(TEXT("OnSuccess")); return SuccessPinName; }
	static const FName& GetErrorPinName() { static const FName ErrorPinName(TEXT("OnError")); return ErrorPinName; }
};

UK2Node_WebAPIOperation::UK2Node_WebAPIOperation()
{
	ProxyActivateFunctionName = NAME_None;

	// @todo:
	// 1. on new node spawned, break Request (input) pin by default: FBlueprintEditor::OnSplitStructPin()
	// 2. option to convert to/from callback styles: UK2Node_DynamicCast::SetPurity(bool bNewPurity)
	// 3. collapse output responses that inherit MessageResponse to single message output pin
}

void UK2Node_WebAPIOperation::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();
}

FText UK2Node_WebAPIOperation::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FText FunctionName = Super::GetNodeTitle(TitleType);
	FText NamespaceName;
	FText ServiceName;
	
	if (const UFunction* Function = GetFactoryFunction())
	{
		NamespaceName = Function->GetOuterUClass()->GetMetaDataText(TEXT("Namespace"));
		ServiceName = Function->GetOuterUClass()->GetMetaDataText(TEXT("Service"));
	}
	
	if(TitleType == ENodeTitleType::FullTitle)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("FunctionName"), FunctionName);
		Args.Add(TEXT("NamespaceName"), NamespaceName);
		Args.Add(TEXT("ServiceName"), ServiceName);

		if (NamespaceName.IsEmpty() && ServiceName.IsEmpty())
		{
			return FText::Format(LOCTEXT("NodeTitle", "{FunctionName}"), Args);
		}
		else if (NamespaceName.IsEmpty())
		{
			return FText::Format(LOCTEXT("NodeTitle_WithNamespace", "{FunctionName}\n{ServiceName}"), Args);
		}
		else if (ServiceName.IsEmpty())
		{
			return FText::Format(LOCTEXT("NodeTitle_WithService", "{FunctionName}\n{NamespaceName}"), Args);
		}
		else
		{
			return FText::Format(LOCTEXT("NodeTitle_WithNamespaceAndService", "{FunctionName}\n{NamespaceName}: {ServiceName}"), Args);
		}		
	}
	else
	{
		return FunctionName;
	}
}

void UK2Node_WebAPIOperation::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Context->bIsDebugging)
	{
		{
			FText MenuEntryTitle = LOCTEXT("MakeCallbackTitle", "Convert to Callback responses");
			FText MenuEntryTooltip = LOCTEXT("MakeCallbackTooltip", "Removes the execution pins and instead adds callbacks.");

			bool bCanToggleAsyncType = true;
			auto CanExecuteAsyncTypeToggle = [](const bool bInCanToggleAsyncType)->bool
			{
				return bInCanToggleAsyncType;
			};

			if (OperationAsyncType == EWebAPIOperationAsyncType::Callback)
			{
				MenuEntryTitle = LOCTEXT("MakeLatentActionTitle", "Convert to Latent Action");
				MenuEntryTooltip = LOCTEXT("MakeLatentActionTooltip", "Adds in branching execution pins so that you can separatly handle the responses.");

				const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(GetSchema());
				check(K2Schema != nullptr);

				bCanToggleAsyncType = K2Schema->DoesGraphSupportImpureFunctions(GetGraph()); // @todo 
				if (!bCanToggleAsyncType)
				{
					MenuEntryTooltip = LOCTEXT("CannotMakeLatentActionTooltip", "This graph does not support latent actions.");
				}
			}

			FToolMenuSection& Section = Menu->AddSection("K2NodeWebAPIOperation", LOCTEXT("AsyncTypeHeader", "Async Type"));
			Section.AddMenuEntry(
				"ToggleAsyncType",
				MenuEntryTitle,
				MenuEntryTooltip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateUObject(const_cast<UK2Node_WebAPIOperation*>(this), &UK2Node_WebAPIOperation::ToggleAsyncType),
					FCanExecuteAction::CreateStatic(CanExecuteAsyncTypeToggle, bCanToggleAsyncType),
					FIsActionChecked()
				)
			);
		}
	}
}

bool UK2Node_WebAPIOperation::CanPasteHere(const UEdGraph* TargetGraph) const
{
	// @todo: unlike default async node, you can paste into a function (not event graph) and this should convert to callback style
	return Super::CanPasteHere(TargetGraph);
}

void UK2Node_WebAPIOperation::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	PostReconstructNode();
}

void UK2Node_WebAPIOperation::PostPasteNode()
{
	Super::PostPasteNode();

	// @todo: handle conversion to/from callback style
}

void UK2Node_WebAPIOperation::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	struct GetMenuActions_Utils
	{
		static void SetNodeFunc(UEdGraphNode* NewNode, bool /*bIsTemplateNode*/, TWeakObjectPtr<UFunction> FunctionPtr)
		{
			UK2Node_WebAPIOperation* AsyncTaskNode = CastChecked<UK2Node_WebAPIOperation>(NewNode);
			if (FunctionPtr.IsValid())
			{
				const UFunction* Func = FunctionPtr.Get();
				FObjectProperty* ReturnProp = CastFieldChecked<FObjectProperty>(Func->GetReturnProperty());

				AsyncTaskNode->ProxyFactoryFunctionName = Func->GetFName();
				AsyncTaskNode->ProxyFactoryClass        = Func->GetOuterUClass();
				AsyncTaskNode->ProxyClass               = ReturnProp->PropertyClass;				
			}
		}
	};

	UClass* NodeClass = GetClass();
	ActionRegistrar.RegisterClassFactoryActions<UWebAPIOperationObject>(FBlueprintActionDatabaseRegistrar::FMakeFuncSpawnerDelegate::CreateLambda([NodeClass](const UFunction* FactoryFunc)->UBlueprintNodeSpawner*
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintFunctionNodeSpawner::Create(FactoryFunc);
		check(NodeSpawner != nullptr);
		
		NodeSpawner->NodeClass = NodeClass;

		const TWeakObjectPtr<UFunction> FunctionPtr = MakeWeakObjectPtr(const_cast<UFunction*>(FactoryFunc));
		NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(GetMenuActions_Utils::SetNodeFunc, FunctionPtr);

		return NodeSpawner;
	}));
}

void UK2Node_WebAPIOperation::PostReconstructNode()
{
	Super::PostReconstructNode();

	if(IsValid())
	{
		SplitRequestPins(GetRequestPins());
		SplitResponsePins(GetResponsePins());
		SplitResponsePins(GetErrorResponsePins());
	}
}

void UK2Node_WebAPIOperation::SetAsyncType(EWebAPIOperationAsyncType InAsyncType)
{
	if (InAsyncType != OperationAsyncType)
	{
		OperationAsyncType = InAsyncType;

		const bool bHasBeenConstructed = (Pins.Num() > 0);
		if (bHasBeenConstructed)
		{
			ReconstructNode();
		}
	}
}

bool UK2Node_WebAPIOperation::IsValid() const
{
	return ProxyFactoryFunctionName != NAME_None
		&& ProxyClass != nullptr
		&& ((bHasCompilerMessage && ErrorType >= EMessageSeverity::Info) || !bHasCompilerMessage);
}

UEdGraphPin* UK2Node_WebAPIOperation::FindPinContainingName(
	const EEdGraphPinDirection& InPinDirection,
	const FString& InName) const
{
	return FindPinByPredicate([&InPinDirection, &InName](const UEdGraphPin* InPin)
	{
		return InPin->Direction == InPinDirection
			&& InPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
			&& InPin->GetName().Contains(InName);
	});
}

TArray<UEdGraphPin*> UK2Node_WebAPIOperation::FindPinsContainingName(
	const EEdGraphPinDirection& InPinDirection,
	const FString& InName) const
{
	return Pins.FilterByPredicate([&InPinDirection, InName](const UEdGraphPin* InPin)
	{
		return InPin->Direction == InPinDirection
			&& InPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
			&& InPin->GetName().Contains(InName);
	});
}

TArray<UEdGraphPin*> UK2Node_WebAPIOperation::GetRequestPins() const
{
	TArray<UEdGraphPin*> RequestPins = FindPinsContainingName(EGPD_Input, TEXT("Request"));
	ensureMsgf(!RequestPins.IsEmpty(), TEXT("The function must contain a parameter with \"Request\" in the name."));	
	return RequestPins;
}

TArray<UEdGraphPin*> UK2Node_WebAPIOperation::GetResponsePins() const
{
	auto ResponsePins = FindPinsContainingName(EGPD_Output, TEXT("Response"));
	ensureMsgf(!ResponsePins.IsEmpty(), TEXT("The Operation must contain a delegate with a parameter containing \"Response\" in the name."));	
	return ResponsePins;
}

TArray<UEdGraphPin*> UK2Node_WebAPIOperation::GetErrorResponsePins() const
{	
	auto ErrorResponsePins = FindPinsContainingName(EGPD_Output, TEXT("Error"));
	ensureMsgf(!ErrorResponsePins.IsEmpty(), TEXT("The Operation must contain a delegate with a parameter containing \"Error\" in the name."));	
	return ErrorResponsePins;
}

// @todo: recursively split where struct is basically a wrapper for another struct
void UK2Node_WebAPIOperation::SplitRequestPins(const TArray<UEdGraphPin*>& InPins) const
{
	// If UWebAPIOperation base class, the pin won't always be valid, so ignore.
	if(!ensureAlways(!InPins.IsEmpty() && Algo::AllOf(InPins, [](const UEdGraphPin* InPin) { return InPin; })))
	{
		return;
	}

	for(UEdGraphPin* Pin : InPins)
	{
		// Split if not already
		if (Pin->SubPins.Num() == 0)
		{
			if(CanSplitPin(Pin))
			{
				const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
				K2Schema->SplitPin(Pin);
			}
		}

		// Removes the prefix from the auto-named split pins 
		if (Pin->SubPins.Num() > 0)
		{
			for (UEdGraphPin* SubPin : Pin->SubPins)
			{
				FString SubPinName = SubPin->PinName.ToString();
				CleanupPinNameInline(SubPinName);

				//SubPin->Modify();			
				SubPin->PinFriendlyName = FText::FromString(SubPinName);
			}
		}
	}
}

// Splits if the Response contains a single property
void UK2Node_WebAPIOperation::SplitResponsePins(const TArray<UEdGraphPin*>& InPins) const
{
	// Compilation errors or stale nodes should be warnings, not errors
	if(!ensureAlways(!InPins.IsEmpty() && Algo::AllOf(InPins, [](const UEdGraphPin* InPin) { return InPin; })))
	{
		return;	
	}

	for(UEdGraphPin* Pin : InPins)
	{
		if (const UScriptStruct* PinStructType = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			uint32 PropertyNum = 0;
			
			// Split if not already
			if (Pin->SubPins.Num() == 0 && CanSplitPin(Pin))
			{
				for (TFieldIterator<const FProperty> PropertyIt(PinStructType, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); PropertyIt; ++PropertyIt)
				{
					if (PropertyIt->HasAnyPropertyFlags(CPF_Transient))
					{
						continue;
					}

					PropertyNum++;
				}

				if(PropertyNum == 1)
				{
					// Split, result should be single pin (cause it only contained a single property!)
					const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
					K2Schema->SplitPin(Pin);
				}
			}

			// Removes the prefix from the auto-named split pins 
			if (Pin->SubPins.Num() > 0)
			{
				for (UEdGraphPin* SubPin : Pin->SubPins)
				{
					FString SubPinName = SubPin->PinName.ToString();
					CleanupPinNameInline(SubPinName);

					//SubPin->Modify();			
					SubPin->PinFriendlyName = FText::FromString(SubPinName);
				}
			}
		}
	}
}

void UK2Node_WebAPIOperation::CleanupPinNameInline(FString& InPinName)
{
	int32 UnderscorePosition = -1;
	if(InPinName.FindLastChar(TCHAR('_'), UnderscorePosition))
	{
		InPinName = InPinName.RightChop(UnderscorePosition + 1);
	}
}

void UK2Node_WebAPIOperation::ToggleAsyncType()
{
	const FText TransactionTitle =
		OperationAsyncType == EWebAPIOperationAsyncType::Callback
		? LOCTEXT("ToggleAsyncTypeToLatentAction", "Convert to Latent Action")
		: LOCTEXT("ToggleAsyncTypeToCallback", "Convert to Callback responses");
	const FScopedTransaction Transaction(TransactionTitle);
	Modify();

	SetAsyncType(StaticCast<EWebAPIOperationAsyncType>(FMath::Abs(StaticCast<int8>(OperationAsyncType) - 1)));
}

bool UK2Node_WebAPIOperation::ReconnectExecPins(TArray<UEdGraphPin*>& OldPins)
{
	// @todo: handle when converting to/from callback style
	
	return true;
}

#undef LOCTEXT_NAMESPACE
