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
		FDirtyIdx Idx = DirtyPropertiesPool.WriteX(FVec3(1,1,1));
		FDirtyIdx Idx2 = DirtyPropertiesPool.WriteX(FVec3(2,1,1));
		FDirtyIdx Idx3 = DirtyPropertiesPool.WriteInvM(3);


		EXPECT_EQ(Idx.Property, EParticleProperty::X);
		EXPECT_EQ(Idx.Entry, 0);
		EXPECT_EQ(DirtyPropertiesPool.ReadX(Idx),FVec3(1,1,1));


		EXPECT_EQ(Idx2.Property, EParticleProperty::X);
		EXPECT_EQ(Idx2.Entry, 1);
		EXPECT_EQ(DirtyPropertiesPool.ReadX(Idx2),FVec3(2,1,1));


		EXPECT_EQ(Idx3.Property, EParticleProperty::InvM);
		EXPECT_EQ(Idx3.Entry, 0);
		EXPECT_EQ(DirtyPropertiesPool.ReadInvM(Idx3),3);


		//release second x proprty
		DirtyPropertiesPool.FreeProperty(Idx2);

		//get another x property, should be same idx as Idx2 because we reuse indices
		FDirtyIdx Idx4 = DirtyPropertiesPool.WriteX(FVec3(4,1,1));
		EXPECT_EQ(Idx4,Idx2);

		EXPECT_EQ(DirtyPropertiesPool.ReadX(Idx4),FVec3(4,1,1));

		//using higher level api
		//pretend we have a particle with V and Mass as dirty

		//create a dirty properties instance to push to other thread
		FDirtyProperties DirtyParticle;

		FDirtyIdx VIdx0 = DirtyPropertiesPool.WriteV(FVec3(5,1,1));
		DirtyParticle.DirtyProperty(VIdx0);
		DirtyParticle.DirtyProperty(DirtyPropertiesPool.WriteM(6));

		//Other thread can see the dirty properties and read them
		EXPECT_TRUE(DirtyParticle.IsDirty(EParticleFlags::V));
		EXPECT_TRUE(DirtyParticle.IsDirty(EParticleFlags::M));
		//Other values are clean
		EXPECT_FALSE(DirtyParticle.IsDirty(EParticleFlags::X));

		//read the values
		EXPECT_EQ(DirtyParticle.ReadV(DirtyPropertiesPool),FVec3(5,1,1));
		EXPECT_EQ(DirtyParticle.ReadM(DirtyPropertiesPool),6);

		DirtyParticle.Clean(DirtyPropertiesPool);

		//verify that we really cleaned the resource
		FDirtyIdx VIdx1 = DirtyPropertiesPool.WriteV(FVec3(5,1,1));
		EXPECT_EQ(VIdx1,VIdx0);

	}
}

