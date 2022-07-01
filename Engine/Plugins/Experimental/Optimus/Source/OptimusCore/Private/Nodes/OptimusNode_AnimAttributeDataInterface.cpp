// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_AnimAttributeDataInterface.h"

#include "OptimusCoreModule.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodePin.h"
#include "OptimusDataTypeRegistry.h"
#include "OptimusHelpers.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DataInterfaces/OptimusDataInterfaceAnimAttribute.h"


UOptimusNode_AnimAttributeDataInterface::UOptimusNode_AnimAttributeDataInterface() :
	Super()
{
	EnableDynamicPins();
}

void UOptimusNode_AnimAttributeDataInterface::SetDataInterfaceClass(
	TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass)
{
	Super::SetDataInterfaceClass(InDataInterfaceClass);
	
	if (UOptimusAnimAttributeDataInterface* Interface = Cast<UOptimusAnimAttributeDataInterface>(DataInterfaceData))
	{
		// Add a default attribute so that the node is ready to be used
		Interface->AddAnimAttribute(TEXT("EmptyName"), NAME_None,
			FOptimusDataTypeRegistry::Get().FindType(*FFloatProperty::StaticClass()) );
	}

	// Undo support
	DataInterfaceData->SetFlags(RF_Transactional);
}

#if WITH_EDITOR
void UOptimusNode_AnimAttributeDataInterface::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
	{
		static FProperty* NameProperty =
			FOptimusAnimAttributeDescription::StaticStruct()->FindPropertyByName(
				GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeDescription, Name)
			);
		
		static FProperty* BoneNameProperty =
			FOptimusAnimAttributeDescription::StaticStruct()->FindPropertyByName(
				GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeDescription, BoneName)
			);
		
		static FProperty* DataTypeProperty =
			FOptimusAnimAttributeDescription::StaticStruct()->FindPropertyByName(
				GET_MEMBER_NAME_STRING_CHECKED(FOptimusAnimAttributeDescription, DataType)
			);

		if (PropertyChangedEvent.PropertyChain.Contains(NameProperty)||
			PropertyChangedEvent.PropertyChain.Contains(BoneNameProperty) || 
			PropertyChangedEvent.PropertyChain.Contains(DataTypeProperty))
		{
			UpdatePinNames();
		}

		if (PropertyChangedEvent.PropertyChain.Contains(DataTypeProperty))
		{
			UpdatePinTypes();
		}

	}
	else if (PropertyChangedEvent.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::Duplicate |
												EPropertyChangeType::ArrayRemove | EPropertyChangeType::ArrayMove))
	{
		RefreshPins();
	}
	else if(PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear)
	{
		ClearPins();
	}
}

#endif

void UOptimusNode_AnimAttributeDataInterface::RecreateValueContainers()
{
	if (UOptimusAnimAttributeDataInterface* Interface = Cast<UOptimusAnimAttributeDataInterface>(DataInterfaceData))
	{
		Interface->RecreateValueContainers();
	}
}

void UOptimusNode_AnimAttributeDataInterface::UpdatePinTypes()
{
	UOptimusAnimAttributeDataInterface* Interface = Cast<UOptimusAnimAttributeDataInterface>(DataInterfaceData);
	const int32 NumAttributes = Interface->AttributeArray.Num(); 

	// Let's try and figure out which pin got changed.
	const TArray<UOptimusNodePin *> NodePins = GetPins();
	
	if (ensure(NumAttributes == NodePins.Num()))
	{
		for (int32 Index = 0; Index < NodePins.Num(); Index++)
		{
			if (NodePins[Index]->GetDataType() != Interface->AttributeArray[Index].DataType.Resolve())
			{
				SetPinDataType(NodePins[Index], Interface->AttributeArray[Index].DataType);
			}
		}
	}
}


void UOptimusNode_AnimAttributeDataInterface::UpdatePinNames()
{
	UOptimusAnimAttributeDataInterface* Interface = Cast<UOptimusAnimAttributeDataInterface>(DataInterfaceData);
	
	TArray<FOptimusCDIPinDefinition> PinDefinitions = Interface->GetPinDefinitions();
	
	// Let's try and figure out which pin got changed.
	TArray<UOptimusNodePin*> NodePins = GetPins();

	if (ensure(PinDefinitions.Num() == NodePins.Num()))
	{
		for (int32 Index = 0; Index < NodePins.Num(); Index++)
		{
			if (NodePins[Index]->GetFName() != PinDefinitions[Index].PinName)
			{
				SetPinName(NodePins[Index], PinDefinitions[Index].PinName);
			}
		}
	}
}


void UOptimusNode_AnimAttributeDataInterface::ClearPins()
{
	TArray<UOptimusNodePin*> NodePins = GetPins();

	for (UOptimusNodePin* Pin : NodePins)
	{
		RemovePin(Pin);
	}
}

void UOptimusNode_AnimAttributeDataInterface::RefreshPins()
{
	// Save the links and readd them later when new pins are created
	TMap<FName, TArray<UOptimusNodePin*>> ConnectedPinsMap;

	for (const UOptimusNodePin* Pin : GetPins())
	{
		ConnectedPinsMap.Add(Pin->GetFName()) = Pin->GetConnectedPins();
	}	

	ClearPins();

	UOptimusAnimAttributeDataInterface* Interface = Cast<UOptimusAnimAttributeDataInterface>(DataInterfaceData);
	for (const FOptimusAnimAttributeDescription& Attribute : Interface->AttributeArray)
	{
		AddPin(Attribute.PinName, EOptimusNodePinDirection::Output, {}, Attribute.DataType);
	}

	for (UOptimusNodePin* AddedPin : GetPins())
	{
		if (TArray<UOptimusNodePin*>* ConnectedPins = ConnectedPinsMap.Find(AddedPin->GetFName()))
		{
			for (UOptimusNodePin* ConnectedPin : *ConnectedPins)
			{
				GetOwningGraph()->AddLink(AddedPin, ConnectedPin);
			}
		}
	}
}
