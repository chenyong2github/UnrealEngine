// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsInterfaceDeclares.h"
#include "PhysicsCore.h"


#if PHYSICS_INTERFACE_PHYSX

#include "Physics/PhysicsInterfacePhysX.h"
#include "Physics/PhysScene_PhysX.h"
#include "Physics/Experimental/PhysScene_ImmediatePhysX.h"
#include "Physics/Experimental/PhysicsInterfaceImmediatePhysX.h"

#elif WITH_CHAOS

#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

#else

static_assert(false, "A physics engine interface must be defined to build");

#endif
