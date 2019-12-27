// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ChaosPhysicalMaterial.cpp
=============================================================================*/ 

#include "PhysicalMaterials/Experimental/ChaosPhysicalMaterial.h"

UChaosPhysicalMaterial::UChaosPhysicalMaterial(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Friction = 0.5f;
	Restitution = 0.1f;
	SleepingAngularVelocityThreshold = 1.f;
	SleepingLinearVelocityThreshold = 1.f;
}
