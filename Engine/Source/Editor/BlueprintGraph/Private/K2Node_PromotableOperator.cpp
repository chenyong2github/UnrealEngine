// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_PromotableOperator.h"
#include "BlueprintTypePromotion.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Framework/Commands/UIAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/WildcardNodeUtils.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "PromotableOperatorNode"

///////////////////////////////////////////////////////////
// Pin names for default construction

static const FName InputPinA_Name = FName(TEXT("A"));
static const FName InputPinB_Name = FName(TEXT("B"));
static const int32 NumFunctionInputs = 2;

///////////////////////////////////////////////////////////
// UK2Node_PromotableOperator

UK2Node_PromotableOperator::UK2Node_PromotableOperator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UpdateOpName();
	OrphanedPinSaveMode = ESaveOrphanPinMode::SaveAllButExec;
	NumAdditionalInputs = 0;
}

///////////////////////////////////////////////////////////
// UEdGraphNode interface

void UK2Node_PromotableOperator::AllocateDefaultPins()
{
	FWildcardNodeUtils::CreateWildcardPin(this, InputPinA_Name, EGPD_Input);
	FWildcardNodeUtils::CreateWildcardPin(this, InputPinB_Name, EGPD_Input);

	UEdGraphPin* OutPin = FWildcardNodeUtils::CreateWildcardPin(this, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);

	// For comparison functions we always want a bool output, so make it visually so
	if (FTypePromotion::IsComparisonFunc(GetTargetFunction()))
	{
		OutPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}

	// Create any additional input pin. Their appropriate type is determined in ReallocatePinsDuringReconstruction
	// because we cannot get a promoted type with no links to the pin.
	for (int32 i = NumFunctionInputs; i < (NumAdditionalInputs + NumFunctionInputs); ++i)
	{
		AddInputPinImpl(i);
	}
}

void UK2Node_PromotableOperator::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	static const FName PromotableOperatorNodeName = FName("PromotableOperator");
	static const FText PromotableOperatorStr = LOCTEXT("PromotableOperatorNode", "Operator Node");

	// Add the option to remove a pin via the context menu
	if (CanRemovePin(Context->Pin))
	{
		FToolMenuSection& Section = Menu->AddSection(PromotableOperatorNodeName, PromotableOperatorStr);
		Section.AddMenuEntry(
			"RemovePin",
			LOCTEXT("RemovePin", "Remove pin"),
			LOCTEXT("RemovePinTooltip", "Remove this input pin"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateUObject(const_cast<UK2Node_PromotableOperator*>(this), &UK2Node_PromotableOperator::RemoveInputPin, const_cast<UEdGraphPin*>(Context->Pin))
			)
		);
	}
	else if (CanAddPin())
	{
		FToolMenuSection& Section = Menu->AddSection(PromotableOperatorNodeName, PromotableOperatorStr);
		Section.AddMenuEntry(
			"AddPin",
			LOCTEXT("AddPin", "Add pin"),
			LOCTEXT("AddPinTooltip", "Add another input pin"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateUObject(const_cast<UK2Node_PromotableOperator*>(this), &UK2Node_PromotableOperator::AddInputPin)
			)
		);
	}

	// If there are possible function conversions that can happen 
	if (Context->Pin && PossibleConversions.Num() > 0 && !Context->bIsDebugging && HasAnyConnectionsOrDefaults())
	{
		FToolMenuSection& Section = Menu->AddSection("K2NodePromotableOperator", LOCTEXT("ConvFunctionHeader", "Convert Function"));
		const UFunction* CurFunction = GetTargetFunction();

		for (const UFunction* Func : PossibleConversions)
		{
			// Don't need to convert to a function if we are already set to it
			if (Func == CurFunction)
			{
				continue;
			}

			FFormatNamedArguments Args;
			Args.Add(TEXT("TargetName"), GetUserFacingFunctionName(Func));
			FText ConversionName = FText::Format(LOCTEXT("CallFunction_Tooltip", "Convert node to function '{TargetName}'"), Args);

			const FText Tooltip = FText::FromString(GetDefaultTooltipForFunction(Func));

			Section.AddMenuEntry(
				Func->GetFName(),
				ConversionName,
				Tooltip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateUObject(const_cast<UK2Node_PromotableOperator*>(this), &UK2Node_PromotableOperator::ConvertNodeToFunction, Func, const_cast<UEdGraphPin*>(Context->Pin))
				)
			);
		}
	}
}

FText UK2Node_PromotableOperator::GetTooltipText() const
{
	// If there are no connections then just display the op name
	if (!HasAnyConnectionsOrDefaults())
	{
		UFunction* Function = GetTargetFunction();

		FName OpName = FTypePromotion::GetOpNameFromFunction(Function);
		return FText::Format(LOCTEXT("PromotableOperatorFunctionTooltip", "{0} Operator"), FText::FromName(OpName));
	}

	// Otherwise use the default one (a more specific function tooltip)
	return Super::GetTooltipText();
}

void UK2Node_PromotableOperator::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (bDefaultValueReentranceGuard)
	{
		return;
	}

	// Re-entrance Guard just in case this function gets called from any notify triggers in the schema
	// to prevent possible recursive calls from ResetPinToAutogeneratedDefaultValue when breaking
	// all links to this node
	TGuardValue<bool> ReentranceGuard(bDefaultValueReentranceGuard, true);

	// If this default value resets to the default one on the pin, and there are no other
	// connections or default values, then we should just reset the whole node to a wildcard
	if (!HasAnyConnectionsOrDefaults())
	{
		ResetNodeToWildcard();
	}
}

void UK2Node_PromotableOperator::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();

	// This will handle the case of dragging off of this node, and connecting to a node via typing
	// in the context menu. Without updating in this case, our pins would be left as wildcards!
	if(HasAnyConnectionsOrDefaults())
	{
		UpdateOpName();
		UpdateFromBestMatchingFunction();

		// Get correct default value boxes
		GetGraph()->NotifyGraphChanged();
	}
}

///////////////////////////////////////////////////////////
// UK2Node interface

void UK2Node_PromotableOperator::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const bool bValidOpName = UpdateOpName();
	if (!bValidOpName)
	{
		UE_LOG(LogBlueprint, Error, TEXT("Could not find matching operation name for this function!"));
		CompilerContext.MessageLog.Error(TEXT("Could not find matching operation on '@@'!"), this);
		return;
	}

	UEdGraphPin* OriginalOutputPin = GetOutputPin();
	TArray<UEdGraphPin*> OriginalInputPins = GetInputPins();

	// Our operator function has been determined on pin connection change
	UFunction* OpFunction = GetTargetFunction();

	if (!OpFunction)
	{
		UE_LOG(LogBlueprint, Error, TEXT("Could not find matching op function during expansion!"));
		CompilerContext.MessageLog.Error(TEXT("Could not find matching op function during expansion on '@@'!"), this);
		return;
	}
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	/** Helper struct to gather the necessary pins we need to create redirections */
	struct FIntermediateCastPinHelper
	{
		UEdGraphPin* InputA = nullptr;
		UEdGraphPin* InputB = nullptr;
		UEdGraphPin* OutputPin = nullptr;
		UEdGraphPin* SelfPin = nullptr;

		explicit FIntermediateCastPinHelper(UK2Node_CallFunction* NewOperator)
		{
			check(NewOperator);
			SelfPin = NewOperator->FindPin(UEdGraphSchema_K2::PN_Self);

			// Find inputs and outputs
			for (UEdGraphPin* Pin : NewOperator->Pins)
			{
				if (Pin == SelfPin)
				{
					continue;
				}

				if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
				{
					if (!InputA)
					{
						InputA = Pin;
					}
					else if (!InputB)
					{
						InputB = Pin;
					}
				}
				else if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
				{
					OutputPin = Pin;
				}
			}
		}

		~FIntermediateCastPinHelper() = default;
	};

	UK2Node_CallFunction* PrevIntermediateNode = nullptr;
	UEdGraphPin* PrevOutputPin = nullptr;

	// Create cast from original 2 inputs to the first intermediate node
	{
		UFunction* BestFunc = OpFunction;
		{
			TArray<UEdGraphPin*> PinsToConsider =
			{
				OriginalInputPins[0],
				OriginalInputPins[1],
				OriginalOutputPin
			};

			if (UFunction* Func = FTypePromotion::FindBestMatchingFunc(OperationName, PinsToConsider))
			{
				BestFunc = Func;
			}
		}

		PrevIntermediateNode = CreateIntermediateNode(this, BestFunc, CompilerContext, SourceGraph);
		FIntermediateCastPinHelper NewOpHelper(PrevIntermediateNode);
		PrevOutputPin = PrevIntermediateNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);

		const bool bPinASuccess = UK2Node_PromotableOperator::CreateIntermediateCast(this, CompilerContext, SourceGraph, OriginalInputPins[0], NewOpHelper.InputA);
		const bool bPinBSuccess = UK2Node_PromotableOperator::CreateIntermediateCast(this, CompilerContext, SourceGraph, OriginalInputPins[1], NewOpHelper.InputB);

		if (!bPinASuccess || !bPinBSuccess)
		{
			CompilerContext.MessageLog.Error(TEXT("'@@' could not successfuly expand pins!"), PrevIntermediateNode);
		}
	}

	// Loop through all the additional inputs, create a new node of this function and connecting inputs as necessary 
	for (int32 i = NumFunctionInputs; i < NumAdditionalInputs + NumFunctionInputs; ++i)
	{
		check(i > 0 && i < OriginalInputPins.Num());
		FIntermediateCastPinHelper PrevNodeHelper(PrevIntermediateNode);

		// Find the best matching function that this intermediate node should use
		// so that we can avoid unnecessary conversion nodes and casts
		UFunction* BestMatchingFunc = OpFunction;
		{
			TArray<UEdGraphPin*> PinsToConsider =
			{
				PrevNodeHelper.OutputPin,
				OriginalInputPins[i],
				OriginalOutputPin
			};

			if (UFunction* Func = FTypePromotion::FindBestMatchingFunc(OperationName, PinsToConsider))
			{
				BestMatchingFunc = Func;
			}
		}

		UK2Node_CallFunction* NewIntermediateNode = CreateIntermediateNode(PrevIntermediateNode, BestMatchingFunc, CompilerContext, SourceGraph);
		FIntermediateCastPinHelper NewOpHelper(NewIntermediateNode);

		// Connect the output pin of the previous intermediate node, to the input of the new one
		const bool bPinASuccess = CreateIntermediateCast(PrevIntermediateNode, CompilerContext, SourceGraph, NewOpHelper.InputA, PrevOutputPin);

		// Connect the original node's pin to the newly created intermediate node's B Pin
		const bool bPinBSuccess = CreateIntermediateCast(this, CompilerContext, SourceGraph, OriginalInputPins[i], NewOpHelper.InputB);

		if (!bPinASuccess || !bPinBSuccess)
		{
			CompilerContext.MessageLog.Error(TEXT("'@@' could not successfuly expand additional pins!"), PrevIntermediateNode);
		}

		// Track what the previous node is so that we can connect it's output appropriately
		PrevOutputPin = NewOpHelper.OutputPin;
		PrevIntermediateNode = NewIntermediateNode;
	}

	// Make the final output connection that we need
	if (OriginalOutputPin && PrevOutputPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*OriginalOutputPin, *PrevOutputPin);
	}
}

void UK2Node_PromotableOperator::NotifyPinConnectionListChanged(UEdGraphPin* ChangedPin)
{
	Super::NotifyPinConnectionListChanged(ChangedPin);

	EvaluatePinsFromChange(ChangedPin);
}

void UK2Node_PromotableOperator::PostReconstructNode()
{
	Super::PostReconstructNode();

	// We only need to set the function if we have connections, otherwise we should stick in a wildcard state
	if (HasAnyConnectionsOrDefaults())
	{
		// Allocate default pins will have been called before this, which means we are reset to wildcard state
		// We need to Update the pins to be the proper function again
		UpdatePinsFromFunction(GetTargetFunction());

		for (UEdGraphPin* AddPin : Pins)
		{
			if (IsAdditionalPin(AddPin) && AddPin->LinkedTo.Num() > 0)
			{
				FEdGraphPinType TypeToSet = FTypePromotion::GetPromotedType(AddPin->LinkedTo);
				AddPin->PinType = TypeToSet;
			}
		}
	}
}

bool UK2Node_PromotableOperator::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	check(MyPin && OtherPin);

	// #TODO_BH Just disallow containers and references for now
	if (OtherPin->PinType.IsContainer() || OtherPin->PinType.bIsReference)
	{
		OutReason = LOCTEXT("NoExecPinsAllowed", "Promotable Operator nodes cannot have containers or references.").ToString();
		return true;
	}
	else if (MyPin == GetOutputPin() && FTypePromotion::IsComparisonFunc(GetTargetFunction()) && OtherPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean)
	{
		OutReason = LOCTEXT("ComparisonNeedsBool", "Comparison operators must return a bool!").ToString();
		return true;
	}

	const bool bHasStructPin = MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct || OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct;

	// If the other pin can be promoted to my pin type, than allow the connection
	if (FTypePromotion::IsValidPromotion(OtherPin->PinType, MyPin->PinType))
	{
		if (bHasStructPin)
		{
			// Compare the directions
			const UEdGraphPin* InputPin = nullptr;
			const UEdGraphPin* OutputPin = nullptr;
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

			if (!K2Schema->CategorizePinsByDirection(MyPin, OtherPin, /*out*/ InputPin, /*out*/ OutputPin))
			{
				OutReason = LOCTEXT("DirectionsIncompatible", "Pin directions are not compatible!").ToString();
				return true;
			}

			if (!FTypePromotion::HasStructConversion(InputPin, OutputPin))
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("MyPinType"), K2Schema->TypeToText(MyPin->PinType));
				Args.Add(TEXT("OtherPinType"), K2Schema->TypeToText(OtherPin->PinType));

				OutReason = FText::Format(LOCTEXT("NoCompatibleStructConv", "No compatible operator functions between '{MyPinType}' and '{OtherPinType}'"), Args).ToString();
				return true;
			}
		}
		return false;
	}

	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

void UK2Node_PromotableOperator::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);

	// We need to fix up any additional pins that may have been created as a wildcard pin
	int32 AdditionalPinsFixed = 0;

	// Additional Pin creation here? Check for orphan pins here and see if we can re-create them
	for (UEdGraphPin* OldPin : OldPins)
	{
		if (IsAdditionalPin(OldPin))
		{
			if (UEdGraphPin* AddPin = GetAdditionalPin(AdditionalPinsFixed + NumFunctionInputs))
			{
				AddPin->PinType = OldPin->PinType;
				AddPin->DefaultValue = OldPin->DefaultValue;
				++AdditionalPinsFixed;
			}
		}
	}
}

void UK2Node_PromotableOperator::AutowireNewNode(UEdGraphPin* ChangedPin)
{
	Super::AutowireNewNode(ChangedPin);

	EvaluatePinsFromChange(ChangedPin);
}

///////////////////////////////////////////////////////////
// IK2Node_AddPinInterface

void UK2Node_PromotableOperator::AddInputPin()
{
	if (CanAddPin())
	{
		FScopedTransaction Transaction(LOCTEXT("AddPinPromotableOperator", "AddPin"));
		Modify();

		AddInputPinImpl(NumFunctionInputs + NumAdditionalInputs);
		++NumAdditionalInputs;

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	}
}

bool UK2Node_PromotableOperator::CanAddPin() const
{
	return ((NumAdditionalInputs + NumFunctionInputs) < GetMaxInputPinsNum()) &&
		!FTypePromotion::IsComparisonFunc(GetTargetFunction());
}

bool UK2Node_PromotableOperator::CanRemovePin(const UEdGraphPin* Pin) const
{
	if (!Pin)
	{
		return false;
	}

	// We cannot remove the first two inputs from a function, because they are always there
	const bool bIsBasePin = (Pin->PinName == InputPinA_Name || Pin->PinName == InputPinB_Name);

	return (
		!bIsBasePin &&
		Pin->ParentPin == nullptr &&
		NumAdditionalInputs > 0 &&
		INDEX_NONE != Pins.IndexOfByKey(Pin) &&
		Pin->Direction == EEdGraphPinDirection::EGPD_Input
		);
}

void UK2Node_PromotableOperator::RemoveInputPin(UEdGraphPin* Pin)
{
	if (CanRemovePin(Pin))
	{
		FScopedTransaction Transaction(LOCTEXT("RemovePinPromotableOperator", "RemovePin"));
		Modify();

		if (RemovePin(Pin))
		{
			--NumAdditionalInputs;

			int32 NameIndex = 0;
			const UEdGraphPin* OutPin = GetOutputPin();
			const UEdGraphPin* SelfPin = FindPin(UEdGraphSchema_K2::PN_Self);

			for (int32 PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
			{
				UEdGraphPin* LocalPin = Pins[PinIndex];
				if (LocalPin && (LocalPin != OutPin) && (LocalPin != SelfPin))
				{
					const FName PinName = GetNameForAdditionalPin(NameIndex);
					if (PinName != LocalPin->PinName)
					{
						LocalPin->Modify();
						LocalPin->PinName = PinName;
					}
					NameIndex++;
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
		}
	}
}

UEdGraphPin* UK2Node_PromotableOperator::GetAdditionalPin(int32 PinIndex) const
{
	const FName PinToFind = GetNameForAdditionalPin(PinIndex);

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->PinName == PinToFind)
		{
			return Pin;
		}
	}

	return nullptr;
}

///////////////////////////////////////////////////////////
// UK2Node_PromotableOperator

UEdGraphPin* UK2Node_PromotableOperator::AddInputPinImpl(int32 PinIndex)
{
	const FName NewPinName = GetNameForAdditionalPin(PinIndex);

	UEdGraphPin* NewPin = FWildcardNodeUtils::CreateWildcardPin(this, NewPinName, EGPD_Input);
	check(NewPin);

	// Determine a default type for this pin if we have other input connections
	const TArray<UEdGraphPin*> InputPins = GetInputPins(/* bIncludeLinks = */ true);
	check(InputPins.Num());
	FEdGraphPinType PromotedType = FTypePromotion::GetPromotedType(InputPins);

	NewPin->PinType = PromotedType;

	return NewPin;
}

bool UK2Node_PromotableOperator::IsAdditionalPin(const UEdGraphPin* Pin) const
{
	// Quickly check if this input pin is one of the two default input pins
	return Pin && Pin->Direction == EGPD_Input && Pin->PinName != InputPinA_Name && Pin->PinName != InputPinB_Name;
}

bool UK2Node_PromotableOperator::HasAnyConnectionsOrDefaults() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->LinkedTo.Num() > 0 || !Pin->DoesDefaultValueMatchAutogenerated())
		{
			return true;
		}
	}
	return false;
}

void UK2Node_PromotableOperator::EvaluatePinsFromChange(UEdGraphPin* ChangedPin)
{
	UpdateOpName();

	const bool bOutputPinWasChanged = (ChangedPin == GetOutputPin());

	// True if the pin that has changed now has zero connections
	const bool bWasAFullDisconnect = (ChangedPin->LinkedTo.Num() == 0);

	// If we have been totally disconnected and don't have any non-default inputs, 
	// than we just reset the node to be a regular wildcard
	if (bWasAFullDisconnect && !HasAnyConnectionsOrDefaults())
	{
		ResetNodeToWildcard();
		return;
	}
	// If the pin that was connected is linked to a wildcard pin, then we should make it a wildcard
	// and do nothing else.
	else if (ChangedPin->GetOwningNode() == this && FWildcardNodeUtils::IsLinkedToWildcard(ChangedPin))
	{
		return;
	}

	// Gather all pins and their links so we can determine the highest type that the user could want
	TArray<UEdGraphPin*> PinsToConsider;

	for (UEdGraphPin* Pin : Pins)
	{
		// If this pin has links, then use those instead of the actual pin because we could be process of changing it
		// which means that it would still have it's old pin type, and could be inaccurate
		if (Pin->LinkedTo.Num() > 0)
		{
			for (UEdGraphPin* Link : Pin->LinkedTo)
			{
				PinsToConsider.Emplace(Link);
			}
		}
		else if (!Pin->DoesDefaultValueMatchAutogenerated() && !FWildcardNodeUtils::IsWildcardPin(Pin))
		{
			// If this pin has doesn't have any links then we should account for the pin itself
			PinsToConsider.Add(Pin);
		}
	}

	FEdGraphPinType HighestType = FTypePromotion::GetPromotedType(PinsToConsider);

	// if a pin was changed, update it if it cannot be promoted to this type
	FEdGraphPinType NewConnectionHighestType = ChangedPin->LinkedTo.Num() > 0 ? FTypePromotion::GetPromotedType(ChangedPin->LinkedTo) : FWildcardNodeUtils::GetDefaultWildcardPinType();

	const UFunction* BestMatchingFunc = FTypePromotion::FindBestMatchingFunc(OperationName, PinsToConsider);

	// Store these other function options for later so that the user can convert to them later
	UpdatePinsFromFunction(BestMatchingFunc, ChangedPin);
}

bool UK2Node_PromotableOperator::UpdateOpName()
{

	// If the function is null then return false, because we did not successfully update it. 
	// This could be possible during node reconstruction/refresh, and we don't want to set the 
	// op name to "Empty" incorrectly. 
	if (const UFunction* Func = GetTargetFunction())
	{
		OperationName = FTypePromotion::GetOpNameFromFunction(Func);
		return true;
	}
	return false;
}

UK2Node_CallFunction* UK2Node_PromotableOperator::CreateIntermediateNode(UK2Node_CallFunction* PreviousNode, const UFunction* const OpFunction, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	// Spawn an intermediate UK2Node_CallFunction node of the function type we need
	UK2Node_CallFunction* NewOperator = SourceGraph->CreateIntermediateNode<UK2Node_CallFunction>();
	NewOperator->SetFromFunction(OpFunction);
	NewOperator->AllocateDefaultPins();

	// Move this node next to the thing it was linked to
	NewOperator->NodePosY = PreviousNode->NodePosY + 50;
	NewOperator->NodePosX = PreviousNode->NodePosX + 8;

	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(NewOperator, this);

	return NewOperator;
}

bool UK2Node_PromotableOperator::CreateIntermediateCast(UK2Node_CallFunction* SourceNode, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InputPin, UEdGraphPin* OutputPin)
{
	check(InputPin && OutputPin);
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// If the pin types are the same, than no casts are needed and we can just connect
	if (InputPin->PinType == OutputPin->PinType)
	{
		// If SourceNode is 'this' then we need to move the pin links instead of just 
		// creating the connection because the output is not another new node, but 
		// just the intermediate expansion node.
		if (SourceNode == this)
		{
			return !CompilerContext.MovePinLinksToIntermediate(*InputPin, *OutputPin).IsFatal();
		}
		else
		{
			return Schema->TryCreateConnection(InputPin, OutputPin);
		}
	}

	UK2Node* TemplateConversionNode = nullptr;
	FName TargetFunctionName;
	UClass* ClassContainingConversionFunction = nullptr;
	TSubclassOf<UK2Node> ConversionNodeClass;

	if (Schema->SearchForAutocastFunction(InputPin->PinType, OutputPin->PinType, /*out*/ TargetFunctionName, /*out*/ClassContainingConversionFunction))
	{
		// Create a new call function node for the casting operator
		UK2Node_CallFunction* TemplateNode = SourceGraph->CreateIntermediateNode<UK2Node_CallFunction>();
		TemplateNode->FunctionReference.SetExternalMember(TargetFunctionName, ClassContainingConversionFunction);
		TemplateConversionNode = TemplateNode;
		TemplateNode->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(TemplateNode, this);
	}
	else
	{
		Schema->FindSpecializedConversionNode(InputPin, OutputPin, true, /*out*/ TemplateConversionNode);
	}

	bool bInputSuccessful = false;
	bool bOutputSuccessful = false;

	if (TemplateConversionNode)
	{
		UEdGraphPin* ConversionInput = nullptr;

		for (UEdGraphPin* ConvPin : TemplateConversionNode->Pins)
		{
			if (ConvPin && ConvPin->Direction == EGPD_Input && ConvPin->PinName != UEdGraphSchema_K2::PSC_Self)
			{
				ConversionInput = ConvPin;
				break;
			}
		}
		UEdGraphPin* ConversionOutput = TemplateConversionNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);

		// Connect my input to the conversion node directly if we have links, otherwise we need to move the intermediate version of it
		if (InputPin->LinkedTo.Num() > 0)
		{
			bInputSuccessful = Schema->TryCreateConnection(InputPin->LinkedTo[0], ConversionInput);
		}
		else if (InputPin && ConversionInput)
		{
			bInputSuccessful = !CompilerContext.MovePinLinksToIntermediate(*InputPin, *ConversionInput).IsFatal();
		}

		// Connect conversion node output to the input of the new operator
		bOutputSuccessful = Schema->TryCreateConnection(ConversionOutput, OutputPin);

		// Move this node next to the thing it was linked to
		TemplateConversionNode->NodePosY = SourceNode->NodePosY;
		TemplateConversionNode->NodePosX = SourceNode->NodePosX + 4;
	}
	else
	{
		CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("NoValidPromotion", "Cannot find appropriate promotion from '{0}' to '{1}' on '@@'"),
			Schema->TypeToText(InputPin->PinType),
			Schema->TypeToText(OutputPin->PinType)).ToString(),
			SourceNode
		);
	}

	return bInputSuccessful && bOutputSuccessful;
}

void UK2Node_PromotableOperator::ResetNodeToWildcard()
{
	RecombineAllSplitPins();

	// Reset type to wildcard
	const FEdGraphPinType& WildType = FWildcardNodeUtils::GetDefaultWildcardPinType();
	const UEdGraphSchema* Schema = GetSchema();

	for (UEdGraphPin* Pin : Pins)
	{
		// Ensure this pin is not a split pin
		if (Pin && Pin->ParentPin == nullptr)
		{
			Pin->PinType = WildType;
			Schema->ResetPinToAutogeneratedDefaultValue(Pin);
		}
	}

	// Set output pins to have a bool output flag by default
	{
		UEdGraphPin* OutPin = GetOutputPin();
		const UFunction* Func = GetTargetFunction();

		if (OutPin && Func && FTypePromotion::IsComparisonFunc(Func))
		{
			OutPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
	}

	// Clear out any possible function matches, since we are removing connections
	PossibleConversions.Empty();
	GetGraph()->NotifyGraphChanged();
}

void UK2Node_PromotableOperator::RecombineAllSplitPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Gather what pins need to be recombined from a split pin	
	for (int32 Index = 0; Index < Pins.Num(); ++Index)
	{
		if (Pins[Index] && Pins[Index]->SubPins.Num() > 0)
		{
			K2Schema->RecombinePin(Pins[Index]);
		}
	}
}

void UK2Node_PromotableOperator::UpdateFromBestMatchingFunction()
{
	// Gather all pins and their links so we can determine the highest type that the user could want
	TArray<UEdGraphPin*> PinsToConsider;

	for (UEdGraphPin* Pin : Pins)
	{
		// If this pin has links, then use those instead of the actual pin because we could be process of changing it
		// which means that it would still have it's old pin type, and could be inaccurate
		if (Pin->LinkedTo.Num() > 0)
		{
			for (UEdGraphPin* Link : Pin->LinkedTo)
			{
				PinsToConsider.Emplace(Link);
			}
		}
		else if (!Pin->DoesDefaultValueMatchAutogenerated() && !FWildcardNodeUtils::IsWildcardPin(Pin))
		{
			// If this pin has doesn't have any links then we should account for the pin itself
			PinsToConsider.Add(Pin);
		}
	}
	const UFunction* BestMatchingFunc = FTypePromotion::FindBestMatchingFunc(OperationName, PinsToConsider);

	// We need to update the pins from our function if have a new connection
	UpdatePinsFromFunction(BestMatchingFunc);
}

TArray<UEdGraphPin*> UK2Node_PromotableOperator::GetInputPins(bool bIncludeLinks /** = false */) const
{
	TArray<UEdGraphPin*> InputPins;
	for (UEdGraphPin* Pin : Pins)
	{
		// Exclude split pins from this
		if (Pin && Pin->Direction == EEdGraphPinDirection::EGPD_Input && Pin->ParentPin == nullptr)
		{
			InputPins.Add(Pin);
			if (bIncludeLinks)
			{
				for (UEdGraphPin* Link : Pin->LinkedTo)
				{
					InputPins.Emplace(Link);
				}
			}
		}
	}
	return InputPins;
}

void UK2Node_PromotableOperator::ConvertNodeToFunction(const UFunction* Function, UEdGraphPin* ChangedPin)
{
	const FScopedTransaction Transaction(LOCTEXT("ConvertPromotableOpToFunction", "Change the function signature of a promotable operator node."));
	Modify();
	RecombineAllSplitPins();

	// If we convert the node to a function, then just get rid of the additional pins
	NumAdditionalInputs = 0;

	UpdatePinsFromFunction(Function, ChangedPin);

	// Reconstruct this node to fix any default values that may be invalid now
	ReconstructNode();
}

void UK2Node_PromotableOperator::UpdatePinsFromFunction(const UFunction* Function, UEdGraphPin* ChangedPin /* = nullptr */)
{
	if (!Function)
	{
		UE_LOG(LogBlueprint, Warning, TEXT("UK2Node_PromotableOperator could not update pins, function was null!"));
		return;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	// Gather the pin types of the properties on the function we want to convert to
	FEdGraphPinType FunctionReturnType;
	FEdGraphPinType HighestFuncInputType;
	TArray<FEdGraphPinType> FunctionInputTypes;
	{
		for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
		{
			FProperty* Param = *PropIt;
			FEdGraphPinType ParamType;

			if (Schema->ConvertPropertyToPinType(Param, /* out */ ParamType))
			{
				if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					FunctionReturnType = ParamType;
				}
				else
				{
					// Track the highest input pin type that we have
					if (FTypePromotion::GetHigherType(HighestFuncInputType, ParamType) == FTypePromotion::ETypeComparisonResult::TypeBHigher)
					{
						HighestFuncInputType = ParamType;
					}
					FunctionInputTypes.Add(ParamType);
				}
			}
		}
	}

	auto ConformPinLamda = [&ChangedPin](const FEdGraphPinType& FunctionPinType, UEdGraphPin* NodePin)
	{
		// If the pin types are already equal, then we don't have to do any work
		// If this is linked to wildcard pins, then we can just ignore it and handle it on expansion
		if (!NodePin || FWildcardNodeUtils::IsLinkedToWildcard(NodePin))
		{
			return;
		}

		// By default, conform to the type of the function param
		FEdGraphPinType ConformingType = FunctionPinType;
		const FEdGraphPinType HighestLinkedType = NodePin->LinkedTo.Num() > 0 ? FTypePromotion::GetPromotedType(NodePin->LinkedTo) : NodePin->PinType;

		// If the highest type is the same as the function type, just continue on with life
		if (NodePin->LinkedTo.Num() > 0 && HighestLinkedType.PinCategory != FunctionPinType.PinCategory)
		{
			// If the links cannot be promoted to the function type, then we need to break them
			// We don't want to break the pin if it is the one that the user has dragged on to though,
			// because that would result in the node breaking connection as soon as the user lets go
			if ((!FTypePromotion::IsValidPromotion(HighestLinkedType, FunctionPinType) || NodePin->Direction == EGPD_Output) && (NodePin != ChangedPin))
			{
				NodePin->BreakAllPinLinks();
			}
			else
			{
				ConformingType = HighestLinkedType;
			}
		}

		// Conform the pin type appropriately
		NodePin->PinType = ConformingType;
	};

	int32 CurPinIndex = 0;
	for (UEdGraphPin* CurPin : Pins)
	{
		// We don't want to try and conform split pin, because we will already have conformed the parent pin
		if (CurPin->ParentPin != nullptr)
		{
			continue;
		}

		if (IsAdditionalPin(CurPin))
		{
			// Conform to the highest input pin on the function
			ConformPinLamda(HighestFuncInputType, CurPin);
		}
		else if (CurPin->Direction == EGPD_Output)
		{
			// Match to the output pin
			ConformPinLamda(FunctionReturnType, CurPin);
		}
		else
		{
			// Match to the appropriate function input type
			ConformPinLamda(FunctionInputTypes[CurPinIndex], CurPin);
			++CurPinIndex;
		}
	}

	// Update the function reference and the FUNC_BlueprintPure/FUNC_Const appropriately
	SetFromFunction(Function);

	// Invalidate the tooltips
	CachedTooltip.MarkDirty();

	// We need to notify the graph that the node has changed to get 
	// the correct default value text boxes on the node
	GetGraph()->NotifyGraphChanged();
}

void UK2Node_PromotableOperator::UpdatePossibleConversionFuncs()
{
	// Pins can be empty if we are doing this during reconstruction
	if (Pins.Num() <= 0)
	{
		return;
	}

	bool bAllPinTypesEqual = true;
	FEdGraphPinType CurType = Pins[0]->PinType;

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->PinType != CurType)
		{
			bAllPinTypesEqual = false;
			break;
		}
	}

	UpdateOpName();

	// We don't want to have a menu that is full of every possible function for an operator, that is way 
	// to overwhelming to the user. Instead, only display conversion functions that when types are not 
	// all the same. 
	if (!bAllPinTypesEqual)
	{
		// #TODO_BH We need up change how these conversions work to be on a per-pin basis instead of via a function
		// which will be a more consistent experience, and work better with additional pins
		// The node has changed, so lets find the lowest matching function with the newly updated types
		FEdGraphPinType HighestType = FTypePromotion::GetPromotedType(GetInputPins());
	}
}

UEdGraphPin* UK2Node_PromotableOperator::GetOutputPin() const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			return Pin;
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE	// "PromotableOperatorNode"