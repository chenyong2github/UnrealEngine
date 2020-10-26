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
#include "Chaos/Framework/PhysicsSolverBase.h"

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
#if 0
		//sub-stepping not supported yet
		//TODO: fix this
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
#endif

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
		const float Dt = 1 / 30.f;

		struct FDummyInt : public FSimCallbackInput
		{
			void Reset() {}
			int32 Data;
		};

		struct FCallback : public TSimCallbackObject<FDummyInt>
		{
			virtual FSimCallbackOutput* OnPreSimulate_Internal(const float StartTime, const float DeltaTime, const TArrayView<const FSimCallbackInput*>& Inputs) override
			{
				EXPECT_EQ(1 / 30.f, DeltaTime);
				EXPECT_EQ(Inputs.Num(), 1);
				EXPECT_EQ(static_cast<const FDummyInt*>(Inputs[0])->Data, *CountPtr);
				++(*CountPtr);
				EXPECT_EQ(*Time, Inputs[0]->GetExternalTime());
				return nullptr;
			}

			mutable int32* CountPtr;	//mutable because callback shouldn't have side effects, but for testing this is the easiest way
			float* Time;
		};

		FCallback* Callback = Solver->CreateAndRegisterSimCallbackObject_External<FCallback>();
		Callback->CountPtr = &Count;
		Callback->Time = &Time;


		for(int Step = 0; Step < 10; ++Step)
		{
			Callback->GetProducerInputData_External()->Data = Step;
			
			Solver->AdvanceAndDispatch_External(Dt);
			Solver->UpdateGameThreadStructures();
			Time += Dt;
		}
		
		EXPECT_EQ(Count,10);

		Solver->UnregisterAndFreeSimCallbackObject_External(Callback);

		for(int Step = 0; Step < 10; ++Step)
		{
			Solver->AdvanceAndDispatch_External(Dt);
			Solver->UpdateGameThreadStructures();
			Time += Dt;
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
			Solver->UpdateGameThreadStructures();
		}

		EXPECT_EQ(Count,11);

	}
}
