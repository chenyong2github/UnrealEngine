// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode.h"

#include "Actions/OptimusNodeActions.h"
#include "OptimusActionStack.h"
#include "OptimusDeveloperModule.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformer.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"

#include "Algo/Reverse.h"
#include "UObject/UObjectIterator.h"
#include "Misc/StringBuilder.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"


const FName UOptimusNode::CategoryName::Attributes("Attributes");
const FName UOptimusNode::CategoryName::Meshes("Meshes");
const FName UOptimusNode::CategoryName::Deformers("Deformers");
const FName UOptimusNode::CategoryName::Resources("Resources");
const FName UOptimusNode::CategoryName::Variables("Variables");

const FName UOptimusNode::PropertyMeta::Input("Input");
const FName UOptimusNode::PropertyMeta::Output("Output");
const FName UOptimusNode::PropertyMeta::Resource("Resource");


// Cached list of node classes
TArray<UClass*> UOptimusNode::CachedNodesClasses;


UOptimusNode::UOptimusNode()
{
	// TODO: Clean up properties (i.e. remove EditAnywhere, VisibleAnywhere for outputs).
}


FName UOptimusNode::GetNodeName() const
{
	return GetClass()->GetFName();
}


FText UOptimusNode::GetDisplayName() const
{
	if (DisplayName.IsEmpty())
	{
		FString Name = GetNodeName().ToString();
		FString PackageName, NodeName;

		if (!Name.Split("_", &PackageName, &NodeName))
		{
			NodeName = Name;
		}

		// Try to make the name a bit prettier.
		return FText::FromString(FName::NameToDisplayString(NodeName, false));
	}

	return DisplayName;
}


bool UOptimusNode::SetDisplayName(FText InDisplayName)
{
	if (DisplayName.EqualTo(InDisplayName))
	{
		return false;
	}
	
	DisplayName = InDisplayName;

	Notify(EOptimusGraphNotifyType::NodeDisplayNameChanged);

	return true;
}



bool UOptimusNode::SetGraphPosition(const FVector2D& InPosition)
{
	return GetActionStack()->RunAction<FOptimusNodeAction_MoveNode>(this, InPosition);
}


bool UOptimusNode::SetGraphPositionDirect(
	const FVector2D& InPosition,
	bool bInNotify
	)
{
	if (InPosition.ContainsNaN() || InPosition.Equals(GraphPosition))
	{
		return false;
	}

	GraphPosition = InPosition;

	if (bInNotify)
	{
		Notify(EOptimusGraphNotifyType::NodePositionChanged);
	}

	return true;
}


FString UOptimusNode::GetNodePath() const
{
	UOptimusNodeGraph* Graph = GetOwningGraph();
	FString GraphPath(TEXT("<Unknown>"));
	if (Graph)
	{
		GraphPath = Graph->GetGraphPath();
	}

	return FString::Printf(TEXT("%s/%s"), *GraphPath, *GetName());
}


UOptimusNodeGraph* UOptimusNode::GetOwningGraph() const
{
	return Cast<UOptimusNodeGraph>(GetOuter());
}


UOptimusNodePin* UOptimusNode::FindPin(const FString& InPinPath) const
{
	TArray<FName> PinPath = UOptimusNodePin::GetPinNamePathFromString(InPinPath);
	if (PinPath.IsEmpty())
	{
		return nullptr;
	}

	return FindPinFromPath(PinPath);
}


UOptimusNodePin* UOptimusNode::FindPinFromPath(const TArray<FName>& InPinPath) const
{
	UOptimusNodePin* const* PinPtrPtr = CachedPinLookup.Find(InPinPath);
	if (PinPtrPtr)
	{
		return *PinPtrPtr;
	}

	const TArray<UOptimusNodePin*>* CurrentPins = &Pins;
	int PathIndex = 0;
	UOptimusNodePin* FoundPin = nullptr;

	for (FName PinName : InPinPath)
	{
		if (CurrentPins == nullptr || CurrentPins->IsEmpty())
		{
			FoundPin = nullptr;
			break;
		}

		UOptimusNodePin* const* FoundPinPtr = CurrentPins->FindByPredicate(
		    [&PinName](const UOptimusNodePin* Pin) {
			    return Pin->GetFName() == PinName;
		    });

		if (FoundPinPtr == nullptr)
		{
			FoundPin = nullptr;
			break;
		}

		FoundPin = *FoundPinPtr;
		CurrentPins = &FoundPin->GetSubPins();
	}

	CachedPinLookup.Add(InPinPath, FoundPin);

	return FoundPin;
}


UOptimusNodePin* UOptimusNode::FindPinFromProperty(
	const FProperty* InRootProperty,
	const FProperty* InSubProperty
	) const
{
	TArray<FName> PinPath;

	// This feels quite icky.
	if (InRootProperty == InSubProperty || InSubProperty == nullptr)
	{
		PinPath.Add(InRootProperty->GetFName());
	}
	else if (const FStructProperty* StructProp = CastField<const FStructProperty>(InRootProperty))
	{
		const UStruct *Struct = StructProp->Struct;

		// Crawl up the property hierarchy until we hit the root prop UStruct.
		while (ensure(InSubProperty))
		{
			PinPath.Add(InSubProperty->GetFName());

			if (const UStruct *OwnerStruct = InSubProperty->GetOwnerStruct())
			{
				if (ensure(OwnerStruct == Struct))
				{
					PinPath.Add(InRootProperty->GetFName());
					break;
				}
				else
				{
					return nullptr;
				}
			}
			else
			{
				InSubProperty = InSubProperty->GetOwner<const FProperty>();
			}
		}

		Algo::Reverse(PinPath);
	}

	return FindPinFromPath(PinPath);
}


TArray<UClass*> UOptimusNode::GetAllNodeClasses()
{
	if (CachedNodesClasses.IsEmpty())
	{
		UClass* ClassType = UOptimusNode::StaticClass();

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) &&
				Class->IsChildOf(UOptimusNode::StaticClass()))
			{
				CachedNodesClasses.Add(Class);
			}
		}
	}
	return CachedNodesClasses;
}


void UOptimusNode::PostCreateNode()
{
	CachedPinLookup.Empty();
	Pins.Empty();
	CreatePins();
}


void UOptimusNode::Notify(EOptimusGraphNotifyType InNotifyType)
{
	UOptimusNodeGraph *Graph = Cast<UOptimusNodeGraph>(GetOuter());

	if (Graph)
	{
		Graph->Notify(InNotifyType, this);
	}
}



void UOptimusNode::CreatePins()
{
	CreatePinsFromStructLayout(GetClass(), nullptr);
}


UOptimusNodePin* UOptimusNode::CreatePinFromDataType(
    FName InName,
    EOptimusNodePinDirection InDirection,
    EOptimusNodePinStorageType InStorageType,
    FOptimusDataTypeRef InDataType,
    UOptimusNodePin* InParentPin
	)
{
	UObject* PinParent = InParentPin ? Cast<UObject>(InParentPin) : this;
	UOptimusNodePin* Pin = NewObject<UOptimusNodePin>(PinParent, InName);

	Pin->Initialize(InDirection, InStorageType, InDataType);

	if (InParentPin)
	{
		InParentPin->AddSubPin(Pin);
	}
	else
	{
		Pins.Add(Pin);
	}

	// Add sub-pins, if the registered type is set to show them but only for value types.
	if (InStorageType == EOptimusNodePinStorageType::Value &&
		EnumHasAnyFlags(InDataType->TypeFlags, EOptimusDataTypeFlags::ShowElements))
	{
		if (const UScriptStruct* Struct = Cast<const UScriptStruct>(InDataType->TypeObject))
		{
			CreatePinsFromStructLayout(Struct, Pin);
		}
	}

	return Pin;
}


void UOptimusNode::SetPinExpanded(const UOptimusNodePin* InPin, bool bInExpanded)
{
	FName Name = InPin->GetUniqueName();
	if (bInExpanded)
	{
		ExpandedPins.Add(Name);
	}
	else
	{
		ExpandedPins.Remove(Name);
	}
}


bool UOptimusNode::GetPinExpanded(const UOptimusNodePin* InPin) const
{
	return ExpandedPins.Contains(InPin->GetUniqueName());
}


void UOptimusNode::CreatePinsFromStructLayout(
	const UStruct* InStruct, 
	UOptimusNodePin* InParentPin
	)
{
	for (const FProperty* Property : TFieldRange<FProperty>(InStruct))
	{
		if (InParentPin)
		{
			// Sub-pins keep the same direction as the parent.
			CreatePinFromProperty(InParentPin->GetDirection(), Property, InParentPin);
		}
		else if (Property->HasMetaData(PropertyMeta::Input))
		{
			if (Property->HasMetaData(PropertyMeta::Output))
			{
				UE_LOG(LogOptimusDeveloper, Error, TEXT("Pin on %s.%s marked both input and output. Ignoring it as output."),
					*GetName(), *Property->GetName());
			}

			CreatePinFromProperty(EOptimusNodePinDirection::Input, Property, InParentPin);
		}
		else if (Property->HasMetaData(PropertyMeta::Output))
		{
			CreatePinFromProperty(EOptimusNodePinDirection::Output, Property, InParentPin);
		}
	}
}


UOptimusNodePin* UOptimusNode::CreatePinFromProperty(
    EOptimusNodePinDirection InDirection,
	const FProperty* InProperty,
	UOptimusNodePin* InParentPin
	)
{
	if (!ensure(InProperty))
	{
		return nullptr;
	}

	// Is this a legitimate type for pins?
	const FOptimusDataTypeRegistry& Registry = FOptimusDataTypeRegistry::Get();

	FOptimusDataTypeHandle DataType = Registry.FindType(*InProperty);

	if (!DataType.IsValid())
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("No registered type found for pin '%s'."), *InProperty->GetName());
		return nullptr;
	}

	EOptimusNodePinStorageType StorageType = EOptimusNodePinStorageType::Value;
	if (InProperty->HasMetaData(PropertyMeta::Resource))
	{
		if (!ensure(!InParentPin))
		{
			UE_LOG(LogOptimusDeveloper, Error, TEXT("Pin '%s' marked as resource cannot have sub-pins."), *InProperty->GetName());
			return nullptr;
		}

		// Ensure that the data type for the property allows it to be used as a resource.
		if (!EnumHasAnyFlags(DataType->UsageFlags, EOptimusDataTypeUsageFlags::Resource))
		{
			UE_LOG(LogOptimusDeveloper, Error, TEXT("Pin '%s' marked as resource but data type is not compatible."), *InProperty->GetName());
			return nullptr;
		}

		StorageType = EOptimusNodePinStorageType::Resource;
	}


	return CreatePinFromDataType(InProperty->GetFName(), InDirection, StorageType, DataType, InParentPin);
}

UOptimusActionStack* UOptimusNode::GetActionStack() const
{
	UOptimusNodeGraph *Graph = GetOwningGraph();
	if (Graph == nullptr)
	{
		return nullptr;
	}
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(Graph->GetOuter());
	if (!Deformer)
	{
		return nullptr;
	}

	return Deformer->GetActionStack();
}
