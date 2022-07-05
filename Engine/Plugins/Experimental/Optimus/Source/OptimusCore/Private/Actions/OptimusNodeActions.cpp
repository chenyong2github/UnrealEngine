// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeActions.h"

#include "IOptimusPathResolver.h"
#include "OptimusHelpers.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "IOptimusNodeAdderPinProvider.h"
#include "OptimusDataTypeRegistry.h"


FOptimusNodeAction_RenameNode::FOptimusNodeAction_RenameNode(
	UOptimusNode* InNode, 
	FString InNewName
	)
{
	NodePath = InNode->GetNodePath();
	NewName = FText::FromString(InNewName);
	OldName = InNode->GetDisplayName();

	SetTitlef(TEXT("Rename %s"), *InNode->GetDisplayName().ToString());
}


bool FOptimusNodeAction_RenameNode::Do(IOptimusPathResolver* InRoot)
{
	UOptimusNode *Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	Node->SetDisplayName(NewName);

	return true;
}


bool FOptimusNodeAction_RenameNode::Undo(IOptimusPathResolver* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	Node->SetDisplayName(OldName);

	return true;
}


FOptimusNodeAction_MoveNode::FOptimusNodeAction_MoveNode(
	UOptimusNode* InNode,
	const FVector2D& InPosition
)
{
	NodePath = InNode->GetNodePath();
	NewPosition = InPosition;
	OldPosition = InNode->GetGraphPosition();
}

bool FOptimusNodeAction_MoveNode::Do(IOptimusPathResolver* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	return Node->SetGraphPositionDirect(NewPosition);
}

bool FOptimusNodeAction_MoveNode::Undo(IOptimusPathResolver* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	return Node->SetGraphPositionDirect(OldPosition);
}


FOptimusNodeAction_SetPinValue::FOptimusNodeAction_SetPinValue(
	UOptimusNodePin* InPin, 
	const FString& InNewValue
	)
{
	if (ensure(InPin) && InPin->GetSubPins().IsEmpty())
	{
		PinPath = InPin->GetPinPath();
		OldValue = InPin->GetValueAsString();
		NewValue = InNewValue;

		SetTitlef(TEXT("Set Value %s"), *InPin->GetPinPath());
	}
}


bool FOptimusNodeAction_SetPinValue::Do(IOptimusPathResolver* InRoot)
{
	UOptimusNodePin* Pin = InRoot->ResolvePinPath(PinPath);
	if (!Pin)
	{
		return false;
	}

	return Pin->SetValueFromStringDirect(NewValue);
}


bool FOptimusNodeAction_SetPinValue::Undo(IOptimusPathResolver* InRoot)
{
	UOptimusNodePin* Pin = InRoot->ResolvePinPath(PinPath);
	if (!Pin)
	{
		return false;
	}

	return Pin->SetValueFromStringDirect(OldValue);
}


FOptimusNodeAction_SetPinName::FOptimusNodeAction_SetPinName(
	UOptimusNodePin* InPin,
	FName InPinName
	)
{
	if (ensure(InPin))
	{
		PinPath = InPin->GetPinPath();
		NewPinName = InPinName;
		OldPinName = InPin->GetFName();
	}
}


bool FOptimusNodeAction_SetPinName::Do(IOptimusPathResolver* InRoot)
{
	return SetPinName(InRoot, NewPinName);
}


bool FOptimusNodeAction_SetPinName::Undo(IOptimusPathResolver* InRoot)
{
	return SetPinName(InRoot, OldPinName);
}


bool FOptimusNodeAction_SetPinName::SetPinName(
	IOptimusPathResolver* InRoot, 
	FName InName
	)
{
	UOptimusNodePin *Pin = InRoot->ResolvePinPath(PinPath);

	if (!Pin)
	{
		return false;
	}
	
	if (!Pin->GetOwningNode()->SetPinNameDirect(Pin, InName))
	{
		return false;
	}

	PinPath = Pin->GetPinPath();
	return true;
}


FOptimusNodeAction_SetPinType::FOptimusNodeAction_SetPinType(
	UOptimusNodePin* InPin,
	FOptimusDataTypeRef InDataType
	)
{
	if (ensure(InPin))
	{
		PinPath = InPin->GetPinPath();
		NewDataTypeName = InDataType.TypeName;
		OldDataTypeName = InPin->GetDataType()->TypeName;
	}
}


bool FOptimusNodeAction_SetPinType::Do(IOptimusPathResolver* InRoot)
{
	return SetPinType(InRoot, NewDataTypeName);
}


bool FOptimusNodeAction_SetPinType::Undo(IOptimusPathResolver* InRoot)
{
	return SetPinType(InRoot, OldDataTypeName);
}


bool FOptimusNodeAction_SetPinType::SetPinType(
	IOptimusPathResolver* InRoot,
	FName InDataType
	) const
{
	UOptimusNodePin *Pin = InRoot->ResolvePinPath(PinPath);

	if (!Pin)
	{
		return false;
	}

	FOptimusDataTypeRef DataType(FOptimusDataTypeRegistry::Get().FindType(InDataType));
	
	return Pin->GetOwningNode()->SetPinDataTypeDirect(Pin, DataType);
}


FOptimusNodeAction_SetPinDataDomain::FOptimusNodeAction_SetPinDataDomain(
	UOptimusNodePin* InPin,
	const TArray<FName>& InContextNames
	)
{
	if (ensure(InPin))
	{
		PinPath = InPin->GetPinPath();
		NewContextNames = InContextNames;
		OldContextNames = InPin->GetDataDomainLevelNames();
	}
}


bool FOptimusNodeAction_SetPinDataDomain::Do(IOptimusPathResolver* InRoot)
{
	return SetPinDataDomain(InRoot, NewContextNames);
}


bool FOptimusNodeAction_SetPinDataDomain::Undo(IOptimusPathResolver* InRoot)
{
	return SetPinDataDomain(InRoot, OldContextNames);
}


bool FOptimusNodeAction_SetPinDataDomain::SetPinDataDomain(
	IOptimusPathResolver* InRoot,
	const TArray<FName>& InContextNames
	) const
{
	UOptimusNodePin *Pin = InRoot->ResolvePinPath(PinPath);

	if (!Pin)
	{
		return false;
	}

	return Pin->GetOwningNode()->SetPinDataDomainDirect(Pin, InContextNames);
}


FOptimusNodeAction_ConnectAdderPin::FOptimusNodeAction_ConnectAdderPin(
	IOptimusNodeAdderPinProvider* InAdderPinProvider,
	UOptimusNodePin* InSourcePin,
	FName InNewPinName)
{
	UOptimusNode* Node = Cast<UOptimusNode>(InAdderPinProvider);
	
	if (ensure(Node))
	{
		NodePath = Node->GetNodePath();
	}

	SourcePinPath = InSourcePin->GetPinPath();

	NewPinName = InNewPinName;
}

bool FOptimusNodeAction_ConnectAdderPin::Do(IOptimusPathResolver* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	IOptimusNodeAdderPinProvider* AdderPinProvider = Cast<IOptimusNodeAdderPinProvider>(Node);
	
	if (!ensure(AdderPinProvider))
	{
		return false;
	}

	UOptimusNodePin* SourcePin = InRoot->ResolvePinPath(SourcePinPath);
	
	const UOptimusNodePin* NewPin = AdderPinProvider->TryAddPinFromPin(SourcePin, NewPinName);

	if (!NewPin)
	{
		return false;
	}
	
	NewPinPath = NewPin->GetPinPath();

	return true;
}

bool FOptimusNodeAction_ConnectAdderPin::Undo(IOptimusPathResolver* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	IOptimusNodeAdderPinProvider* AdderPinProvider = Cast<IOptimusNodeAdderPinProvider>(Node);
	
	if (!ensure(AdderPinProvider))
	{
		return false;
	}

	UOptimusNodePin* NewPin = InRoot->ResolvePinPath(NewPinPath);

	return AdderPinProvider->RemoveAddedPin(NewPin);
}

FOptimusNodeAction_AddRemovePin::FOptimusNodeAction_AddRemovePin(
	UOptimusNode* InNode,
	FName InName,
	EOptimusNodePinDirection InDirection,
	FOptimusNodePinStorageConfig InStorageConfig,
	FOptimusDataTypeRef InDataType,
	UOptimusNodePin* InBeforePin
	)
{
	if (ensure(InNode) &&
		ensure(!InBeforePin || InBeforePin->GetOwningNode() == InNode) &&
		ensure(!InBeforePin || InBeforePin->GetParentPin() == nullptr))
	{
		NodePath = InNode->GetNodePath();
		PinName = InName;
		Direction = InDirection;
		StorageConfig = InStorageConfig;
		DataType = InDataType.TypeName;

		// New pins are always created in a non-expanded state.
		bExpanded = false;

		if (InBeforePin)
		{
			BeforePinPath = InBeforePin->GetPinPath();
		}
	}
}


FOptimusNodeAction_AddRemovePin::FOptimusNodeAction_AddRemovePin(UOptimusNodePin* InPin)
{
	if (ensure(InPin))
	{
		NodePath = InPin->GetOwningNode()->GetNodePath();
		PinPath = InPin->GetPinPath();
		PinName = InPin->GetFName();
		Direction = InPin->GetDirection();
		if (InPin->GetStorageType() == EOptimusNodePinStorageType::Resource)
		{
			StorageConfig = FOptimusNodePinStorageConfig(InPin->GetDataDomainLevelNames());
		}
		else
		{
			StorageConfig = FOptimusNodePinStorageConfig();
		}
		DataType = InPin->GetDataType()->TypeName;

		// Store the expansion info.
		bExpanded = InPin->GetIsExpanded();

		// Capture the before pin.
		const TArray<UOptimusNodePin*>& Pins = InPin->GetOwningNode()->GetPins();
		const int32 PinIndex = Pins.IndexOfByKey(InPin);
		if (ensure(Pins.IsValidIndex(PinIndex)))
		{
			// If it's not the last, then grab the path of the next pin.
			if (PinIndex < (Pins.Num() - 1))
			{
				BeforePinPath = Pins[PinIndex + 1]->GetPinPath();
			}
		}
	}
}


bool FOptimusNodeAction_AddRemovePin::AddPin(IOptimusPathResolver* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	FOptimusDataTypeRef TypeRef;
	TypeRef = FOptimusDataTypeRegistry::Get().FindType(DataType);

	UOptimusNodePin *BeforePin = nullptr;
	if (!BeforePinPath.IsEmpty())
	{
		BeforePin = InRoot->ResolvePinPath(BeforePinPath);
		if (!BeforePin)
		{
			return false;
		}
	}
	
	UOptimusNodePin *Pin = Node->AddPinDirect(PinName, Direction, StorageConfig, TypeRef, BeforePin);
	if (!Pin)
	{
		return false;
	}

	Pin->SetIsExpanded(bExpanded);

	PinName = Pin->GetFName();
	PinPath = Pin->GetPinPath();

	return true;
}


bool FOptimusNodeAction_AddRemovePin::RemovePin(IOptimusPathResolver* InRoot) const
{
	UOptimusNodePin *Pin = InRoot->ResolvePinPath(PinPath);
	if (!Pin)
	{
		return false;
	}
	UOptimusNode *Node = Pin->GetOwningNode();

	return Node->RemovePinDirect(Pin);
}


UOptimusNodePin* FOptimusNodeAction_AddPin::GetPin(IOptimusPathResolver* InRoot) const
{
	return InRoot->ResolvePinPath(PinPath);
}
