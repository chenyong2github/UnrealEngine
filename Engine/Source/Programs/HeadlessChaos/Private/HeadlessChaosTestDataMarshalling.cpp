// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestUtility.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ErrorReporter.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/Utilities.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#include "Modules/ModuleManager.h"
#include "Chaos/ChaosMarshallingManager.h"

namespace ChaosTest
{

    using namespace Chaos;

	GTEST_TEST(DataMarshalling, SyncMarshalling)
	{
		FChaosMarshallingManager Manager;

		float ExternalDt = 1/60.f;
		float InternalTime = 0;
		float InternalDt = 1/60.f;

		EXPECT_EQ(Manager.ConsumeData_Internal(InternalTime,InternalDt),nullptr);	//empty initially
		auto* FirstData = Manager.GetProducerData_External();
		EXPECT_NE(FirstData,nullptr);	//gives valid data to write into
		EXPECT_EQ(Manager.ConsumeData_Internal(InternalTime,InternalDt),nullptr);	//still empty because haven't called step yet

		Manager.Step_External(ExternalDt);

		auto* SecondData = Manager.GetProducerData_External();
		EXPECT_NE(SecondData,nullptr);	//gives valid data to write into immediately after calling step

		auto* FirstDataInternal = Manager.ConsumeData_Internal(InternalTime,InternalDt);
		EXPECT_NE(FirstDataInternal,nullptr);	//gives valid data to read from
		EXPECT_EQ(FirstDataInternal,FirstData);	//we got the right data
		Manager.FreeData_Internal(FirstDataInternal);
		EXPECT_EQ(Manager.ConsumeData_Internal(InternalTime,InternalDt),nullptr);	//empty because already consumed

		//consume data again
		Manager.Step_External(ExternalDt);
		EXPECT_EQ(Manager.ConsumeData_Internal(InternalTime,InternalDt),nullptr);	//new data starts at time 1/60 so this gives back nullptr
		InternalTime += InternalDt;
		auto* SecondDataInternal = Manager.ConsumeData_Internal(InternalTime,InternalDt);
		EXPECT_NE(SecondDataInternal,nullptr);	//now we see valid data
		EXPECT_EQ(SecondDataInternal,SecondData);
		Manager.FreeData_Internal(SecondDataInternal);
		EXPECT_EQ(Manager.ConsumeData_Internal(InternalTime,InternalDt),nullptr);	//empty because already consumed

		//don't write anything
		Manager.Step_External(ExternalDt);
		InternalTime += InternalDt;
		auto* ThirdDataInternal = Manager.ConsumeData_Internal(InternalTime,InternalDt);
		EXPECT_NE(ThirdDataInternal,nullptr);	//still gives valid data to read from, even if it's empty
		Manager.FreeData_Internal(ThirdDataInternal);
		EXPECT_EQ(Manager.ConsumeData_Internal(InternalTime,InternalDt),nullptr);	//empty because already consumed

		//data is reused and we only use 2 buffers total
		for(int i=0; i<10; ++i)
		{
			auto* AnotherExternalBuffer = Manager.GetProducerData_External();
			auto* ExpectedBuffer = i % 2 == 0 ? SecondData : FirstData;
			EXPECT_EQ(AnotherExternalBuffer,ExpectedBuffer);	//just keeps alternating back and forth
			Manager.Step_External(ExternalDt);
			InternalTime += InternalDt;
			auto* InternalBuffer = Manager.ConsumeData_Internal(InternalTime,InternalDt);
			EXPECT_EQ(InternalBuffer,ExpectedBuffer); //internal is the same as external was
			Manager.FreeData_Internal(InternalBuffer);
		}
		
	}
}
