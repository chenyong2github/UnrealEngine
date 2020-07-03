// Copyright Epic Games, Inc. All Rights Reserved.

#include "BodyInstanceCore.h"
#include "BodySetupCore.h"

FBodyInstanceCore::FBodyInstanceCore()
: bSimulatePhysics(false)
, bOverrideMass(false)
, bEnableGravity(true)
, bAutoWeld(false)
, bStartAwake(true)
, bGenerateWakeEvents(false)
, bUpdateMassWhenScaleChanges(false)
{
}

bool FBodyInstanceCore::ShouldInstanceSimulatingPhysics() const
{
	return bSimulatePhysics && BodySetup.IsValid() && BodySetup->GetCollisionTraceFlag() != ECollisionTraceFlag::CTF_UseComplexAsSimple;
}