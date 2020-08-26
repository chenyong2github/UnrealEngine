// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodePin.h"

#include "Actions/OptimusNodeActions.h"
#include "OptimusActionStack.h"
#include "OptimusHelpers.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"

#include "UObject/Package.h"

static UOptimusNodePin* InvalidPin = nullptr;


UOptimusNodePin* UOptimusNodePin::GetParentPin()
{
	return Cast<UOptimusNodePin>(GetOuter());
}

const UOptimusNodePin* UOptimusNodePin::GetParentPin() const
{
	return Cast<const UOptimusNodePin>(GetOuter());
}


UOptimusNodePin* UOptimusNodePin::GetRootPin()
{
	UOptimusNodePin* CurrentPin = this;
	while (UOptimusNodePin* ParentPin = GetParentPin())
	{
		CurrentPin = ParentPin;
	}
	return CurrentPin;
}

const UOptimusNodePin* UOptimusNodePin::GetRootPin() const
{
	const UOptimusNodePin* CurrentPin = this;
	while (const UOptimusNodePin* ParentPin = GetParentPin())
	{
		CurrentPin = ParentPin;
	}
	return CurrentPin;
}

// FIXME: Rename to GetOwningNode
UOptimusNode* UOptimusNodePin::GetNode() const
{
	const UOptimusNodePin* RootPin = GetRootPin();
	return Cast<UOptimusNode>(RootPin->GetOuter());
}

TArray<FName> UOptimusNodePin::GetPinNamePath() const
{
	TArray<const UOptimusNodePin*> Nodes;
	Nodes.Reserve(4);
	const UOptimusNodePin* CurrentPin = this;
	while (CurrentPin)
	{
		Nodes.Add(CurrentPin);
		CurrentPin = CurrentPin->GetParentPin();
	}

	TArray<FName> Path;
	Path.Reserve(Nodes.Num());

	for (int32 i = Nodes.Num(); i-- > 0; /**/)
	{
		Path.Add(Nodes[i]->GetFName());
	}

	return Path;
}


FName UOptimusNodePin::GetUniqueName() const
{
	return *FString::JoinBy(GetPinNamePath(), TEXT("."), [](const FName& N) { return N.ToString(); });
}


FString UOptimusNodePin::GetPinPath() const
{
	return FString::Printf(TEXT("%s.%s"), *GetNode()->GetNodePath(), *GetUniqueName().ToString());
}


TArray<FName> UOptimusNodePin::GetPinNamePathFromString(const FString& PinPathString)
{
	// FIXME: This should really become a part of FStringView, or a shared algorithm.
	TArray<FStringView, TInlineAllocator<4>> PinPathParts;
	FStringView PinPathView(PinPathString);

	int32 Index = INDEX_NONE;
	while(PinPathView.FindChar(TCHAR('.'), Index))
	{
		if (Index > 0)
		{
			PinPathParts.Add(PinPathView.Mid(0, Index));
		}
		PinPathView = PinPathView.Mid(Index + 1);
	}
	if (!PinPathView.IsEmpty())
	{
		PinPathParts.Add(PinPathView);
	}

	TArray<FName> PinPath;
	PinPath.Reserve(PinPathParts.Num());
	for (FStringView PinPathPart : PinPathParts)
	{
		// Don't add names, just return a NAME_None.
		PinPath.Emplace(PinPathPart, FNAME_Find);
	}
	return PinPath;
}


UObject* UOptimusNodePin::GetTypeObject() const
{
	// A sentinel object to mark a pointer as "invalid but don't try searching again".
	UObject* SentinelObject = GetClass()->GetDefaultObject();

	if (TypeObject == nullptr && !TypeObjectPath.IsEmpty())
	{
		TypeObject = Optimus::FindObjectInPackageOrGlobal<UObject>(TypeObjectPath);

		if (TypeObject == nullptr)
		{
			// Use the CDO as a sentinel to indicate that the object was not found and we should
			// not try to search again.
			TypeObject = SentinelObject;
		}
	}

	
	if (TypeObject == SentinelObject)
	{
		// We tried to find it before but failed, so don't attempt again to avoid repeated
		// useless resolves.
		return nullptr;
	}
	else
	{
		return TypeObject;
	}
}


FProperty* UOptimusNodePin::GetPropertyFromPin() const
{
	UStruct *ScopeStruct = GetNode()->GetClass();
	TArray<FName> NamePath = GetPinNamePath();

	FProperty* Property = nullptr;
	for (int32 Index = 0; Index < NamePath.Num(); Index++)
	{
		Property = ScopeStruct->FindPropertyByName(NamePath[Index]);
		if (!Property)
		{
			return nullptr;
		}

		if (Index == (NamePath.Num() - 1))
		{
			break;
		}

		FStructProperty *StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty)
		{
			return nullptr;
		}

		ScopeStruct = StructProperty->Struct;
	}

	return Property;
}


FString UOptimusNodePin::GetValueAsString() const
{
	UObject *NodeObject = GetNode();
	const FProperty *Property = GetPropertyFromPin();

	FString ValueString;
	if (ensure(Property))
	{
		const uint8 *NodeData = Property->ContainerPtrToValuePtr<const uint8>(NodeObject);
		Property->ExportTextItem(ValueString, NodeData, nullptr, NodeObject, PPF_None);
	}

	return ValueString;
}



bool UOptimusNodePin::SetValueFromString(const FString& InStringValue)
{
	return GetActionStack()->RunAction<FOptimusNodeAction_SetPinValue>(this, InStringValue);
}


bool UOptimusNodePin::SetValueFromStringDirect(const FString& InStringValue)
{
	UOptimusNode* Node = GetNode();
	FProperty* Property = GetPropertyFromPin();
	bool bSuccess = false;

	if (ensure(Property))
	{
		FEditPropertyChain PropertyChain;
		PropertyChain.AddHead(Property);
		Node->PreEditChange(PropertyChain);

		// FIXME: We need a way to sanitize the input. Trying and failing is not good, since
		// it's unknown whether this may leave the property in an indeterminate state.
		uint8 *NodeData = Property->ContainerPtrToValuePtr<uint8>(Node);
		bSuccess = Property->ImportText(*InStringValue, NodeData, PPF_None, Node) != nullptr;

		// We notify that the value change occurred, whether that's true or not. This way
		// the graph pin value sync will ensure that if an invalid value was entered, it will
		// get reverted back to the true value.
		FPropertyChangedEvent ChangedEvent(Property);
		Node->PostEditChangeProperty(ChangedEvent);

		Notify(EOptimusNodeGraphNotifyType::PinValueChanged);
	}

	return bSuccess;
}


bool UOptimusNodePin::CanCannect(const UOptimusNodePin* InOtherPin, FString* OutReason) const
{
	if (!ensure(InOtherPin))
	{
		if (OutReason)
		{
			*OutReason = TEXT("No pin given.");
		}
		return false;
	}

	if (Direction == InOtherPin->GetDirection())
	{
		if (OutReason)
		{
			*OutReason = FString::Printf(TEXT("Can't connect an %1$s pin to a %1$s"), 
				Direction == EOptimusNodePinDirection::Input ? TEXT("input") : TEXT("output"));
		}
		return false;
	}

	// Check for self-connect.
	if (GetNode() == InOtherPin->GetNode())
	{
		if (OutReason)
		{
			*OutReason = TEXT("Can't connect input and output pins on the same node.");
		}
		return false;
	}

	if (GetNode()->GetOwningGraph() != InOtherPin->GetNode()->GetOwningGraph())
	{
		if (OutReason)
		{
			*OutReason = TEXT("Pins belong to nodes from two different graphs.");
		}
		return false;
	}

	// Check for incompatible types.
	if (TypeName != InOtherPin->GetTypeName())
	{
		// TBD: Automatic conversion.
		if (OutReason)
		{
			*OutReason = TEXT("Incompatible pin types.");
		}
		return false;
	}

	// Will this connection cause a cycle?
	const UOptimusNodePin *OutputPin = Direction == EOptimusNodePinDirection::Output ? this : InOtherPin;
	const UOptimusNodePin* InputPin = Direction == EOptimusNodePinDirection::Input ? this : InOtherPin;

	if (GetNode()->GetOwningGraph()->DoesLinkFormCycle(OutputPin, InputPin))
	{
		if (OutReason)
		{
			*OutReason = TEXT("Connection would form a graph cycle.");
		}
		return false;
	}

	return true;
}


void UOptimusNodePin::InitializeFromProperty(
	EOptimusNodePinDirection InDirection, 
	const FProperty *InProperty
	)
{
	Direction = InDirection;

	FString ExtendedType;
	FString TypeString = InProperty->GetCPPType(&ExtendedType);
	if (!ExtendedType.IsEmpty())
	{
		TypeString += ExtendedType;
	}

	TypeName = *TypeString;

	const FProperty* PropertyForType = InProperty;
	const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(PropertyForType);
	if (ArrayProperty)
	{
		PropertyForType = ArrayProperty->Inner;
	}

	if (const FStructProperty* StructProperty = CastField<const FStructProperty>(PropertyForType))
	{
		TypeObject = StructProperty->Struct->GetClass();
	}
	else if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(PropertyForType))
	{
		TypeObject = EnumProperty->GetEnum()->GetClass();
	}
	else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(PropertyForType))
	{
		TypeObject = ByteProperty->Enum->GetClass();
	}

	if (TypeObject)
	{
		// Store this so that we can restore the type object on load/undo.
		TypeObjectPath = TypeObject->GetPathName();
	}
}


void UOptimusNodePin::AddSubPin(UOptimusNodePin* InSubPin)
{
	SubPins.Add(InSubPin);
}


void UOptimusNodePin::Notify(EOptimusNodeGraphNotifyType InNotifyType)
{
	UOptimusNodeGraph *Graph = GetNode()->GetOwningGraph();

	Graph->Notify(InNotifyType, this);
}


UOptimusActionStack* UOptimusNodePin::GetActionStack() const
{
	return GetNode()->GetActionStack();
}
