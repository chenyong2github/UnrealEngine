// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode.h"

#include "Actions/OptimusNodeActions.h"
#include "OptimusActionStack.h"
#include "OptimusDataDomain.h"
#include "OptimusCoreModule.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusDeformer.h"
#include "OptimusDiagnostic.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "OptimusObjectVersion.h"
#include "Actions/OptimusNodeGraphActions.h"

#include "Algo/Reverse.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"


const FName UOptimusNode::CategoryName::DataProviders("Data Providers");
const FName UOptimusNode::CategoryName::Deformers("Deformers");
const FName UOptimusNode::CategoryName::Resources("Resources");
const FName UOptimusNode::CategoryName::Variables("Variables");
const FName UOptimusNode::CategoryName::Values("Values");

// NOTE: There really should be a central place for these. Magic strings are _bad_.
const FName UOptimusNode::PropertyMeta::Category("Category");
const FName UOptimusNode::PropertyMeta::Input("Input");
const FName UOptimusNode::PropertyMeta::Output("Output");
const FName UOptimusNode::PropertyMeta::Resource("Resource");


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
	const FVector2D& InPosition
	)
{
	if (InPosition.ContainsNaN() || InPosition.Equals(GraphPosition))
	{
		return false;
	}

	GraphPosition = InPosition;

	if (bSendNotifications)
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


void UOptimusNode::SetDiagnosticLevel(EOptimusDiagnosticLevel InDiagnosticLevel)
{
	if (DiagnosticLevel != InDiagnosticLevel)
	{
		DiagnosticLevel = InDiagnosticLevel;
		Notify(EOptimusGraphNotifyType::NodeDiagnosticLevelChanged);
	}
}


UOptimusNodePin* UOptimusNode::FindPin(const FStringView InPinPath) const
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
	TArray<UClass*> NodeClasses;
	
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		
		if (!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden) &&
			Class->IsChildOf(StaticClass()) &&
			Class->GetPackage() != GetTransientPackage())
		{
			NodeClasses.Add(Class);
		}
	}
	return NodeClasses;
}


void UOptimusNode::PostCreateNode()
{
	CachedPinLookup.Empty();
	Pins.Empty();

	{
		TGuardValue<bool> NodeConstructionGuard(bConstructingNode, true);
		ConstructNode();
	}
}


void UOptimusNode::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);

	Ar.UsingCustomVersion(FOptimusObjectVersion::GUID);
}


void UOptimusNode::PostLoad()
{
	UObject::PostLoad();

	// Earlier iterations didn't set this flag. 
	SetFlags(RF_Transactional);
}


void UOptimusNode::Notify(EOptimusGraphNotifyType InNotifyType)
{
	if (CanNotify())
	{
		UOptimusNodeGraph *Graph = Cast<UOptimusNodeGraph>(GetOuter());

		if (Graph)
		{
			Graph->Notify(InNotifyType, this);
		}
	}
}



void UOptimusNode::ConstructNode()
{
	CreatePinsFromStructLayout(GetClass(), nullptr);
}


void UOptimusNode::EnableDynamicPins()
{
	bDynamicPins = true;
}


UOptimusNodePin* UOptimusNode::AddPin(
	FName InName,
	EOptimusNodePinDirection InDirection,
	FOptimusNodePinStorageConfig InStorageConfig,
	FOptimusDataTypeRef InDataType,
	UOptimusNodePin* InBeforePin
	)
{
	if (!bDynamicPins)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Attempting to add a pin to a non-dynamic node: %s"), *GetNodePath());
		return nullptr;
	}

	if (InBeforePin)
	{
		if (InBeforePin->GetOwningNode() != this)
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Attempting to place a pin before one that does not belong to this node: %s"), *InBeforePin->GetPinPath());
			return nullptr;
		}
		// TODO: Revisit if/when we add pin groups.
		if (InBeforePin->GetParentPin() != nullptr)
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Attempting to place a pin before one that is not a top-level pin: %s"), *InBeforePin->GetPinPath());
			return nullptr;
		}
	}

	FOptimusNodeAction_AddPin *AddPinAction = new FOptimusNodeAction_AddPin(
		this, InName, InDirection, InStorageConfig, InDataType, InBeforePin); 
	if (!GetActionStack()->RunAction(AddPinAction))
	{
		return nullptr;
	}

	return AddPinAction->GetPin(GetActionStack()->GetGraphCollectionRoot());
}


UOptimusNodePin* UOptimusNode::AddPinDirect(
    FName InName,
    EOptimusNodePinDirection InDirection,
    FOptimusNodePinStorageConfig InStorageConfig,
    FOptimusDataTypeRef InDataType,
    UOptimusNodePin* InBeforePin,
    UOptimusNodePin* InParentPin
	)
{
	UObject* PinParent = InParentPin ? Cast<UObject>(InParentPin) : this;
	UOptimusNodePin* Pin = NewObject<UOptimusNodePin>(PinParent, InName);

	Pin->Initialize(InDirection, InStorageConfig, InDataType);

	if (InParentPin)
	{
		InParentPin->AddSubPin(Pin, InBeforePin);
	}
	else
	{
		int32 Index = Pins.Num();
		if (InBeforePin && ensure(Pins.IndexOfByKey(InBeforePin) != INDEX_NONE))
		{
			Index = Pins.IndexOfByKey(InBeforePin); 
		}
		Pins.Insert(Pin, Index);
	}

	// Add sub-pins, if the registered type is set to show them but only for value types.
	if (InStorageConfig.Type == EOptimusNodePinStorageType::Value &&
		EnumHasAnyFlags(InDataType->TypeFlags, EOptimusDataTypeFlags::ShowElements))
	{
		if (const UScriptStruct* Struct = Cast<const UScriptStruct>(InDataType->TypeObject))
		{
			CreatePinsFromStructLayout(Struct, Pin);
		}
	}

	if (CanNotify())
	{
		Pin->Notify(EOptimusGraphNotifyType::PinAdded);
	}

	return Pin;
}


UOptimusNodePin* UOptimusNode::AddPinDirect(
	const FOptimusParameterBinding& InBinding,
	EOptimusNodePinDirection InDirection,
	UOptimusNodePin* InBeforePin
	)
{
	FOptimusNodePinStorageConfig StorageConfig;
	
	if (!InBinding.DataDomain.IsEmpty())
	{
		StorageConfig.Type = EOptimusNodePinStorageType::Resource;
		StorageConfig.DataDomain = InBinding.DataDomain;
	}
	return AddPinDirect(InBinding.Name, InDirection, StorageConfig, InBinding.DataType, InBeforePin);
}


bool UOptimusNode::RemovePin(UOptimusNodePin* InPin)
{
	if (!bDynamicPins)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Attempting to remove a pin from a non-dynamic node: %s"), *GetNodePath());
		return false;
	}

	if (InPin->GetParentPin() != nullptr)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Attempting to remove a non-root pin: %s"), *InPin->GetPinPath());
		return false;
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	Action->SetTitlef(TEXT("Remove Pin"));

	TArray<UOptimusNodePin*> PinsToRemove = InPin->GetSubPinsRecursively();
	PinsToRemove.Add(InPin);

	const UOptimusNodeGraph* Graph = GetOwningGraph();

	// Validate that there are no links to the pins we want to remove.
	for (const UOptimusNodePin* Pin: PinsToRemove)
	{
		for (const UOptimusNodeLink *Link: Graph->GetPinLinks(Pin))
		{
			Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Link);
		}
	}

	Action->AddSubAction<FOptimusNodeAction_RemovePin>(InPin);
	
	return GetActionStack()->RunAction(Action);
}


bool UOptimusNode::RemovePinDirect(UOptimusNodePin* InPin)
{
	TArray<UOptimusNodePin*> PinsToRemove = InPin->GetSubPinsRecursively();
	PinsToRemove.Add(InPin);

	// Reverse the list so that we start by deleting the leaf-most pins first.
	Algo::Reverse(PinsToRemove);

	const UOptimusNodeGraph* Graph = GetOwningGraph();

	// Validate that there are no links to the pins we want to remove.
	for (const UOptimusNodePin* Pin: PinsToRemove)
	{
		if (!Graph->GetConnectedPins(Pin).IsEmpty())
		{
			UE_LOG(LogOptimusCore, Warning, TEXT("Attempting to remove a connected pin: %s"), *Pin->GetPinPath());
			return false;
		}
	}

	// We only notify on the root pin once we're no longer reachable.
	Pins.Remove(InPin);
	InPin->Notify(EOptimusGraphNotifyType::PinRemoved);
	
	for (UOptimusNodePin* Pin: PinsToRemove)
	{
		ExpandedPins.Remove(Pin->GetUniqueName());
		
		Pin->Rename(nullptr, GetTransientPackage());
		Pin->MarkAsGarbage();
	}

	CachedPinLookup.Reset();

	return true;
}

bool UOptimusNode::SetPinDataType
(
	UOptimusNodePin* InPin,
	FOptimusDataTypeRef InDataType
	)
{
	if (!InPin || InPin->GetDataType() == InDataType.Resolve())
	{
		return false;
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	Action->SetTitlef(TEXT("Set Pin Type"));

	// Disconnect all the links because they _will_ become incompatible.
	for (UOptimusNodePin* ConnectedPin: InPin->GetConnectedPins())
	{
		if (InPin->GetDirection() == EOptimusNodePinDirection::Input)
		{
			Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(ConnectedPin, InPin);
		}
		else
		{
			Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(InPin, ConnectedPin);
		}
	}
	
	Action->AddSubAction<FOptimusNodeAction_SetPinType>(InPin, InDataType);
	
	return GetActionStack()->RunAction(Action);
}


bool UOptimusNode::SetPinDataTypeDirect(
	UOptimusNodePin* InPin, 
	FOptimusDataTypeRef InDataType
	)
{
	// We can currently only change pin types if they have no underlying property.
	if (ensure(InPin) && ensure(InDataType.IsValid()) && 
	    ensure(InPin->GetPropertyFromPin() == nullptr))
	{
		if (!InPin->SetDataType(InDataType))
		{
			return false;
		}

		// For value types, we want to show sub-pins.
		if (InPin->GetStorageType() == EOptimusNodePinStorageType::Value)
		{
			// Remove all sub-pins, if there were any.		
			TGuardValue<bool> SuppressNotifications(bSendNotifications, false);
				
			// If the type was already a sub-element type, remove the existing pins.
			InPin->ClearSubPins();
				
			// Add sub-pins, if the registered type is set to show them but only for value types.
			if (EnumHasAllFlags(InDataType->TypeFlags, EOptimusDataTypeFlags::ShowElements))
			{
				if (const UScriptStruct* Struct = Cast<const UScriptStruct>(InDataType->TypeObject))
				{
					CreatePinsFromStructLayout(Struct, InPin);
				}
			}
		}

		if (CanNotify())
		{
			InPin->Notify(EOptimusGraphNotifyType::PinTypeChanged);
		}

		return true;
	}
	else
	{
		return false;
	}
}


bool UOptimusNode::SetPinName(
	UOptimusNodePin* InPin, 
	FName InNewName
	)
{
	if (!InPin || InPin->GetFName() == InNewName)
	{
		return false;
	}

	// FIXME: Namespace check?
	return GetActionStack()->RunAction<FOptimusNodeAction_SetPinName>(InPin, InNewName);
}


bool UOptimusNode::SetPinNameDirect(
	UOptimusNodePin* InPin, 
	FName InNewName
	)
{
	if (ensure(InPin) && InNewName != NAME_None)
	{
		const FName OldName = InPin->GetFName();
		const bool bIsExpanded = ExpandedPins.Contains(OldName);

		if (InPin->SetName(InNewName))
		{
			// Flush the lookup table
			CachedPinLookup.Reset();

			if (bIsExpanded)
			{
				ExpandedPins.Remove(OldName);
				ExpandedPins.Add(InNewName);
			}
			return true;
		}
	}

	// No success.
	return false;
}


bool UOptimusNode::SetPinDataDomain(
	UOptimusNodePin* InPin,
	const TArray<FName>& InDataDomainLevelNames
	)
{
	if (!InPin || InPin->GetDataDomainLevelNames() == InDataDomainLevelNames)
	{
		return false;
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	Action->SetTitlef(TEXT("Set Pin Data Domain"));

	// Disconnect all the links because they _will_ become incompatible.
	for (UOptimusNodePin* ConnectedPin: InPin->GetConnectedPins())
	{
		if (InPin->GetDirection() == EOptimusNodePinDirection::Input)
		{
			Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(ConnectedPin, InPin);
		}
		else
		{
			Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(InPin, ConnectedPin);
		}
	}
	
	Action->AddSubAction<FOptimusNodeAction_SetPinDataDomain>(InPin, InDataDomainLevelNames);
	
	return GetActionStack()->RunAction(Action);
}


bool UOptimusNode::SetPinDataDomainDirect(
	UOptimusNodePin* InPin,
	const TArray<FName>& InDataDomainLevelNames
	)
{
	InPin->DataDomain.LevelNames = InDataDomainLevelNames;
	return true;
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
#if WITH_EDITOR
		else if (Property->HasMetaData(PropertyMeta::Input))
		{
			if (Property->HasMetaData(PropertyMeta::Output))
			{
				UE_LOG(LogOptimusCore, Error, TEXT("Pin on %s.%s marked both input and output. Ignoring it as output."),
					*GetName(), *Property->GetName());
			}

			CreatePinFromProperty(EOptimusNodePinDirection::Input, Property, InParentPin);
		}
		else if (Property->HasMetaData(PropertyMeta::Output))
		{
			CreatePinFromProperty(EOptimusNodePinDirection::Output, Property, InParentPin);
		}
#endif
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
		UE_LOG(LogOptimusCore, Error, TEXT("No registered type found for pin '%s'."), *InProperty->GetName());
		return nullptr;
	}

	FOptimusNodePinStorageConfig StorageConfig{};
#if WITH_EDITOR
	if (InProperty->HasMetaData(PropertyMeta::Resource))
	{
		if (!ensure(!InParentPin))
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Pin '%s' marked as resource cannot have sub-pins."), *InProperty->GetName());
			return nullptr;
		}

		// Ensure that the data type for the property allows it to be used as a resource.
		if (!EnumHasAnyFlags(DataType->UsageFlags, EOptimusDataTypeUsageFlags::Resource))
		{
			UE_LOG(LogOptimusCore, Error, TEXT("Pin '%s' marked as resource but data type is not compatible."), *InProperty->GetName());
			return nullptr;
		}

		StorageConfig = FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex});
	}
#endif

	return AddPinDirect(InProperty->GetFName(), InDirection, StorageConfig, DataType, nullptr, InParentPin);
}

UOptimusActionStack* UOptimusNode::GetActionStack() const
{
	UOptimusNodeGraph *Graph = GetOwningGraph();
	if (Graph == nullptr)
	{
		return nullptr;
	}
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(Graph->GetCollectionRoot());
	if (!Deformer)
	{
		return nullptr;
	}

	return Deformer->GetActionStack();
}
