// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

#include "Modules/ModuleManager.h"

namespace ChaosTest
{

	using namespace Chaos;

	GTEST_TEST(DirtyParticleTests,Basic)
	{
		FDirtyPropertiesManager DirtyPropertiesPool;
		FDirtyProperties DirtyParticle1;
		DirtyParticle1.WriteX(DirtyPropertiesPool, FVec3(1,1,1));
		DirtyParticle1.WriteInvM(DirtyPropertiesPool, 3);

		FDirtyProperties DirtyParticle2;
		DirtyParticle2.WriteX(DirtyPropertiesPool, FVec3(2,1,1));

		//properties are dirty
		EXPECT_TRUE(DirtyParticle1.IsDirty(EParticleFlags::X));
		EXPECT_TRUE(DirtyParticle1.IsDirty(EParticleFlags::InvM));

		EXPECT_TRUE(DirtyParticle2.IsDirty(EParticleFlags::X));

		//untouched properties are clean
		EXPECT_FALSE(DirtyParticle2.IsDirty(EParticleFlags::InvM));

		//values were saved
		EXPECT_EQ(DirtyParticle1.ReadX(DirtyPropertiesPool), FVec3(1,1,1));
		EXPECT_EQ(DirtyParticle1.ReadInvM(DirtyPropertiesPool), 3);
		EXPECT_EQ(DirtyParticle2.ReadX(DirtyPropertiesPool), FVec3(2,1,1));
		
		EXPECT_EQ(DirtyParticle1.PopX(DirtyPropertiesPool),FVec3(1,1,1));
		EXPECT_FALSE(DirtyParticle1.IsDirty(EParticleFlags::X));

		//make sure we are not leaking shared ptrs
		TSharedPtr<FImplicitObject,ESPMode::ThreadSafe> Ptr(new TSphere<FReal,3>(FVec3(0), 0));
		DirtyParticle1.WriteGeometry(DirtyPropertiesPool, Ptr);
		TWeakPtr<FImplicitObject,ESPMode::ThreadSafe> WeakPtr(Ptr);
		Ptr = nullptr;

		EXPECT_TRUE(WeakPtr.IsValid());	//still around because dirty pool is holding on to it
		{
			TSharedPtr<FImplicitObject,ESPMode::ThreadSafe> Geom = DirtyParticle1.PopGeometry(DirtyPropertiesPool);
			EXPECT_TRUE(WeakPtr.IsValid());	//Popped the geometry but still holding on to it
		}

		EXPECT_FALSE(WeakPtr.IsValid());	//Finished with popped geometry so shared ptr goes away
		

		//If we haven't popped everything we call this
		DirtyParticle1.Clean(DirtyPropertiesPool);
		DirtyParticle2.Clean(DirtyPropertiesPool);
	}
}

