// Copyright Epic Games, Inc. All Rights Reserved.

#include "IControlRigManipulationLayer.h"
#include "Manipulatable/IControlRigManipulatable.h"

UControlRigManipulationLayer::UControlRigManipulationLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

IControlRigManipulationLayer::IControlRigManipulationLayer()
	:bLayerCreated(false)
{}

void IControlRigManipulationLayer::CreateLayer()
{
	// clear current layers
	if (bLayerCreated)
	{
		DestroyLayer();
	}

	bLayerCreated = true;
}

void IControlRigManipulationLayer::DestroyLayer()
{
	bLayerCreated = false;

	DestroyGizmosActors();

	for (int32 Index = ManipulatableObjects.Num() - 1; Index >= 0; --Index)
	{
		if (!ManipulatableObjects[Index].IsValid())
		{
			continue;
		}

		if (IControlRigManipulatable* Manipulatable = Cast<IControlRigManipulatable>(ManipulatableObjects[Index].Get()))
		{
			RemoveManipulatableObject(Manipulatable);
		}
	}
}

void IControlRigManipulationLayer::AddManipulatableObject(IControlRigManipulatable* InObject)
{
	UObject* ManipulatableUObject = Cast<UObject>(InObject);
	if (ManipulatableUObject == nullptr)
	{
		return;
	}

	for (int32 Index = ManipulatableObjects.Num() - 1; Index >= 0; --Index)
	{
		if (ManipulatableObjects[Index].Get() == ManipulatableUObject)
		{
			return;
		}
	}

	ManipulatableObjects.Add(TWeakObjectPtr<UObject>(ManipulatableUObject));
}

void IControlRigManipulationLayer::RemoveManipulatableObject(IControlRigManipulatable* InObject)
{
	UObject* ManipulatableUObject = Cast<UObject>(InObject);
	if (ManipulatableUObject == nullptr)
	{
		return;
	}

	for (int32 Index = ManipulatableObjects.Num() - 1; Index >= 0; --Index)
	{
		if (ManipulatableObjects[Index].Get() == ManipulatableUObject)
		{
			ManipulatableObjects.RemoveAt(Index);
			return;
		}
	}
}

