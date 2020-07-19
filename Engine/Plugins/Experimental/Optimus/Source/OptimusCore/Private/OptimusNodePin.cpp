// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodePin.h"

#include "OptimusHelpers.h"
#include "OptimusNode.h"

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


UOptimusNode* UOptimusNodePin::GetNode()
{
	UOptimusNodePin* RootPin = GetRootPin();
	return Cast<UOptimusNode>(RootPin->GetOuter());
}

const UOptimusNode* UOptimusNodePin::GetNode() const
{
	const UOptimusNodePin* RootPin = GetRootPin();
	return Cast<const UOptimusNode>(RootPin->GetOuter());
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


void UOptimusNodePin::InitializeFromProperty(
	EOptimusNodePinDirection InDirection, 
	const FProperty *InProperty
	)
{
	Direction = InDirection;

	FString ExtendedType;
	TypeString = InProperty->GetCPPType(&ExtendedType);
	TypeString += ExtendedType;

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

