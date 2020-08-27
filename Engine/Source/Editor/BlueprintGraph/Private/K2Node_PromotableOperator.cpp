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

#define LOCTEXT_NAMESPACE "PromotableOperatorNode"

///////////////////////////////////////////////////////////
// Pin names for default construction

static const FName InputPinA_Name = FName(TEXT("A"));
static const FName InputPinB_Name = FName(TEXT("B"));

///////////////////////////////////////////////////////////
// UK2Node_PromotableOperator

UK2Node_PromotableOperator::UK2Node_PromotableOperator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UpdateOpName();
	OrphanedPinSaveMode = ESaveOrphanPinMode::SaveAllButExec;
}

///////////////////////////////////////////////////////////
// UEdGraphNode interface

void UK2Node_PromotableOperator::AllocateDefaultPins()
{
	FWildcardNodeUtils::CreateWildcardPin(this, InputPinA_Name, EGPD_Input);
	FWildcardNodeUtils::CreateWildcardPin(this, InputPinB_Name, EGPD_Input);

	FWildcardNodeUtils::CreateWildcardPin(this, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
}

void UK2Node_PromotableOperator::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

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
		FString OpName;
		FTypePromotion::GetOpNameFromFunction(Function, OpName);
		return FText::Format(LOCTEXT("PromotableOperatorFunctionTooltip", "{0} Operator"), FText::FromString(OpName));
	}

	// Otherwise use the default one (a more specific function tooltip)
	return Super::GetTooltipText();
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

	TArray<UEdGraphPin*> InputPins = GetInputPins();
	UEdGraphPin* OutputPin = GetOutputPin();

	// Our operator function has been determined on pin connection change
	const UFunction* const OpFunction = GetTargetFunction();

	if (!OpFunction)
	{
		UE_LOG(LogBlueprint, Error, TEXT("Could not find matching op function during expansion!"));
		CompilerContext.MessageLog.Error(TEXT("Could not find matching op function during expansion on '@@'!"), this);
		return;
	}

	// Now to actually go through the promotion process on pins that need to be promoted to 
	// fit our function signature! 

	// Spawn an intermediate K2NodeCommunicative op node of that type
	UK2Node_CallFunction* NewOperator = SourceGraph->CreateIntermediateNode<UK2Node_CallFunction>();
	NewOperator->SetFromFunction(OpFunction);
	NewOperator->AllocateDefaultPins();

	// Move this node next to the thing it was linked to
	NewOperator->NodePosY = this->NodePosY;
	NewOperator->NodePosX = this->NodePosX + 8;

	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(NewOperator, this);

	UEdGraphPin* NewOperatorInputA = nullptr;
	UEdGraphPin* NewOperatorInputB = nullptr;
	UEdGraphPin* NewOperatorOutputPin = nullptr;
	UEdGraphPin* NewOperatorSelfPin = NewOperator->FindPin(UEdGraphSchema_K2::PN_Self);

	for (UEdGraphPin* Pin : NewOperator->Pins)
	{
		if (Pin == NewOperatorSelfPin)
		{
			continue;
		}

		if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			if (!NewOperatorInputA) 
			{
				NewOperatorInputA = Pin;
			}
			else if (!NewOperatorInputB) 
			{
				NewOperatorInputB = Pin;
			}
		}
		else if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
		{
			NewOperatorOutputPin = Pin;
		}
	}

	check(NewOperatorOutputPin);

	// Create some auto casts if they are necessary
	const bool bPinASuccess = CreateIntermediateCast(CompilerContext, SourceGraph, InputPins[0], NewOperatorInputA);
	const bool bPinBSuccess = CreateIntermediateCast(CompilerContext, SourceGraph, InputPins[1], NewOperatorInputB);

	if (!bPinASuccess || !bPinBSuccess)
	{
		CompilerContext.MessageLog.Error(TEXT("'@@' could not successfuly expand pins!"), this);
	}

	// Connect intermediate node output to this nodes output
	CompilerContext.MovePinLinksToIntermediate(*GetOutputPin(), *NewOperatorOutputPin);
}

void UK2Node_PromotableOperator::NotifyPinConnectionListChanged(UEdGraphPin* ChangedPin)
{
	Super::NotifyPinConnectionListChanged(ChangedPin);

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
		ChangedPin->PinType = FWildcardNodeUtils::GetDefaultWildcardPinType();
		return;
	}

	// Gather all pins and their links so we can determine the highest type that the user could want
	TArray<UEdGraphPin*> InputPins;

	for (UEdGraphPin* Pin : Pins)
	{
		// Consider inputs, maybe the output, and ignore the changed pin if it was totally disconnected.
		if (Pin->LinkedTo.Num() > 0 || !Pin->DoesDefaultValueMatchAutogenerated())
		{
			InputPins.Add(Pin);
			for (UEdGraphPin* Link : Pin->LinkedTo)
			{
				InputPins.Emplace(Link);
			}
		}
	}

	FEdGraphPinType HighestType = FTypePromotion::GetPromotedType(InputPins);

	// if a pin was changed, update it if it cannot be promoted to this type
	FEdGraphPinType NewConnectionHighestType = ChangedPin->LinkedTo.Num() > 0 ? FTypePromotion::GetPromotedType(ChangedPin->LinkedTo) : FWildcardNodeUtils::GetDefaultWildcardPinType();

	// If there are ANY wildcards on this node, than we need to update the whole node accordingly. Otherwise we can 
	// update only the Changed and output pins.

	if (FWildcardNodeUtils::NodeHasAnyWildcards(this) || bOutputPinWasChanged || bWasAFullDisconnect ||
		FTypePromotion::GetHigherType(NewConnectionHighestType, GetOutputPin()->PinType) == FTypePromotion::ETypeComparisonResult::TypeAHigher)
	{
		const UFunction* LowestFunc = FTypePromotion::FindLowestMatchingFunc(OperationName, HighestType, PossibleConversions);

		// Store these other function options for later so that the user can convert to them later
		UpdatePinsFromFunction(LowestFunc, ChangedPin);
	}

	// If the user connected a type that was a valid promotion, than leave it as the pin type they dragged from for a better UX
	if (!bWasAFullDisconnect && NewConnectionHighestType.PinCategory != UEdGraphSchema_K2::PC_Wildcard &&
		(FTypePromotion::IsValidPromotion(NewConnectionHighestType, ChangedPin->PinType) || FTypePromotion::IsValidPromotion(ChangedPin->PinType, NewConnectionHighestType)))
	{
		ChangedPin->PinType = NewConnectionHighestType;
	}

	// Update context menu options for this node
	UpdatePossibleConversionFuncs();
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
	// Allocate default pins will have been called before this, which means we are reset to wildcard state
	// We need to Update the pins to be the proper function again
	UpdatePinsFromFunction(GetTargetFunction(), nullptr);

	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UK2Node_PromotableOperator::AutowireNewNode(UEdGraphPin* ChangedPin)
{
	Super::AutowireNewNode(ChangedPin);

	NotifyPinConnectionListChanged(ChangedPin);
}

bool UK2Node_PromotableOperator::IsActionFilteredOut(FBlueprintActionFilter const& Filter)
{
	return false;
}

///////////////////////////////////////////////////////////
// UK2Node_PromotableOperator

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

bool UK2Node_PromotableOperator::UpdateOpName()
{
	const UFunction* Func = GetTargetFunction();
	// If the function is null then return false, because we did not successfully update it. 
	// This could be possible during node reconstruction/refresh, and we don't want to set the 
	// op name to "Empty" incorrectly. 
	return Func ? FTypePromotion::GetOpNameFromFunction(GetTargetFunction(), /* out */ OperationName) : false;
}

bool UK2Node_PromotableOperator::CreateIntermediateCast(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InputPin, UEdGraphPin* OutputPin)
{
	check(InputPin && OutputPin);

	// If the pin types are the same, than no casts are needed and we can just connect
	if (InputPin->PinType == OutputPin->PinType)
	{
		return !CompilerContext.MovePinLinksToIntermediate(*InputPin, *OutputPin).IsFatal();
	}

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

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

		// Connect my input to the conversion node
		if (InputPin->LinkedTo.Num() > 0)
		{
			bInputSuccessful = Schema->TryCreateConnection(InputPin->LinkedTo[0], ConversionInput);
		}

		// Connect conversion node output to the input of the new operator
		bOutputSuccessful = Schema->TryCreateConnection(ConversionOutput, OutputPin);

		// Move this node next to the thing it was linked to
		TemplateConversionNode->NodePosY = this->NodePosY;
		TemplateConversionNode->NodePosX = this->NodePosX + 4;
	}
	else
	{
		CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("NoValidPromotion", "Cannot find appropriate promotion from '{0}' to '{1}' on '@@'"),
			Schema->TypeToText(InputPin->PinType),
			Schema->TypeToText(OutputPin->PinType)).ToString(),
			this
		);
	}

	return bInputSuccessful && bOutputSuccessful;
}

void UK2Node_PromotableOperator::ResetNodeToWildcard()
{
	RecombineAllSplitPins();

	// Reset type to wildcard
	const FEdGraphPinType& WildType = FWildcardNodeUtils::GetDefaultWildcardPinType();
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	for (UEdGraphPin* Pin : Pins)
	{
		// Ensure this pin is not a split pin
		if (Pin && Pin->ParentPin == nullptr)
		{
			Pin->PinType = WildType;
			K2Schema->ResetPinToAutogeneratedDefaultValue(Pin);
		}
	}

	// Clear out any possible function matches, since we are removing connections
	PossibleConversions.Empty();
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
	const FScopedTransaction Transaction(LOCTEXT("ConvertPromotableOpToFunction", "Change the function signiture of a promotable operator node."));
	Modify();
	RecombineAllSplitPins();
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

	TMap<FString, TSet<UEdGraphPin*>> PinConnections;
	FEdGraphUtilities::GetPinConnectionMap(this, PinConnections);

	int32 ArgCount = 0;
	for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
	{
		FProperty* Param = *PropIt;
		FEdGraphPinType ParamType;

		// If this param type is valid and we are still within range of the number of pins that we have
		if (Schema->ConvertPropertyToPinType(Param, /* out */ ParamType) && ArgCount >= 0 && ArgCount < Pins.Num())
		{
			// Get either the output pin or one of the input pins
			UEdGraphPin* PinToChange = Param->HasAnyPropertyFlags(CPF_ReturnParm) ? GetOutputPin() : Pins[ArgCount];

			check(PinToChange);

			const bool bHasConnectionOrDefault = PinToChange->LinkedTo.Num() > 0 || !PinToChange->DoesDefaultValueMatchAutogenerated();
			const bool bIsWildcard = FWildcardNodeUtils::IsWildcardPin(PinToChange);
			const bool bIsValidPromo = !bIsWildcard && FTypePromotion::IsValidPromotion(PinToChange->PinType, ParamType);
			const bool bTypesEqual = PinToChange->PinType == ParamType;
			const bool bIsOutPin = PinToChange->Direction == EGPD_Output;

			bool bIsLinkedToWildcard = false;
			if (bIsWildcard && bHasConnectionOrDefault)
			{
				for (UEdGraphPin* LinkedPin : PinToChange->LinkedTo)
				{
					if (FWildcardNodeUtils::IsWildcardPin(LinkedPin))
					{
						bIsLinkedToWildcard = true;
						break;
					}
				}
			}

			// If this is a wildcard WITH a connection, then we should just leave this pin as a wildcard and let the compiler handle it
			if (bIsLinkedToWildcard)
			{
				continue;
			}

			bool bNeedsTypeUpdate = true;

			// If this pin has a valid value already, than dont bother updating it.
			if (bHasConnectionOrDefault && (bIsValidPromo || bTypesEqual))
			{
				bNeedsTypeUpdate = false;
			}

			// We always want to update the out pin or if we have a wildcard pin (which is the case during reconstruction)
			if (!bTypesEqual && (bIsOutPin || bIsWildcard))
			{
				bNeedsTypeUpdate = true;
			}

			if (bNeedsTypeUpdate)
			{
				// If this is a wildcard pin than we have to reconsider the links to that 
				if (bIsWildcard && PinToChange->LinkedTo.Num() > 0)
				{
					FEdGraphPinType LinkedType = FTypePromotion::GetPromotedType(PinToChange->LinkedTo);
					if (FTypePromotion::IsValidPromotion(PinToChange->PinType, ParamType))
					{
						ParamType = LinkedType;
					}
				}
				// If this update is coming from a changed pin, than we need to orphan the pin that had the old type
				else if (ChangedPin && bHasConnectionOrDefault)
				{
					PinToChange->BreakAllPinLinks();
				}

				// Only change the type of this pin if it is necessary				
				PinToChange->PinType = ParamType;
			}

		}

		++ArgCount;
	}

	// Update the function reference and the FUNC_BlueprintPure/FUNC_Const appropriately
	SetFromFunction(Function);

	UpdatePossibleConversionFuncs();
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
		// The node has changed, so lets find the lowest matching function with the newly updated types
		FEdGraphPinType HighestType = FTypePromotion::GetPromotedType(GetInputPins());
		FTypePromotion::FindLowestMatchingFunc(OperationName, HighestType, PossibleConversions);
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