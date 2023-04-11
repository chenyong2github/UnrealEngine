// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CollisionQueryTaskData.h"

DEFINE_TARGETING_DATA_STORE(FCollisionQueryTaskData)

void FCollisionQueryTaskData::AddStructReferencedObjects(class FReferenceCollector& Collector)
{
	for (TObjectPtr<AActor>& ReferencedActor : IgnoredActors)
	{
		Collector.AddReferencedObject(ReferencedActor);
	}
}
