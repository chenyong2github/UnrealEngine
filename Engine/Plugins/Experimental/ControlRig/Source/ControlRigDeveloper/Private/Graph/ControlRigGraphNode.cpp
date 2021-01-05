// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "RigVMCore/RigVMExecuteContext.h"
#include "ControlRigDeveloper.h"
#include "ControlRigObjectVersion.h"

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigGraphNode"

UControlRigGraphNode::UControlRigGraphNode()
: Dimensions(0.0f, 0.0f)
, NodeTitle(FText::GetEmpty())
, CachedTitleColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
, CachedNodeColor(FLinearColor(0.f, 0.f, 0.f, 0.f))
{
	bHasCompilerMessage = false;
	ErrorType = (int32)EMessageSeverity::Info + 1;
}

FText UControlRigGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if(NodeTitle.IsEmpty())
	{
		if(URigVMNode* ModelNode = GetModelNode())
		{
			if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(ModelNode))
			{
				if (StructNode->GetScriptStruct()->IsChildOf(FRigUnit::StaticStruct()))
				{
					if (TSharedPtr<FStructOnScope> StructOnScope = StructNode->ConstructStructInstance())
					{
						FRigUnit* RigUnit = (FRigUnit*)StructOnScope->GetStructMemory();
						NodeTitle = FText::FromString(RigUnit->GetUnitLabel());
					}
				}
			}

			if (NodeTitle.IsEmpty())
			{
				NodeTitle = FText::FromString(ModelNode->GetNodeTitle());
			}
		}

		if(IsDeprecated())
		{
			NodeTitle = FText::FromString(FString::Printf(TEXT("%s (Deprecated)"), *NodeTitle.ToString()));
		}
	}

	return NodeTitle;
}

void UControlRigGraphNode::ReconstructNode()
{
	ReconstructNode_Internal();
}

void UControlRigGraphNode::ReconstructNode_Internal(bool bForce)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GetGraph());
	if (RigGraph && !bForce)
	{
		if (RigGraph->bIsTemporaryGraphForCopyPaste)
		{
			return;
		}

		// if this node has been saved prior to our custom version,
		// don't reset the node
		int32 LinkerVersion = RigGraph->GetLinkerCustomVersion(FControlRigObjectVersion::GUID);
		if (LinkerVersion < FControlRigObjectVersion::SwitchedToRigVM)
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
	CachedPins.Reset();
	CachedModelPins.Reset();
	ReallocatePinsDuringReconstruction(OldPins);
	RewireOldPinsToNewPins(OldPins, Pins);

	// Let subclasses do any additional work
	PostReconstructNode();

	GetGraph()->NotifyGraphChanged();
}

bool UControlRigGraphNode::IsDeprecated() const
{
	if(URigVMNode* ModelNode = GetModelNode())
	{
		if(URigVMStructNode* StructModelNode = Cast<URigVMStructNode>(ModelNode))
		{
			return StructModelNode->IsDeprecated();
		}
	}
	return Super::IsDeprecated();
}

FEdGraphNodeDeprecationResponse UControlRigGraphNode::GetDeprecationResponse(EEdGraphNodeDeprecationType DeprecationType) const
{
	FEdGraphNodeDeprecationResponse Response = Super::GetDeprecationResponse(DeprecationType);

	if(URigVMNode* ModelNode = GetModelNode())
	{
		if(URigVMStructNode* StructModelNode = Cast<URigVMStructNode>(ModelNode))
		{
			FString DeprecatedMetadata = StructModelNode->GetDeprecatedMetadata();
			if (!DeprecatedMetadata.IsEmpty())
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("DeprecatedMetadata"), FText::FromString(DeprecatedMetadata));
				Response.MessageText = FText::Format(LOCTEXT("ControlRigGraphNodeDeprecationMessage", "Warning: This node is deprecated from: {DeprecatedMetadata}"), Args);
			}
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
		SetupPinDefaultsFromModel(Pin);
	}

	bCanRenameNode = false;

	if(URigVMNode* ModelNode = GetModelNode())
	{
		SetColorFromModel(ModelNode->GetNodeColor());
	}
}

void UControlRigGraphNode::SetColorFromModel(const FLinearColor& InColor)
{
	static const FLinearColor TitleToNodeColor(0.35f, 0.35f, 0.35f, 1.f);
	CachedNodeColor = InColor * TitleToNodeColor;
	CachedTitleColor = InColor;
}

void UControlRigGraphNode::HandleClearArray(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		Blueprint->Controller->ClearArrayPin(InPinPath);
	}
}

void UControlRigGraphNode::HandleAddArrayElement(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		Blueprint->Controller->OpenUndoBracket(TEXT("Add Array Pin"));
		FString PinPath = Blueprint->Controller->AddArrayPin(InPinPath);
		Blueprint->Controller->SetPinExpansion(InPinPath, true);
		Blueprint->Controller->SetPinExpansion(PinPath, true);
		Blueprint->Controller->CloseUndoBracket();
	}
}

void UControlRigGraphNode::HandleRemoveArrayElement(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		Blueprint->Controller->RemoveArrayPin(InPinPath);
	}
}

void UControlRigGraphNode::HandleInsertArrayElement(FString InPinPath)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(GetOuter()->GetOuter());
	if (Blueprint)
	{
		if (URigVMPin* ArrayElementPin = GetModelPinFromPinPath(InPinPath))
			{
			if (URigVMPin* ArrayPin = ArrayElementPin->GetParentPin())
				{
				Blueprint->Controller->OpenUndoBracket(TEXT("Add Array Pin"));
				FString PinPath = Blueprint->Controller->InsertArrayPin(InPinPath, ArrayElementPin->GetPinIndex() + 1, FString());
				Blueprint->Controller->SetPinExpansion(InPinPath, true);
				Blueprint->Controller->SetPinExpansion(PinPath, true);
				Blueprint->Controller->CloseUndoBracket();
			}
		}
	}
}

void UControlRigGraphNode::AllocateDefaultPins()
{
	ExecutePins.Reset();
	InputPins.Reset();
	InputOutputPins.Reset();
	OutputPins.Reset();

	if (URigVMNode* ModelNode = GetModelNode())
	{
		for (URigVMPin* ModelPin : ModelNode->GetPins())
		{
			if (ModelPin->ShowInDetailsPanelOnly())
			{
				continue;
			}
			if (ModelPin->GetDirection() == ERigVMPinDirection::IO)
			{
				if (ModelPin->IsStruct())
				{
					if (ModelPin->GetScriptStruct()->IsChildOf(FRigVMExecuteContext::StaticStruct()))
					{
						ExecutePins.Add(ModelPin);
						continue;
					}
				}
				InputOutputPins.Add(ModelPin);
			}
			else if (ModelPin->GetDirection() == ERigVMPinDirection::Input || 
				ModelPin->GetDirection() == ERigVMPinDirection::Visible)
			{
				InputPins.Add(ModelPin);
			}
			else if (ModelPin->GetDirection() == ERigVMPinDirection::Output)
			{
				OutputPins.Add(ModelPin);
			}
		}
	}

	CreateExecutionPins();
	CreateInputPins();
	CreateInputOutputPins();
	CreateOutputPins();
}

void UControlRigGraphNode::CreateExecutionPins()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = ExecutePins;
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.InputPin == nullptr)
		{
			Pair.InputPin = CreatePin(EGPD_Input, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.InputPin != nullptr)
		{
				Pair.InputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
		}
	}
		if (Pair.OutputPin == nullptr)
	{
			Pair.OutputPin = CreatePin(EGPD_Output, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.OutputPin != nullptr)
		{
				Pair.OutputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
		}
	}
		// note: no recursion for execution pins
	}
}

void UControlRigGraphNode::CreateInputPins(URigVMPin* InParentPin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = InParentPin == nullptr ? InputPins : InParentPin->GetSubPins();
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.InputPin == nullptr)
		{
			Pair.InputPin = CreatePin(EGPD_Input, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.InputPin != nullptr)
			{
				Pair.InputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.InputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::Input;

				SetupPinDefaultsFromModel(Pair.InputPin);

				if (InParentPin != nullptr)
	{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.InputPin->SubPins.Add(Pair.InputPin);
					Pair.InputPin->ParentPin = ParentPair.InputPin;
				}
		}
		}
		CreateInputPins(ModelPin);
	}
}

void UControlRigGraphNode::CreateInputOutputPins(URigVMPin* InParentPin, bool bHidden)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = InParentPin == nullptr ? InputOutputPins : InParentPin->GetSubPins();
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.InputPin == nullptr)
		{
			Pair.InputPin = CreatePin(EGPD_Input, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.InputPin != nullptr)
		{
				Pair.InputPin->bHidden = bHidden;
				Pair.InputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.InputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::IO;

				SetupPinDefaultsFromModel(Pair.InputPin);

				if (InParentPin != nullptr)
		{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.InputPin->SubPins.Add(Pair.InputPin);
					Pair.InputPin->ParentPin = ParentPair.InputPin;
		}
	}
	}
		if (Pair.OutputPin == nullptr)
	{
			Pair.OutputPin = CreatePin(EGPD_Output, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.OutputPin != nullptr)
		{
				Pair.OutputPin->bHidden = bHidden;
				Pair.OutputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.OutputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::IO;

				if (InParentPin != nullptr)
		{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.OutputPin->SubPins.Add(Pair.OutputPin);
					Pair.OutputPin->ParentPin = ParentPair.OutputPin;
				}
		}
	}

		// don't recurse on knot / compact reroute nodes
		if(URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(GetModelNode()))
	{
			if (!RerouteNode->GetShowsAsFullNode())
		{
				bHidden = true;
		}
	}

		CreateInputOutputPins(ModelPin, bHidden);
	}
}

void UControlRigGraphNode::CreateOutputPins(URigVMPin* InParentPin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	const TArray<URigVMPin*> ModelPins = InParentPin == nullptr ? OutputPins : InParentPin->GetSubPins();
	for (URigVMPin* ModelPin : ModelPins)
	{
		PinPair& Pair = CachedPins.FindOrAdd(ModelPin);
		if (Pair.OutputPin == nullptr)
		{
			Pair.OutputPin = CreatePin(EGPD_Output, GetPinTypeForModelPin(ModelPin), FName(*ModelPin->GetPinPath()));
			if (Pair.OutputPin != nullptr)
			{
				Pair.OutputPin->PinFriendlyName = FText::FromName(ModelPin->GetDisplayName());
				Pair.OutputPin->bNotConnectable = ModelPin->GetDirection() != ERigVMPinDirection::Output;

				if (InParentPin != nullptr)
	{
					PinPair& ParentPair = CachedPins.FindChecked(InParentPin);
					ParentPair.OutputPin->SubPins.Add(Pair.OutputPin);
					Pair.OutputPin->ParentPin = ParentPair.OutputPin;
				}
			}
		}
		CreateOutputPins(ModelPin);
	}
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

FLinearColor UControlRigGraphNode::GetNodeOpacityColor() const
{
	if (URigVMNode* ModelNode = GetModelNode())
	{
		if (Cast<URigVMParameterNode>(ModelNode) || Cast<URigVMVariableNode>(ModelNode))
		{
			return FLinearColor::White;
		}
		if (ModelNode->GetInstructionIndex() == INDEX_NONE)
		{
			return FLinearColor(0.35f, 0.35f, 0.35f, 0.35f);
		}
	}
	return FLinearColor::White;
}

FLinearColor UControlRigGraphNode::GetNodeTitleColor() const
{
	// return a darkened version of the default node's color
	return CachedTitleColor * GetNodeOpacityColor();
}

FLinearColor UControlRigGraphNode::GetNodeBodyTintColor() const
{
	return CachedNodeColor * GetNodeOpacityColor();
}

FSlateIcon UControlRigGraphNode::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

void UControlRigGraphNode::GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const
{
#if WITH_EDITOR
	const UControlRigGraphSchema* Schema = Cast<UControlRigGraphSchema>(GetSchema());
	IControlRigEditorModule::Get().GetContextMenuActions(Schema, Menu, Context);
#endif
}

bool UControlRigGraphNode::IsPinExpanded(const FString& InPinPath)
{
	if (URigVMPin* ModelPin = GetModelPinFromPinPath(InPinPath))
	{
		return ModelPin->IsExpanded();
	}
	return false;
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
			if(PropertyName_DEPRECATED.IsValid())
		{
				FControlRigBlueprintUtils::RemoveMemberVariableIfNotUsed(ControlRigBlueprint, PropertyName_DEPRECATED, this);
			}
		}
		}

	UEdGraphNode::DestroyNode();
}

void UControlRigGraphNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	CopyPinDefaultsToModel(Pin, true);
}

void UControlRigGraphNode::CopyPinDefaultsToModel(UEdGraphPin* Pin, bool bUndo)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Pin->Direction != EGPD_Input)
	{
		return;
		}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (URigVMPin* ModelPin = GetModelPinFromPinPath(Pin->GetName()))
	{
		if (ModelPin->GetSubPins().Num() > 0)
		{
			return;
	}

		FString DefaultValue = Pin->DefaultValue;
		if (DefaultValue == FName(NAME_None).ToString() && Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Name)
		{
			DefaultValue = FString();
	}

		if (ModelPin->GetDefaultValue() != DefaultValue)
				{
			GetBlueprint()->Controller->SetPinDefaultValue(ModelPin->GetPinPath(), DefaultValue, false, true, false);
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

URigVMNode* UControlRigGraphNode::GetModelNode() const
{
	UControlRigGraphNode* MutableThis = (UControlRigGraphNode*)this;
	if (CachedModelNode)
	{
		if (CachedModelNode->GetOuter() == GetTransientPackage())
		{
			MutableThis->CachedModelNode = nullptr;
		}
		else
		{
			return CachedModelNode;
		}
	}

	if (UControlRigGraph* Graph = Cast<UControlRigGraph>(GetOuter()))
	{
#if WITH_EDITOR

		if (Graph->TemplateModel != nullptr)
		{
			return MutableThis->CachedModelNode = Graph->TemplateModel->FindNode(ModelNodePath);
		}

#endif

		if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter()))
		{
			if (URigVMGraph* Model = Blueprint->Model)
			{
				return MutableThis->CachedModelNode = Model->FindNode(ModelNodePath);
			}
		}
	}

	return nullptr;
}

FName UControlRigGraphNode::GetModelNodeName() const
{
	if (URigVMNode* ModelNode = GetModelNode())
	{
		return ModelNode->GetFName();
	}
	return NAME_None;
}

URigVMPin* UControlRigGraphNode::GetModelPinFromPinPath(const FString& InPinPath) const
{
	if (URigVMPin*const* CachedModelPinPtr = CachedModelPins.Find(InPinPath))
	{
		URigVMPin* CachedModelPin = *CachedModelPinPtr;
		if (!CachedModelPin->HasAnyFlags(RF_Transient) && CachedModelPin->GetNode())
		{
			return CachedModelPin;
		}
	}

	if (URigVMNode* ModelNode = GetModelNode())
	{
		FString PinPath = InPinPath.RightChop(ModelNode->GetNodePath().Len() + 1);
		URigVMPin* ModelPin = ModelNode->FindPin(PinPath);
		if (ModelPin)
	{
			UControlRigGraphNode* MutableThis = (UControlRigGraphNode*)this;
			MutableThis->CachedModelPins.Add(InPinPath, ModelPin);
	}
		return ModelPin;
	}
	
	return nullptr;
}

void UControlRigGraphNode::SetupPinDefaultsFromModel(UEdGraphPin* Pin)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (Pin->Direction != EGPD_Input)
	{
		return;
		}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (URigVMPin* ModelPin = GetModelPinFromPinPath(Pin->GetName()))
	{
		if (ModelPin->GetSubPins().Num() > 0)
		{
			return;
			}

		FString DefaultValueString = ModelPin->GetDefaultValue();
		if (DefaultValueString.IsEmpty() && ModelPin->GetCPPType() == TEXT("FName"))
	{
			DefaultValueString = FName(NAME_None).ToString();
	}
						K2Schema->GetPinDefaultValuesFromString(Pin->PinType, Pin->GetOwningNodeUnchecked(), DefaultValueString, Pin->DefaultValue, Pin->DefaultObject, Pin->DefaultTextValue);
					}
}

FText UControlRigGraphNode::GetTooltipText() const
{
	if(URigVMNode* ModelNode = GetModelNode())
	{
		return ModelNode->GetToolTipText();
	}
	return FText::FromString(ModelNodePath);
}

void UControlRigGraphNode::InvalidateNodeTitle() const
{
	NodeTitle = FText();

	NodeTitleDirtied.ExecuteIfBound();
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
			if (Schema->TryCreateConnection(FromPin, Pin))
			{
				break;
					}
				}
		}
}

bool UControlRigGraphNode::IsSelectedInEditor() const
{
	URigVMNode* ModelNode = GetModelNode();
	if (ModelNode)
	{
		return ModelNode->IsSelected();
	}
	return false;
}

bool UControlRigGraphNode::ShouldDrawNodeAsControlPointOnly(int32& OutInputPinIndex, int32& OutOutputPinIndex) const
{
	if (URigVMRerouteNode* Reroute = Cast<URigVMRerouteNode>(GetModelNode()))
	{
		if (!Reroute->GetShowsAsFullNode())
	{
			if (Pins.Num() >= 2)
			{
				OutInputPinIndex = 0;
				OutOutputPinIndex = 1;
				return true;
			}
	}
	}
	return false;
}

FEdGraphPinType UControlRigGraphNode::GetPinTypeForModelPin(URigVMPin* InModelPin)
{
	FEdGraphPinType PinType;
	PinType.ResetToDefaults();

	FName ModelPinCPPType = InModelPin->IsArray() ? *InModelPin->GetArrayElementCppType() : *InModelPin->GetCPPType();
	if (ModelPinCPPType == TEXT("bool"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (ModelPinCPPType == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (ModelPinCPPType == TEXT("float"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (ModelPinCPPType == TEXT("FName"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (ModelPinCPPType == TEXT("FString"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (InModelPin->GetScriptStruct() != nullptr)
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = InModelPin->GetScriptStruct();
	}
	else if (InModelPin->GetEnum() != nullptr)
		{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		PinType.PinSubCategoryObject = InModelPin->GetEnum();
		}

	if (InModelPin->IsArray())
		{
		PinType.ContainerType = EPinContainerType::Array;
		}
	else
	{
		PinType.ContainerType = EPinContainerType::None;
	}

	PinType.bIsConst = InModelPin->IsDefinedAsConstant();

	return PinType;
}

#undef LOCTEXT_NAMESPACE
