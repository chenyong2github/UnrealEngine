// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "K2Node_EvaluateLiveLinkFrame.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "EditorCategoryUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "LiveLinkRole.h"
#include "LiveLinkTypes.h"
#include "LiveLinkBlueprintLibrary.h"
#include "UObject/PropertyPortFlags.h"


#define LOCTEXT_NAMESPACE "K2Node_EvaluateLiveLinkFrame"


struct UK2Node_EvaluateLiveLinkFrameHelper
{
	static FName LiveLinkSubjectPinName;
	static FName LiveLinkDataResultPinName;
	static FName FrameNotAvailablePinName;
};

FName UK2Node_EvaluateLiveLinkFrameHelper::FrameNotAvailablePinName(*LOCTEXT("FrameNotAvailablePinName", "Invalid Frame").ToString());
FName UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkDataResultPinName(*LOCTEXT("LiveLinkDataResultPinName", "LiveLinkDataResult").ToString());
FName UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkSubjectPinName(*LOCTEXT("LiveLinkSubjectNamePinName", "LiveLinkSubject").ToString());

UK2Node_EvaluateLiveLinkFrame::UK2Node_EvaluateLiveLinkFrame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UK2Node_EvaluateLiveLinkFrame::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Add execution pins
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* FrameAvailablePin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	FrameAvailablePin->PinFriendlyName = LOCTEXT("EvaluateLiveLinkFrame Frame available", "Valid Frame");
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UK2Node_EvaluateLiveLinkFrameHelper::FrameNotAvailablePinName);

	// Subject pin
	UEdGraphPin* LiveLinkSubjectRepPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FLiveLinkSubjectRepresentation::StaticStruct(), UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkSubjectPinName);
	SetPinToolTip(*LiveLinkSubjectRepPin, LOCTEXT("LiveLinkSubjectNamePinDescription", "The Live Link Subject Reprensation to get a frame from"));
	

	// Output structs pins
	UEdGraphPin* DataResultPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkDataResultPinName);
	SetPinToolTip(*DataResultPin, LOCTEXT("DataResultPinDescription", "The data struct, if a frame was present for the given role"));

	Super::AllocateDefaultPins();
}

void UK2Node_EvaluateLiveLinkFrame::SetPinToolTip(UEdGraphPin& InOutMutatablePin, const FText& InPinDescription) const
{
	InOutMutatablePin.PinToolTip = UEdGraphSchema_K2::TypeToText(InOutMutatablePin.PinType).ToString();

	UEdGraphSchema_K2 const* const K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
	if (K2Schema != nullptr)
	{
		InOutMutatablePin.PinToolTip += TEXT(" ");
		InOutMutatablePin.PinToolTip += K2Schema->GetPinDisplayName(&InOutMutatablePin).ToString();
	}

	InOutMutatablePin.PinToolTip += FString(TEXT("\n")) + InPinDescription.ToString();
}

void UK2Node_EvaluateLiveLinkFrame::RefreshDataOutputPinType()
{
	UScriptStruct* DataType = GetLiveLinkRoleOutputStructType();
	SetReturnTypeForOutputStruct(DataType);
}

void UK2Node_EvaluateLiveLinkFrame::SetReturnTypeForOutputStruct(UScriptStruct* InClass)
{
	UScriptStruct* OldDataStruct = GetReturnTypeForOutputDataStruct();
	if (InClass != OldDataStruct)
	{
		UEdGraphPin* ResultPin = GetResultingDataPin();

		if (ResultPin->SubPins.Num() > 0)
		{
			GetSchema()->RecombinePin(ResultPin);
		}

		// NOTE: purposefully not disconnecting the ResultPin (even though it changed type)... we want the user to see the old
		//       connections, and incompatible connections will produce an error (plus, some super-struct connections may still be valid)
		ResultPin->PinType.PinSubCategoryObject = InClass;
		ResultPin->PinType.PinCategory = (InClass == nullptr) ? UEdGraphSchema_K2::PC_Wildcard : UEdGraphSchema_K2::PC_Struct;
	}
}

UScriptStruct* UK2Node_EvaluateLiveLinkFrame::GetReturnTypeForOutputDataStruct()
{
	UScriptStruct* ReturnStructType = (UScriptStruct*)(GetResultingDataPin()->PinType.PinSubCategoryObject.Get());

	return ReturnStructType;
}

UScriptStruct* UK2Node_EvaluateLiveLinkFrame::GetLiveLinkRoleOutputStructType() const
{
	UScriptStruct* DataStructType = nullptr;

	FLiveLinkSubjectRepresentation Representation = GetDefaultSubjectPinValue();
	if (Representation.Role != nullptr)
	{
		DataStructType = Representation.Role.GetDefaultObject()->GetBlueprintDataStruct();
	}

	//No type was deduced from the role, try to deduce it from where it's connected
	if (DataStructType == nullptr)
	{
		UEdGraphPin* ResultPin = GetResultingDataPin();
		if (ResultPin && ResultPin->LinkedTo.Num() > 0)
		{
			DataStructType = Cast<UScriptStruct>(ResultPin->LinkedTo[0]->PinType.PinSubCategoryObject.Get());
			for (int32 LinkIndex = 1; LinkIndex < ResultPin->LinkedTo.Num(); ++LinkIndex)
			{
				UEdGraphPin* Link = ResultPin->LinkedTo[LinkIndex];
				UScriptStruct* LinkType = Cast<UScriptStruct>(Link->PinType.PinSubCategoryObject.Get());

				if (DataStructType->IsChildOf(LinkType))
				{
					DataStructType = LinkType;
				}
			}
		}
	}
	return DataStructType;
}

void UK2Node_EvaluateLiveLinkFrame::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UK2Node_EvaluateLiveLinkFrame::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
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

FText UK2Node_EvaluateLiveLinkFrame::GetMenuCategory() const
{
	return FText::FromString(TEXT("LiveLink"));
}

bool UK2Node_EvaluateLiveLinkFrame::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin == GetResultingDataPin() && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		bool bDisallowed = true;
		if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (UScriptStruct* ConnectionType = Cast<UScriptStruct>(OtherPin->PinType.PinSubCategoryObject.Get()))
			{
				bDisallowed = !ConnectionType->IsChildOf(FLiveLinkBaseBlueprintData::StaticStruct());
			}
		}
		else if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			bDisallowed = false;
		}

		if (bDisallowed)
		{
			OutReason = TEXT("Must be a struct that inherits from FLiveLinkBaseBlueprintData");
		}
		return bDisallowed;
	}
	
	return false;
}

void UK2Node_EvaluateLiveLinkFrame::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (ChangedPin && ChangedPin->PinName == UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkSubjectPinName)
	{
		RefreshDataOutputPinType();
	}
}

FText UK2Node_EvaluateLiveLinkFrame::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Attempts to Get a LiveLink Frame from a subject using a given Role");
}

UEdGraphPin* UK2Node_EvaluateLiveLinkFrame::GetThenPin()const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_Then);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

UEdGraphPin* UK2Node_EvaluateLiveLinkFrame::GetLiveLinkSubjectPin(const TArray<UEdGraphPin*>* InPinsToSearch /*= nullptr*/) const
{
	const TArray<UEdGraphPin*>* PinsToSearch = InPinsToSearch ? InPinsToSearch : &Pins;

	UEdGraphPin* Pin = nullptr;
	for (UEdGraphPin* TestPin : *PinsToSearch)
	{
		if (TestPin && TestPin->PinName == UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkSubjectPinName)
		{
			Pin = TestPin;
			break;
		}
	}
	check(Pin == nullptr || Pin->Direction == EGPD_Input);
	return Pin;
}

UEdGraphPin* UK2Node_EvaluateLiveLinkFrame::GetFrameNotAvailablePin() const
{
	UEdGraphPin* Pin = FindPinChecked(UK2Node_EvaluateLiveLinkFrameHelper::FrameNotAvailablePinName);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

UEdGraphPin* UK2Node_EvaluateLiveLinkFrame::GetResultingDataPin() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked(UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkDataResultPinName);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

FText UK2Node_EvaluateLiveLinkFrame::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("ListViewTitle", "Evaluate Live Link Frame");
}

void UK2Node_EvaluateLiveLinkFrame::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* OriginalLiveLinkSubjectPin = GetLiveLinkSubjectPin();
	FLiveLinkSubjectRepresentation Representation = GetDefaultSubjectPinValue();
	if (Representation.Role == nullptr)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("EvaluateLiveLinkRoleNoRole_Error", "EvaluateLiveLinkFrame must have a Role specified.").ToString(), this);
		// we break exec links so this is the only error we get
		BreakAllNodeLinks();
		return;
	}

	// FUNCTION NODE
	const FName FunctionName = GET_FUNCTION_NAME_CHECKED(ULiveLinkBlueprintLibrary, EvaluateLiveLinkFrame);
	UK2Node_CallFunction* EvaluateLiveLinkFrameFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	EvaluateLiveLinkFrameFunction->FunctionReference.SetExternalMember(FunctionName, ULiveLinkBlueprintLibrary::StaticClass());
	EvaluateLiveLinkFrameFunction->AllocateDefaultPins();
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *(EvaluateLiveLinkFrameFunction->GetExecPin()));

	// Connect the input of our EvaluateLiveLinkFrame to the Input of our Function pin
	{
		UEdGraphPin* LiveLinkSubjectInPin = EvaluateLiveLinkFrameFunction->FindPinChecked(TEXT("SubjectRepresentation"));
		if (OriginalLiveLinkSubjectPin->LinkedTo.Num() > 0)
		{
			// Copy the connection
			CompilerContext.MovePinLinksToIntermediate(*OriginalLiveLinkSubjectPin, *LiveLinkSubjectInPin);
		}
		else
		{
			// Copy literal
			LiveLinkSubjectInPin->DefaultValue = OriginalLiveLinkSubjectPin->DefaultValue;
		}
	}

	// Get some pins to work with
	UEdGraphPin* OriginalDataOutPin = FindPinChecked(UK2Node_EvaluateLiveLinkFrameHelper::LiveLinkDataResultPinName);
	UEdGraphPin* FunctionDataOutPin = EvaluateLiveLinkFrameFunction->FindPinChecked(TEXT("OutBlueprintData"));
	UEdGraphPin* FunctionReturnPin = EvaluateLiveLinkFrameFunction->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* FunctionThenPin = EvaluateLiveLinkFrameFunction->GetThenPin();

	// Set the type of each output pins on this expanded mode to match original
	FunctionDataOutPin->PinType = OriginalDataOutPin->PinType;
	FunctionDataOutPin->PinType.PinSubCategoryObject = OriginalDataOutPin->PinType.PinSubCategoryObject;

	//BRANCH NODE
	UK2Node_IfThenElse* BranchNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	BranchNode->AllocateDefaultPins();
	// Hook up inputs to branch
	FunctionThenPin->MakeLinkTo(BranchNode->GetExecPin());
	FunctionReturnPin->MakeLinkTo(BranchNode->GetConditionPin());

	// Hook up outputs
	CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *(BranchNode->GetThenPin()));
	CompilerContext.MovePinLinksToIntermediate(*GetFrameNotAvailablePin(), *(BranchNode->GetElsePin()));
	CompilerContext.MovePinLinksToIntermediate(*OriginalDataOutPin, *FunctionDataOutPin);

	BreakAllNodeLinks();
}

FSlateIcon UK2Node_EvaluateLiveLinkFrame::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

void UK2Node_EvaluateLiveLinkFrame::PostReconstructNode()
{
	Super::PostReconstructNode();

	RefreshDataOutputPinType();
}

void UK2Node_EvaluateLiveLinkFrame::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	const UEdGraphPin* LiveLinkSubjectPin = GetLiveLinkSubjectPin();
	if (!LiveLinkSubjectPin)
	{
		MessageLog.Error(*LOCTEXT("MissingPins", "Missing pins in @@").ToString(), this);
		return;
	}

	FLiveLinkSubjectRepresentation Representation = GetDefaultSubjectPinValue();
	if (Representation.Role == nullptr || !Representation.Role->IsChildOf(ULiveLinkRole::StaticClass()))
	{
		MessageLog.Error(*LOCTEXT("NoLiveLinkRole", "No LiveLinkRole in @@").ToString(), this);
		return;
	}

	if (Representation.Subject.IsNone())
	{
		MessageLog.Warning(*LOCTEXT("NoLiveLinkSubjectName", "No subject in @@").ToString(), this);
		return;
	}
}

void UK2Node_EvaluateLiveLinkFrame::PreloadRequiredAssets()
{
	return Super::PreloadRequiredAssets();
}

void UK2Node_EvaluateLiveLinkFrame::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);

	UEdGraphPin* LiveLinkSubjectPin = GetLiveLinkSubjectPin();
	if (Pin == GetResultingDataPin())
	{
		// this connection would only change the output type if the role pin is undefined
		const bool bIsTypeAuthority = (LiveLinkSubjectPin->LinkedTo.Num() <= 0 && LiveLinkSubjectPin->DefaultObject == nullptr);
		if (bIsTypeAuthority)
		{
			RefreshDataOutputPinType();
		}
	}
	else if (Pin == LiveLinkSubjectPin)
	{
		const bool bConnectionAdded = Pin->LinkedTo.Num() > 0;
		if (bConnectionAdded)
		{
			RefreshDataOutputPinType();
		}
	}
}

FLiveLinkSubjectRepresentation UK2Node_EvaluateLiveLinkFrame::GetDefaultSubjectPinValue() const
{
	FLiveLinkSubjectRepresentation Representation;

	UEdGraphPin* LiveLinkSubjectPin = GetLiveLinkSubjectPin();
	if (LiveLinkSubjectPin)
	{
		if (LiveLinkSubjectPin->LinkedTo.Num() > 0)
		{
			if (LiveLinkSubjectPin->LinkedTo[0]->DefaultValue.Len())
			{
				FLiveLinkSubjectRepresentation::StaticStruct()->ImportText(*LiveLinkSubjectPin->LinkedTo[0]->DefaultValue, &Representation, nullptr, EPropertyPortFlags::PPF_None, nullptr, TEXT(""));
			}
		}
		else if (LiveLinkSubjectPin->DefaultValue.Len())
		{
			FLiveLinkSubjectRepresentation::StaticStruct()->ImportText(*LiveLinkSubjectPin->DefaultValue, &Representation, nullptr, EPropertyPortFlags::PPF_None, nullptr, TEXT(""));
		}
	}

	return Representation;
}


#undef LOCTEXT_NAMESPACE
