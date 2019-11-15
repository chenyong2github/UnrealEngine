// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidParticles.h"

void Chaos::EnsureSleepingObjectState(EObjectStateType ObjectState)
{
	ensure(ObjectState != EObjectStateType::Kinematic);
	ensure(ObjectState != EObjectStateType::Static);
}

template class Chaos::TPBDRigidParticles<float, 3>;
