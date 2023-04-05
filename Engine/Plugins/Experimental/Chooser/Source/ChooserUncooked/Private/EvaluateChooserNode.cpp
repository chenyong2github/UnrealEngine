// Copyright Epic Games, Inc. All Rights Reserved.


#include "EvaluateChooserNode.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "ChooserFunctionLibrary.h"
#include "Containers/UnrealString.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EditorCategoryUtils.h"
#include "Internationalization/Internationalization.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MakeArray.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompiler.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CString.h"
#include "Templates/Casts.h"
#include "UObject/Class.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "K2Node_Self.h"


#define LOCTEXT_NAMESPACE "EvaluateChooserNode"

/////////////////////////////////////////////////////
// UK2Node_EvaluateChooser

UK2Node_EvaluateChooser::UK2Node_EvaluateChooser(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeTooltip = LOCTEXT("NodeTooltip", "Evaluates an Chooser Table, and returns the resulting Object or Objects.");
}

void UK2Node_EvaluateChooser::UnregisterChooserCallback()
{
	if(CurrentCallbackChooser)
	{
		CurrentCallbackChooser->OnOutputObjectTypeChanged.RemoveAll(this);
		CurrentCallbackChooser = nullptr;
	}
}

void UK2Node_EvaluateChooser::BeginDestroy()
{
	UnregisterChooserCallback();
	
	Super::BeginDestroy();
}

void UK2Node_EvaluateChooser::PostEditUndo()
{
	Super::PostEditUndo();
	ChooserChanged();
}

void UK2Node_EvaluateChooser::DestroyNode()
{
	UnregisterChooserCallback();
	Super::DestroyNode();
}


void UK2Node_EvaluateChooser::ChooserChanged()
{
	if (Chooser != CurrentCallbackChooser)
	{
		UnregisterChooserCallback();
	
		if (Chooser)
		{
			Chooser->OnOutputObjectTypeChanged.AddUObject(this, &UK2Node_EvaluateChooser::ResultTypeChanged);
		}
	
		CurrentCallbackChooser = Chooser;

		AllocateDefaultPins();
	}
}

void UK2Node_EvaluateChooser::ResultTypeChanged(const UClass*)
{
	AllocateDefaultPins();
}

void UK2Node_EvaluateChooser::PreloadRequiredAssets()
{
	if (Chooser)
	{
		if (FLinkerLoad* ObjLinker = GetLinker())
		{
			ObjLinker->Preload(Chooser);
		}
	}
    		
	Super::PreloadRequiredAssets();
}

void UK2Node_EvaluateChooser::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();
	
	UClass* ChooserResultType = UObject::StaticClass();

   	if (Chooser && Chooser->OutputObjectType)
   	{
   		ChooserResultType = Chooser->OutputObjectType;
   	}

	if (UEdGraphPin* ResultPin = FindPin(TEXT("Result"), EGPD_Output))
	{
		ResultPin->PinType.PinSubCategoryObject = ChooserResultType;
		ResultPin->PinType.ContainerType = (Mode == EEvaluateChooserMode::AllResults) ? EPinContainerType::Array : EPinContainerType::None;
	}
	else
	{
		UEdGraphNode::FCreatePinParams PinParams;
		PinParams.ContainerType = (Mode == EEvaluateChooserMode::AllResults) ? EPinContainerType::Array : EPinContainerType::None;
		PinParams.ValueTerminalType.TerminalCategory = UEdGraphSchema_K2::PC_Object;
				
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, ChooserResultType, TEXT("Result"), PinParams);
	}
}

FText UK2Node_EvaluateChooser::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Chooser)
	{
		return FText::FromString(Chooser->GetName());
	}
	else
	{
		return LOCTEXT("EvaluateChooser_Title", "Evaluate Chooser");
	}
}

FText UK2Node_EvaluateChooser::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	return FText::FromName(Pin->PinName);
}

void UK2Node_EvaluateChooser::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetName() == "Chooser")
	{
		ChooserChanged();
	}
	if (PropertyChangedEvent.Property->GetName() == "Mode")
	{
		AllocateDefaultPins();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UK2Node_EvaluateChooser::PostLoad()
{
	Super::PostLoad();
	ChooserChanged();
}

void UK2Node_EvaluateChooser::PinConnectionListChanged(UEdGraphPin* Pin)
{

	Modify();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
}

void UK2Node_EvaluateChooser::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
}

void UK2Node_EvaluateChooser::PinTypeChanged(UEdGraphPin* Pin)
{
	Super::PinTypeChanged(Pin);
}

FText UK2Node_EvaluateChooser::GetTooltipText() const
{
	return NodeTooltip;
}

void UK2Node_EvaluateChooser::PostReconstructNode()
{
	Super::PostReconstructNode();
}

void UK2Node_EvaluateChooser::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* ResultPin = FindPinChecked(TEXT("Result"));
	if (ResultPin->HasAnyConnections())
	{
		UK2Node_CallFunction* CallFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(CallFunction, this);

		if (Mode == EEvaluateChooserMode::AllResults)
		{
			CallFunction->SetFromFunction(UChooserFunctionLibrary::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UChooserFunctionLibrary, EvaluateChooserMulti)));
		}
		else
		{
			CallFunction->SetFromFunction(UChooserFunctionLibrary::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UChooserFunctionLibrary, EvaluateChooser)));
		}
		CallFunction->AllocateDefaultPins();

		UK2Node_Self* SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(this, SourceGraph);
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(SelfNode, this);
		SelfNode->AllocateDefaultPins();

		SelfNode->FindPin(TEXT("self"))->MakeLinkTo(CallFunction->FindPin(TEXT("ContextObject"))); // add null check + error
		
		UEdGraphPin* ChooserTablePin = CallFunction->FindPin(TEXT("ChooserTable"));
		CallFunction->GetSchema()->TrySetDefaultObject(*ChooserTablePin, Chooser);

		UEdGraphPin* OutputPin = CallFunction->GetReturnValuePin();

		if (Chooser->OutputObjectType)
		{
			UEdGraphPin* OutputClassPin = CallFunction->FindPin(TEXT("ObjectClass"));
			CallFunction->GetSchema()->TrySetDefaultObject(*OutputClassPin, Chooser->OutputObjectType);
		}

		CompilerContext.MovePinLinksToIntermediate(*ResultPin, *OutputPin);
	}
	
	BreakAllNodeLinks();
}

UK2Node::ERedirectType UK2Node_EvaluateChooser::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	ERedirectType RedirectType = ERedirectType_None;

	// if the pin names do match
	if (NewPin->PinName.ToString().Equals(OldPin->PinName.ToString(), ESearchCase::CaseSensitive))
	{
		// Make sure we're not dealing with a menu node
		UEdGraph* OuterGraph = GetGraph();
		if( OuterGraph && OuterGraph->Schema )
		{
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
			if( !K2Schema || K2Schema->IsSelfPin(*NewPin) || K2Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType) )
			{
				RedirectType = ERedirectType_Name;
			}
			else
			{
				RedirectType = ERedirectType_None;
			}
		}
	}
	else
	{
		// try looking for a redirect if it's a K2 node
		if (UK2Node* Node = Cast<UK2Node>(NewPin->GetOwningNode()))
		{	
			// if you don't have matching pin, now check if there is any redirect param set
			TArray<FString> OldPinNames;
			GetRedirectPinNames(*OldPin, OldPinNames);

			FName NewPinName;
			RedirectType = ShouldRedirectParam(OldPinNames, /*out*/ NewPinName, Node);

			// make sure they match
			if ((RedirectType != ERedirectType_None) && (!NewPin->PinName.ToString().Equals(NewPinName.ToString(), ESearchCase::CaseSensitive)))
			{
				RedirectType = ERedirectType_None;
			}
		}
	}

	return RedirectType;
}

bool UK2Node_EvaluateChooser::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

void UK2Node_EvaluateChooser::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
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

FText UK2Node_EvaluateChooser::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Animation);
}


#undef LOCTEXT_NAMESPACE
