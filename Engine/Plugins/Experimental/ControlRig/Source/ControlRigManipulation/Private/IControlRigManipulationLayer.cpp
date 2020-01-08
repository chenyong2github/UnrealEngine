// Copyright Epic Games, Inc. All Rights Reserved.

#include "IControlRigManipulationLayer.h"

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

	for (int32 Index = ManipulatableObjects.Num() - 1; Index >= 0 ; --Index)
	{
		IControlRigManipulatable* ManipObj = ManipulatableObjects[Index];
		RemoveManipulatableObject(ManipObj);
	}

	ManipulatableObjects.Num();
}

void IControlRigManipulationLayer::AddManipulatableObject(IControlRigManipulatable* InObject)
{
	if (InObject && !ManipulatableObjects.Contains(InObject))
	{
		ManipulatableObjects.Add(InObject);
	}
}

void IControlRigManipulationLayer::RemoveManipulatableObject(IControlRigManipulatable* InObject)
{
	if (InObject && ManipulatableObjects.Contains(InObject))
	{
		ManipulatableObjects.Remove(InObject);
	}
}

