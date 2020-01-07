// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Graph/ControlRigGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "BlueprintNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "ControlRig.h"
#include "Textures/SlateIcon.h"
#include "Units/RigUnit.h"
#include "ControlRigBlueprint.h"
#include "PropertyPathHelpers.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "ScopedTransaction.h"
#include "StructReference.h"
#include "UObject/PropertyPortFlags.h"
#include "ControlRigBlueprintUtils.h"
#include "Curves/CurveFloat.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigGraphNode"

UControlRigGraphNode::UControlRigGraphNode()
: Dimensions(0.0f, 0.0f)
, NodeTitleFull(FText::GetEmpty())
, NodeTitle(FText::GetEmpty())
, CachedTitleColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
, CachedNodeColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
{
	bHasCompilerMessage = false;
	ErrorType = (int32)EMessageSeverity::Info + 1;
	ParameterType = (int32)EControlRigModelParameterType::None;
}

FText UControlRigGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if(NodeTitle.IsEmpty() || NodeTitleFull.IsEmpty())
	{
		UScriptStruct* ScriptStruct = GetUnitScriptStruct();
		if(ScriptStruct && ScriptStruct->HasMetaData(UControlRig::DisplayNameMetaName))
		{
			if(ScriptStruct->HasMetaData(UControlRig::ShowVariableNameInTitleMetaName))
			{
				NodeTitleFull = FText::Format(LOCTEXT("NodeFullTitleFormat", "{0}\n{1}"), FText::FromName(PropertyName), FText::FromString(ScriptStruct->GetMetaData(UControlRig::DisplayNameMetaName)));
				NodeTitle = FText::FromName(PropertyName);
			}
			else
			{
				NodeTitle = NodeTitleFull = FText::FromString(ScriptStruct->GetMetaData(UControlRig::DisplayNameMetaName));
			}
		}
		else
		{
			NodeTitle = NodeTitleFull = FText::FromName(PropertyName);
		}
	}

	if(TitleType == ENodeTitleType::FullTitle)
	{
		return NodeTitleFull;
	}
	else
	{
		return NodeTitle;
	}
}

void UControlRigGraphNode::ReconstructNode()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetGraph());
	if (RigGraph)
	{
		if (RigGraph->bIsTemporaryGraphForCopyPaste)
		{
			return;
		}
	}

	// Clear previously set messages
	ErrorMsg.Reset();

	// @TODO: support pin orphaning/conversions for upgrades/deprecations?

	// Move the existing pins to a saved array
	TArray<UEdGraphPin*> OldPins(Pins);
	Pins.Reset();

	// Recreate the new pins
	ReallocatePinsDuringReconstruction(OldPins);
	RewireOldPinsToNewPins(OldPins, Pins);

	// Let subclasses do any additional work
	PostReconstructNode();

	GetGraph()->NotifyGraphChanged();

	UScriptStruct* ScriptStruct = GetUnitScriptStruct();
	if (ScriptStruct)
	{
		StructPath = ScriptStruct->GetPathName();
	}
}

void UControlRigGraphNode::CacheHierarchyRefConnectionsOnPostLoad()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (HierarchyRefOutputConnections.Num() > 0)
	{
		return;
	}
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			continue;
		}
		if (Pin->PinType.PinSubCategoryObject != FRigHierarchyRef::StaticStruct())
		{
			continue;
		}
		if (Pin->Direction == EEdGraphPinDirection::EGPD_Output)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				HierarchyRefOutputConnections.Add(LinkedPin->GetOwningNode());
			}
		}
		else if (Pin->Direction == EEdGraphPinDirection::EGPD_Input)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				UControlRigGraphNode* LinkedNode = Cast<UControlRigGraphNode>(LinkedPin->GetOwningNode());
				if (LinkedNode)
				{
					LinkedNode->HierarchyRefOutputConnections.Add(this);
				}
			}
		}
	}
}

void UControlRigGraphNode::PrepareForCopying()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// cache the data we need for paste to work
	// we fill up struct for rig unit
	UScriptStruct* ScriptStruct = GetUnitScriptStruct();
	if (ScriptStruct)
	{
		StructPath = ScriptStruct->GetPathName();
	}
	// or property
	FProperty* Property = GetProperty();
	if (Property)
	{
		FString PropertyPath = Property->GetPathName();

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();	
		Schema->ConvertPropertyToPinType(Property, PinType);
	}
}

bool UControlRigGraphNode::IsDeprecated() const
{
	UScriptStruct* ScriptStruct = GetUnitScriptStruct();
	if (ScriptStruct)
	{
		FString DeprecatedMetadata;
		ScriptStruct->GetStringMetaDataHierarchical(UControlRig::DeprecatedMetaName, &DeprecatedMetadata);
		if (!DeprecatedMetadata.IsEmpty())
		{
			return true;
		}
	}
	return Super::IsDeprecated();
}

FEdGraphNodeDeprecationResponse UControlRigGraphNode::GetDeprecationResponse(EEdGraphNodeDeprecationType DeprecationType) const
{
	FEdGraphNodeDeprecationResponse Response = Super::GetDeprecationResponse(DeprecationType);

	UScriptStruct* ScriptStruct = GetUnitScriptStruct();
	if (ScriptStruct)
	{
		FString DeprecatedMetadata;
		ScriptStruct->GetStringMetaDataHierarchical(UControlRig::DeprecatedMetaName, &DeprecatedMetadata);
		if (!DeprecatedMetadata.IsEmpty())
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("DeprecatedMetadata"), FText::FromString(DeprecatedMetadata));
			Response.MessageText = FText::Format(LOCTEXT("ControlRigGraphNodeDeprecationMessage", "Warning: This node is deprecated from: {DeprecatedMetadata}"), Args);
		}
	}

	return Response;
}

void UControlRigGraphNode::ReallocatePinsDuringReconstruction(const TArray<UEdGraphPin*>& OldPins)
{
	AllocateDefaultPins();
}

void UControlRigGraphNode::RewireOldPinsToNewPins(TArray<UEdGraphPin*>& InOldPins, TArray<UEdGraphPin*>& InNewPins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	// @TODO: we should account for redirectors, orphaning etc. here too!

	for(UEdGraphPin* OldPin : InOldPins)
	{
		for(UEdGraphPin* NewPin : InNewPins)
		{
			if(OldPin->PinName == NewPin->PinName && OldPin->PinType == NewPin->PinType && OldPin->Direction == NewPin->Direction)
			{
				NewPin->MovePersistentDataFromOldPin(*OldPin);
				break;
			}
		}
	}

	DestroyPinList(InOldPins);
}

void UControlRigGraphNode::DestroyPinList(TArray<UEdGraphPin*>& InPins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UBlueprint* Blueprint = GetBlueprint();
	bool bNotify = false;
	if (Blueprint != nullptr)
	{
		bNotify = !Blueprint->bIsRegeneratingOnLoad;
	}

	// Throw away the original pins
	for (UEdGraphPin* Pin : InPins)
	{
		Pin->BreakAllPinLinks(bNotify);

		UEdGraphNode::DestroyPin(Pin);
	}
}

void UControlRigGraphNode::PostReconstructNode()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (UEdGraphPin* Pin : Pins)
	{
		SetupPinDefaultsFromCDO(Pin);
	}

	bCanRenameNode = false;

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		if (Blueprint->Model)
		{
			if (const FControlRigModelNode* ModelNode = Blueprint->Model->FindNode(PropertyName))
			{
				SetColorFromModel(ModelNode->Color);
			}
		}
	}
}

void UControlRigGraphNode::SetColorFromModel(const FLinearColor& InColor)
{
	static const FLinearColor TitleToNodeColor(0.35f, 0.35f, 0.35f, 1.f);
	CachedNodeColor = InColor * TitleToNodeColor;
	CachedTitleColor = InColor;
}

#if WITH_EDITORONLY_DATA
void UControlRigGraphNode::PostLoad()
{
	Super::PostLoad();
	HierarchyRefOutputConnections.Reset();
}
#endif

void UControlRigGraphNode::CreateVariablePins(bool bAlwaysCreatePins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	CacheVariableInfo();
	CreateExecutionPins(bAlwaysCreatePins);
	CreateInputPins(bAlwaysCreatePins);
	CreateInputOutputPins(bAlwaysCreatePins);
	CreateOutputPins(bAlwaysCreatePins);
}

void UControlRigGraphNode::HandleClearArray(FString InPropertyPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		if (Blueprint->ModelController && Blueprint->Model)
		{
			FString Left, Right;
			Blueprint->Model->SplitPinPath(InPropertyPath, Left, Right);
			Blueprint->ModelController->ClearArrayPin(*Left, *Right);
		}
	}
}

void UControlRigGraphNode::HandleAddArrayElement(FString InPropertyPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		if (Blueprint->ModelController && Blueprint->Model)
		{
			FString Left, Right;
			Blueprint->Model->SplitPinPath(InPropertyPath, Left, Right);
			Blueprint->ModelController->AddArrayPin(*Left, *Right, FString());
		}
	}
}

void UControlRigGraphNode::HandleRemoveArrayElement(FString InPropertyPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		if (Blueprint->ModelController && Blueprint->Model)
		{
			const FControlRigModelPin* ChildPin = Blueprint->Model->FindPinFromPath(*InPropertyPath);
			if (ChildPin)
			{
				const FControlRigModelPin* ParentPin = Blueprint->Model->GetParentPin(ChildPin->GetPair());
				if (ParentPin)
				{
					FString ParentPinPath = Blueprint->Model->GetPinPath(ParentPin->GetPair());
					FString Left, Right;
					Blueprint->Model->SplitPinPath(ParentPinPath, Left, Right);
					// todo: really should be remove at index
					Blueprint->ModelController->PopArrayPin(*Left, *Right);
				}
			}
		}
	}
}

void UControlRigGraphNode::HandleInsertArrayElement(FString InPropertyPath)
{
	// todo: really should be insert
	HandleAddArrayElement(InPropertyPath);
}

void UControlRigGraphNode::AllocateDefaultPins()
{
	CreateVariablePins(true);
}

/** Helper function to check whether this is a struct reference pin */
static bool IsStructReference(const TSharedPtr<FControlRigField>& InputInfo)
{
	if(FStructProperty* StructProperty = CastField<FStructProperty>(InputInfo->GetField()))
	{
		return StructProperty->Struct->IsChildOf(FStructReference::StaticStruct());
	}

	return false;
}

void UControlRigGraphNode::CreateExecutionPins(bool bAlwaysCreatePins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<TSharedRef<FControlRigField>>& LocalExecutionInfos = GetExecutionVariableInfo();

	for (const TSharedRef<FControlRigField>& ExecutionInfo : LocalExecutionInfos)
	{
		if (bAlwaysCreatePins || ExecutionInfo->InputPin == nullptr)
		{
			ExecutionInfo->InputPin = CreatePin(EGPD_Input, ExecutionInfo->GetPinType(), FName(*ExecutionInfo->GetPinPath()));
			ExecutionInfo->InputPin->PinFriendlyName = ExecutionInfo->GetDisplayNameText();
			ExecutionInfo->InputPin->PinType.bIsReference = IsStructReference(ExecutionInfo);
			ExecutionInfo->InputPin->DefaultValue = ExecutionInfo->GetPin()->DefaultValue;
		}

		if (bAlwaysCreatePins || ExecutionInfo->OutputPin == nullptr)
		{
			ExecutionInfo->OutputPin = CreatePin(EGPD_Output, ExecutionInfo->GetPinType(), FName(*ExecutionInfo->GetPinPath()));
		}

		// note: no recursion for execution pins
	}
}

void UControlRigGraphNode::CreateInputPins_Recursive(const TSharedPtr<FControlRigField>& InputInfo, bool bAlwaysCreatePins)
{
	for (const TSharedPtr<FControlRigField>& ChildInfo : InputInfo->Children)
	{
		if (bAlwaysCreatePins || ChildInfo->InputPin == nullptr)
		{
			ChildInfo->InputPin = CreatePin(EGPD_Input, ChildInfo->GetPinType(), FName(*ChildInfo->GetPinPath()));
			ChildInfo->InputPin->PinFriendlyName = ChildInfo->GetDisplayNameText();
			ChildInfo->InputPin->PinType.bIsReference = IsStructReference(ChildInfo);
			ChildInfo->InputPin->ParentPin = InputInfo->InputPin;
			ChildInfo->InputPin->DefaultValue = ChildInfo->GetPin()->DefaultValue;
			ChildInfo->OutputPin = nullptr;
			InputInfo->InputPin->SubPins.Add(ChildInfo->InputPin);
		}
	}

	for (const TSharedPtr<FControlRigField>& ChildInfo : InputInfo->Children)
	{
		CreateInputPins_Recursive(ChildInfo, bAlwaysCreatePins);
	}
}

void UControlRigGraphNode::CreateInputPins(bool bAlwaysCreatePins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<TSharedRef<FControlRigField>>& LocalInputInfos = GetInputVariableInfo();

	for (const TSharedRef<FControlRigField>& InputInfo : LocalInputInfos)
	{
		if (bAlwaysCreatePins || InputInfo->InputPin == nullptr)
		{
			InputInfo->InputPin = CreatePin(EGPD_Input, InputInfo->GetPinType(), FName(*InputInfo->GetPinPath()));
			InputInfo->InputPin->PinFriendlyName = InputInfo->GetDisplayNameText();
			InputInfo->InputPin->PinType.bIsReference = IsStructReference(InputInfo);
			InputInfo->InputPin->DefaultValue = InputInfo->GetPin()->DefaultValue;
			InputInfo->OutputPin = nullptr;
		}
		else
		{
			InputInfo->InputPin->DefaultValue = InputInfo->GetPin()->DefaultValue;
		}

		CreateInputPins_Recursive(InputInfo, bAlwaysCreatePins);
	}
}

void UControlRigGraphNode::CreateInputOutputPins_Recursive(const TSharedPtr<FControlRigField>& InputOutputInfo, bool bAlwaysCreatePins)
{
	for (const TSharedPtr<FControlRigField>& ChildInfo : InputOutputInfo->Children)
	{
		if (bAlwaysCreatePins || ChildInfo->InputPin == nullptr)
		{
			ChildInfo->InputPin = CreatePin(EGPD_Input, ChildInfo->GetPinType(), FName(*ChildInfo->GetPinPath()));
			ChildInfo->InputPin->PinFriendlyName = ChildInfo->GetDisplayNameText();
			ChildInfo->InputPin->PinType.bIsReference = IsStructReference(ChildInfo);
			ChildInfo->InputPin->DefaultValue = ChildInfo->GetPin()->DefaultValue;
			ChildInfo->InputPin->ParentPin = InputOutputInfo->InputPin;
			InputOutputInfo->InputPin->SubPins.Add(ChildInfo->InputPin);
		}
		else
		{
			ChildInfo->InputPin->DefaultValue = ChildInfo->GetPin()->DefaultValue;
		}

		if (bAlwaysCreatePins || ChildInfo->OutputPin == nullptr)
		{
			ChildInfo->OutputPin = CreatePin(EGPD_Output, ChildInfo->GetPinType(), FName(*ChildInfo->GetPinPath()));
			ChildInfo->OutputPin->PinFriendlyName = ChildInfo->GetDisplayNameText();
			ChildInfo->OutputPin->ParentPin = InputOutputInfo->OutputPin;
			ChildInfo->OutputPin->PinType.bIsReference = IsStructReference(ChildInfo);
			InputOutputInfo->OutputPin->SubPins.Add(ChildInfo->OutputPin);
		}
	}

	for (const TSharedPtr<FControlRigField>& ChildInfo : InputOutputInfo->Children)
	{
		CreateInputOutputPins_Recursive(ChildInfo, bAlwaysCreatePins);
	}
}

void UControlRigGraphNode::CreateInputOutputPins(bool bAlwaysCreatePins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<TSharedRef<FControlRigField>>& LocalInputOutputInfos = GetInputOutputVariableInfo();

	for (const TSharedRef<FControlRigField>& InputOutputInfo : LocalInputOutputInfos)
	{
		if (bAlwaysCreatePins || InputOutputInfo->InputPin == nullptr)
		{
			InputOutputInfo->InputPin = CreatePin(EGPD_Input, InputOutputInfo->GetPinType(), FName(*InputOutputInfo->GetPinPath()));
			InputOutputInfo->InputPin->PinFriendlyName = InputOutputInfo->GetDisplayNameText();
			InputOutputInfo->InputPin->PinType.bIsReference = IsStructReference(InputOutputInfo);
			InputOutputInfo->InputPin->DefaultValue = InputOutputInfo->GetPin()->DefaultValue;
		}

		if (bAlwaysCreatePins || InputOutputInfo->OutputPin == nullptr)
		{
			InputOutputInfo->OutputPin = CreatePin(EGPD_Output, InputOutputInfo->GetPinType(), FName(*InputOutputInfo->GetPinPath()));
		}

		CreateInputOutputPins_Recursive(InputOutputInfo, bAlwaysCreatePins);
	}
}

void UControlRigGraphNode::CreateOutputPins_Recursive(const TSharedPtr<FControlRigField>& OutputInfo, bool bAlwaysCreatePins)
{
	for (const TSharedPtr<FControlRigField>& ChildInfo : OutputInfo->Children)
	{
		if (bAlwaysCreatePins || ChildInfo->OutputPin == nullptr)
		{
			ChildInfo->OutputPin = CreatePin(EGPD_Output, ChildInfo->GetPinType(), FName(*ChildInfo->GetPinPath()));
			ChildInfo->OutputPin->PinFriendlyName = ChildInfo->GetDisplayNameText();
			ChildInfo->OutputPin->PinType.bIsReference = IsStructReference(ChildInfo);
			ChildInfo->OutputPin->ParentPin = OutputInfo->OutputPin;
			ChildInfo->InputPin = nullptr;
			OutputInfo->OutputPin->SubPins.Add(ChildInfo->OutputPin);
		}
	}

	for (const TSharedPtr<FControlRigField>& ChildInfo : OutputInfo->Children)
	{
		CreateOutputPins_Recursive(ChildInfo, bAlwaysCreatePins);
	}
}

void UControlRigGraphNode::CreateOutputPins(bool bAlwaysCreatePins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<TSharedRef<FControlRigField>>& LocalOutputInfos = GetOutputVariableInfo();

	for (const TSharedRef<FControlRigField>& OutputInfo : LocalOutputInfos)
	{
		if (bAlwaysCreatePins || OutputInfo->OutputPin == nullptr)
		{
			OutputInfo->OutputPin = CreatePin(EGPD_Output, OutputInfo->GetPinType(), FName(*OutputInfo->GetPinPath()));
			OutputInfo->OutputPin->PinFriendlyName = OutputInfo->GetDisplayNameText();
			OutputInfo->OutputPin->PinType.bIsReference = IsStructReference(OutputInfo);
			OutputInfo->InputPin = nullptr;
		}

		CreateOutputPins_Recursive(OutputInfo, bAlwaysCreatePins);
	}
}

void UControlRigGraphNode::CacheVariableInfo()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	ExecutionInfos.Reset();
	GetExecutionFields(ExecutionInfos);

	InputInfos.Reset();
	GetInputFields(InputInfos);

	OutputInfos.Reset();
	GetOutputFields(OutputInfos);

	InputOutputInfos.Reset();
	GetInputOutputFields(InputOutputInfos);
}

UClass* UControlRigGraphNode::GetControlRigGeneratedClass() const
{
	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if(Blueprint)
	{
		if (Blueprint->GeneratedClass)
		{
			check(Blueprint->GeneratedClass->IsChildOf(UControlRig::StaticClass()));
			return Blueprint->GeneratedClass;
		}
	}

	return nullptr;
}

UClass* UControlRigGraphNode::GetControlRigSkeletonGeneratedClass() const
{
	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if(Blueprint)
	{
		if (Blueprint->SkeletonGeneratedClass)
		{
			check(Blueprint->SkeletonGeneratedClass->IsChildOf(UControlRig::StaticClass()));
			return Blueprint->SkeletonGeneratedClass;
		}
	}
	return nullptr;
}

FLinearColor UControlRigGraphNode::GetNodeTitleColor() const
{
	// return a darkened version of the default node's color
	return CachedTitleColor;
}

FLinearColor UControlRigGraphNode::GetNodeBodyTintColor() const
{
	return CachedNodeColor;
}

FSlateIcon UControlRigGraphNode::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

// bool UControlRigGraphNode::ReferencesVariable(const FName& InVarName, const UStruct* InScope) const
// {
// 	return InVarName == PropertyName;
// }

TSharedPtr<FControlRigField> UControlRigGraphNode::CreateControlRigField(const FControlRigModelPin* InPin, const FString& InPinPath, int32 InArrayIndex) const
{
	TSharedPtr<FControlRigField> NewField = MakeShareable(new FControlRigPin(InPin, InPinPath, InArrayIndex));
	NewField->DisplayNameText = InPin->DisplayNameText;
	NewField->TooltipText = InPin->TooltipText;
	NewField->InputPin = FindPin(InPinPath, EGPD_Input);
	NewField->OutputPin = FindPin(InPinPath, EGPD_Output);
	return NewField;
}

void UControlRigGraphNode::GetExecutionFields(TArray<TSharedRef<FControlRigField>>& OutFields) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GetFields([](const FControlRigModelPin* InPin, const FControlRigModelNode* InNode)
	{
		FString PinPath = InNode->GetPinPath(InPin->Index, false);
		return InPin->Direction == EGPD_Output &&
			InNode->FindPin(*PinPath, true) != nullptr &&
			InPin->Type.PinSubCategoryObject == FControlRigExecuteContext::StaticStruct();
	}, OutFields);
}

void UControlRigGraphNode::GetInputFields(TArray<TSharedRef<FControlRigField>>& OutFields) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GetFields([](const FControlRigModelPin* InPin, const FControlRigModelNode* InNode)
	{
		FString PinPath = InNode->GetPinPath(InPin->Index, false);
		if (InPin->Direction != EGPD_Input)
		{
			return false;
		}

		if (InNode->IsParameter() && InNode->ParameterType == EControlRigModelParameterType::Output)
		{
			return true;
		}

		return InNode->FindPin(*PinPath, false) == nullptr;
	}, OutFields);
}

void UControlRigGraphNode::GetOutputFields(TArray<TSharedRef<FControlRigField>>& OutFields) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GetFields([](const FControlRigModelPin* InPin, const FControlRigModelNode* InNode)
	{
		FString PinPath = InNode->GetPinPath(InPin->Index, false);
		if (InPin->Direction != EGPD_Output)
		{
			return false;
		}
		
		if (InNode->IsParameter() && InNode->ParameterType == EControlRigModelParameterType::Input)
		{
			return true;
		}
		
		return InNode->FindPin(*PinPath, true) == nullptr;
	}, OutFields);
}

void UControlRigGraphNode::GetInputOutputFields(TArray<TSharedRef<FControlRigField>>& OutFields) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GetFields([](const FControlRigModelPin* InPin, const FControlRigModelNode* InNode)
	{
		if (InNode->IsParameter())
		{
			return false;
		}
		FString PinPath = InNode->GetPinPath(InPin->Index, false);
		return InPin->Direction == EGPD_Input && 
			InNode->FindPin(*PinPath, false) != nullptr &&
			InPin->Type.PinSubCategoryObject != FControlRigExecuteContext::StaticStruct();
	}, OutFields);
}

void UControlRigGraphNode::GetFields(TFunction<bool(const FControlRigModelPin*, const FControlRigModelNode*)> InPinCheckFunction, TArray<TSharedRef<FControlRigField>>& OutFields) const
{
	OutFields.Reset();

	FControlRigModelNode Node;

	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(GetBlueprint()))
	{
		if (RigBlueprint->Model)
		{
			const FControlRigModelNode* FoundNode = RigBlueprint->Model->FindNode(GetPropertyName());
			if (FoundNode != nullptr)
			{
				Node = *FoundNode;
			}
		}
	}

	if (!Node.IsValid())
	{
		if (IsVariable())
		{
			FName DataType = PinType.PinCategory;
			if (UStruct* Struct = Cast<UStruct>(PinType.PinSubCategoryObject))
			{
				DataType = Struct->GetFName();
			}
			UControlRigController::ConstructPreviewParameter(DataType, EControlRigModelParameterType::Input, Node);
		}
		else
		{
			if (UStruct* Struct = GetUnitScriptStruct())
			{
				FName FunctionName = Struct->GetFName();
				UControlRigController::ConstructPreviewNode(FunctionName, Node);
			}
		}
	}

	if (!Node.IsValid())
	{
		return;
	}

	Node.Name = GetPropertyName();

	TMap<int32, TSharedRef<FControlRigField>> AllFields;
	for(int32 PinIndex=0;PinIndex < Node.Pins.Num(); PinIndex++)
	{
		const FControlRigModelPin& Pin = Node.Pins[PinIndex];
		if (InPinCheckFunction(&Pin, &Node))
		{
			FString PinPath = Node.GetPinPath(Pin.Index, true);
			TSharedPtr<FControlRigField> NewField = CreateControlRigField(&Pin, PinPath);
			if (NewField.IsValid())
			{
				TSharedRef<FControlRigField> NewFieldRef = NewField.ToSharedRef();
				AllFields.Add(Pin.Index, NewFieldRef);

				TSharedRef<FControlRigField>* ParentField = AllFields.Find(Pin.ParentIndex);
				if (ParentField)
				{
					(*ParentField)->Children.Add(NewFieldRef);
				}
				else
				{
					OutFields.Add(NewFieldRef);
				}
			}
		}
	}
}

FStructProperty* UControlRigGraphNode::GetUnitProperty() const
{
	FProperty* ClassProperty = GetProperty();
	if(ClassProperty)
	{
		// Check if this is a unit struct and if so extract the pins we want to display...
		if(FStructProperty* StructProperty = CastField<FStructProperty>(ClassProperty))
		{
			if(StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
			{
				return StructProperty;
			}
		}
	}

	return nullptr;
}

UScriptStruct* UControlRigGraphNode::GetUnitScriptStruct() const
{
	if(FStructProperty* StructProperty = GetUnitProperty())
	{
		if(StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
		{
			return StructProperty->Struct;
		}
	}
	else 
	{
		// Assume that the property name we have is the name of the struct type
		UScriptStruct* Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *PropertyName.ToString());
		if(Struct && Struct->IsChildOf(FRigUnit::StaticStruct()))
		{
			return Struct;
		}

		// if this doesn't work we can still fall back on the struct path
		FString Prefix, StructName;
		if (StructPath.Split(TEXT("."), &Prefix, &StructName))
		{
			Struct = FindObject<UScriptStruct>(ANY_PACKAGE, *StructName);
			if (Struct && Struct->IsChildOf(FRigUnit::StaticStruct()))
			{
				return Struct;
			}
		}
	}
	return nullptr;
}

FProperty* UControlRigGraphNode::GetProperty() const
{
	if (UClass* MyControlRigClass = GetControlRigSkeletonGeneratedClass())
	{
		return MyControlRigClass->FindPropertyByName(PropertyName);
	}
	return nullptr;
}

void UControlRigGraphNode::PinConnectionListChanged(UEdGraphPin* Pin) 
{

}

void UControlRigGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
#if WITH_EDITOR
	IControlRigEditorModule::Get().GetNodeContextMenuActions(this, Menu, Context);
#endif
}

void UControlRigGraphNode::SetPinExpansion(const FString& InPinPropertyPath, bool bExpanded)
{
	if(bExpanded)
	{
		ExpandedPins.AddUnique(InPinPropertyPath);
	}
	else
	{
		ExpandedPins.Remove(InPinPropertyPath);
	}
}

bool UControlRigGraphNode::IsPinExpanded(const FString& InPinPropertyPath) const
{
	return ExpandedPins.Contains(InPinPropertyPath);
}

void UControlRigGraphNode::DestroyNode()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Graph->GetOuter());
		if(ControlRigBlueprint)
		{
			BreakAllNodeLinks();
			FControlRigBlueprintUtils::RemoveMemberVariableIfNotUsed(ControlRigBlueprint, PropertyName, this);
		}
	}

	UEdGraphNode::DestroyNode();
}

void UControlRigGraphNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	CopyPinDefaultsToModel(Pin, true);
}

TSharedPtr<INameValidatorInterface> UControlRigGraphNode::MakeNameValidator() const
{
	return MakeShared<FKismetNameValidator>(GetBlueprint(), PropertyName);
}

void UControlRigGraphNode::CopyPinDefaultsToModel(UEdGraphPin* Pin, bool bUndo)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Graph->GetOuter());
		if(ControlRigBlueprint)
		{
			if (ControlRigBlueprint->Model)
			{
				if (Pin->Direction == EGPD_Input)
				{
					FString DefaultValue = Pin->DefaultValue;
					FString Left, Right;
					ControlRigBlueprint->Model->SplitPinPath(Pin->GetName(), Left, Right);
					if (DefaultValue.IsEmpty() && Pin->DefaultObject != nullptr)
					{
						DefaultValue = Pin->DefaultObject->GetPathName();
					}
					ControlRigBlueprint->ModelController->SetPinDefaultValue(*Left, *Right, DefaultValue, false, bUndo);
				}
			}
		}
	}
}

UControlRigBlueprint* UControlRigGraphNode::GetBlueprint() const
{
	if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		return Cast<UControlRigBlueprint>(Graph->GetOuter());
	}
	return nullptr;
}

void UControlRigGraphNode::SetupPinDefaultsFromCDO(UEdGraphPin* Pin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	if(UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
		UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(Graph->GetOuter());
		if(ControlRigBlueprint)
		{
			// Note we need the actual generated class here
			if (UClass* MyControlRigClass = GetControlRigGeneratedClass())
			{
				if(UObject* DefaultObject = MyControlRigClass->GetDefaultObject(false))
				{
					FString DefaultValueString;
					FCachedPropertyPath PropertyPath(Pin->PinName.ToString());
					if(PropertyPathHelpers::GetPropertyValueAsString(DefaultObject, PropertyPath, DefaultValueString))
					{
						K2Schema->GetPinDefaultValuesFromString(Pin->PinType, Pin->GetOwningNodeUnchecked(), DefaultValueString, Pin->DefaultValue, Pin->DefaultObject, Pin->DefaultTextValue);
					}
				}
			}
		}
	}
}

FText UControlRigGraphNode::GetTooltipText() const
{
	if(GetUnitScriptStruct())
	{
		return GetUnitScriptStruct()->GetToolTipText();
	}
	else if(GetUnitProperty())
	{
		return GetUnitProperty()->GetToolTipText();
	}

	return FText::FromName(PropertyName);
}

void UControlRigGraphNode::InvalidateNodeTitle() const
{
	NodeTitleFull = FText();
	NodeTitle = FText();
}

bool UControlRigGraphNode::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* InSchema) const
{
	return InSchema->IsA<UControlRigGraphSchema>();
}

void UControlRigGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::AutowireNewNode(FromPin);

	const UControlRigGraphSchema* Schema = GetDefault<UControlRigGraphSchema>();

	for(UEdGraphPin* Pin : Pins)
	{
		if (Pin->ParentPin != nullptr)
		{
			continue;
		}

		FPinConnectionResponse ConnectResponse = Schema->CanCreateConnection(FromPin, Pin);
		if(ConnectResponse.Response != ECanCreateConnectionResponse::CONNECT_RESPONSE_DISALLOW)
		{
			if(Schema->TryCreateConnection(FromPin, Pin))
			{
				// expand any sub-pins so the connection is visible
				if(UControlRigGraphNode* OuterNode = Cast<UControlRigGraphNode>(Pin->GetOwningNode()))
				{
					UEdGraphPin* ParentPin = Pin->ParentPin;
					while(ParentPin != nullptr)
					{
						OuterNode->SetPinExpansion(ParentPin->PinName.ToString(), true);
						ParentPin = ParentPin->ParentPin;
					}
				}
				return;
			}
		}
	}
}

void ReplacePropertyName(TArray<TSharedRef<FControlRigField>>& InArray, const FString& OldPropName, const FString& NewPropName)
{
	for (int32 Index = 0; Index < InArray.Num(); ++Index)
	{
		InArray[Index]->PinPath = InArray[Index]->PinPath.Replace(*OldPropName, *NewPropName);
		ReplacePropertyName(InArray[Index]->Children, OldPropName, NewPropName);
	}
};

void UControlRigGraphNode::SetPropertyName(const FName& InPropertyName, bool bReplaceInnerProperties/*=false*/)
{ 
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const FString OldPropertyName = PropertyName.ToString();
	const FString NewPropertyName = InPropertyName.ToString();
	PropertyName = InPropertyName;

	if (bReplaceInnerProperties && InPropertyName != NAME_None)
	{
		ReplacePropertyName(InputInfos, OldPropertyName, NewPropertyName);
		ReplacePropertyName(InputOutputInfos, OldPropertyName, NewPropertyName);
		ReplacePropertyName(OutputInfos, OldPropertyName, NewPropertyName);

		// now change pins
		for (int32 Index = 0; Index < Pins.Num(); ++Index)
		{
			FString PinString = Pins[Index]->PinName.ToString();
			Pins[Index]->PinName = FName(*PinString.Replace(*OldPropertyName, *NewPropertyName));
		}

		for (int32 Index = 0; Index < ExpandedPins.Num(); ++Index)
		{
			FString& PinString = ExpandedPins[Index];
			PinString = PinString.Replace(*OldPropertyName, *NewPropertyName);
		}
	}
}

bool UControlRigGraphNode::IsVariable() const
{
	return GetUnitScriptStruct() == nullptr;
}

#undef LOCTEXT_NAMESPACE
