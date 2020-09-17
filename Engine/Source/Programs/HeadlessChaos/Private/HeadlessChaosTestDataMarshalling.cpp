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

	GTEST_TEST(DataMarshalling,Marshalling)
	{
		FChaosMarshallingManager Manager;

		float ExternalDt = 1/30.f;
		float InternalDt = ExternalDt;

		TSet<FPushPhysicsData*> BuffersSeen;
		for(int Step = 0; Step < 10; ++Step)
		{
			//Internal and external dt match so every internal step should get 1 data, the one we wrote to
			const auto DataWritten = Manager.GetProducerData_External();
			Manager.Step_External(ExternalDt);
			const auto PushData = Manager.StepInternalTime_External(InternalDt);
			EXPECT_EQ(PushData.Num(),1);
			EXPECT_EQ(PushData[0],DataWritten);
			
			BuffersSeen.Add(DataWritten);
			EXPECT_EQ(BuffersSeen.Num(),Step == 0 ? 1 : 2);	//we should only ever use two buffers when dts match because we just keep cycling back and forth

			Manager.FreeData_Internal(PushData[0]);
		}

		BuffersSeen.Empty();
		InternalDt = ExternalDt * 0.5f;
		//tick internal dt twice as fast, should only get data every other step
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto DataWritten = Manager.GetProducerData_External();
			Manager.Step_External(ExternalDt);
			for(int InternalStep = 0; InternalStep < 2; ++InternalStep)
			{
				const auto PushData = Manager.StepInternalTime_External(InternalDt);
				if(InternalStep == 0)
				{
					EXPECT_EQ(PushData.Num(),1);
					EXPECT_EQ(DataWritten,PushData[0]);
					Manager.FreeData_Internal(PushData[0]);

					BuffersSeen.Add(DataWritten);
					EXPECT_EQ(BuffersSeen.Num(),Step == 0 ? 1 : 2);	//we should only ever use two buffers when dts match because we just keep cycling back and forth
				}
				else
				{
					EXPECT_EQ(PushData.Num(),0);
				}
			}
		}

		BuffersSeen.Empty();
		InternalDt = ExternalDt * 2;
		//tick internal dt for double the interval, should get two push datas per internal tick
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto DataWritten1 = Manager.GetProducerData_External();
			Manager.Step_External(ExternalDt);

			const auto DataWritten2 = Manager.GetProducerData_External();
			Manager.Step_External(ExternalDt);

			const auto PushData = Manager.StepInternalTime_External(InternalDt);
			EXPECT_EQ(PushData.Num(),2);
			EXPECT_EQ(PushData[0],DataWritten1);
			EXPECT_EQ(PushData[1],DataWritten2);

			for(FPushPhysicsData* Data : PushData)
			{
				BuffersSeen.Add(Data);
				Manager.FreeData_Internal(Data);
			}

			EXPECT_EQ(BuffersSeen.Num(),Step == 0 ? 2 : 3);	//we should only ever use three buffers
		}
	}

	TYPED_TEST(AllTraits, DataMarshalling_Callbacks)
	{
		auto* Solver = FChaosSolversModule::GetModule()->CreateSolver<TypeParam>(nullptr, EThreadingMode::SingleThread);
		
		int Count = 0;
		float Time = 0;
		FSimCallbackHandle* Callback = &Solver->RegisterSimCallback([&Count, &Time](const TArray<FSimCallbackData*>& Data)
		{
			EXPECT_EQ(Data.Num(),1);
			EXPECT_EQ(Data[0]->Data.Int, Count);
			++Count;
			EXPECT_EQ(Time,Data[0]->GetStartTime());
		});

		const float Dt = 1/30.f;

		for(int Step = 0; Step < 10; ++Step)
		{
			Solver->FindOrCreateCallbackProducerData(*Callback).Data.Int = Step;
			Solver->AdvanceAndDispatch_External(Dt);
			Time += Dt;

			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
		}
		
		EXPECT_EQ(Count,10);

		Solver->UnregisterSimCallback(*Callback);

		for(int Step = 0; Step < 10; ++Step)
		{
			Solver->AdvanceAndDispatch_External(Dt);
			Time += Dt;

			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
		}

		EXPECT_EQ(Count,10);
	}

	TYPED_TEST(AllTraits,DataMarshalling_OneShotCallbacks)
	{
		auto* Solver = FChaosSolversModule::GetModule()->CreateSolver<TypeParam>(nullptr,EThreadingMode::SingleThread);
		
		int Count = 0;
		Solver->RegisterSimOneShotCallback([&Count]()
		{
			EXPECT_EQ(Count,0);
			++Count;
		});

		for(int Step = 0; Step < 10; ++Step)
		{
			Solver->RegisterSimOneShotCallback([Step, &Count]()
			{
				EXPECT_EQ(Count,Step+1);	//at step plus first one we registered
				++Count;
			});

			Solver->AdvanceAndDispatch_External(1/30.f);

			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
		}

		EXPECT_EQ(Count,11);

	}
}
