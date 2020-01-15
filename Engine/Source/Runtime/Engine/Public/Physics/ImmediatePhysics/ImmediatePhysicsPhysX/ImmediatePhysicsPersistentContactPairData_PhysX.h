// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

#ifndef PERSISTENT_CONTACT_PAIRS
#define PERSISTENT_CONTACT_PAIRS 1
#endif

namespace ImmediatePhysics_PhysX
{

#if PERSISTENT_CONTACT_PAIRS
	/** Persistent data like contact manifold, friction patches, etc... */
	struct FPersistentContactPairData
	{
		PxCache Cache;
		PxU8* Frictions;
		PxU32 NumFrictions;
		uint32 SimCount;

		void Clear()
		{
			FPlatformMemory::Memzero(this, sizeof(FPersistentContactPairData));
		}
	};
#endif

}

#endif // WITH_PHYSX