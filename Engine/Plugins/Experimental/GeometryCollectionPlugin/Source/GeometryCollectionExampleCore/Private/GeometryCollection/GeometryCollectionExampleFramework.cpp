// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleFramework.h"
#include "GeometryCollection/GeometryCollectionExampleUtility.h"
#include "ChaosSolversModule.h"

#include "Chaos/ErrorReporter.h"


namespace GeometryCollectionExample
{
	template<>
	WrapperBase* NewSimulationObject<GeometryType::GeometryCollectionWithSingleCube>(const CreationParameters Params)
	{
		TSharedPtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
		TSharedPtr<FGeometryCollection> RestCollection;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection;

		InitCollectionsParameters InitParams = { Params.Position, Params.Scale, nullptr, Params.DynamicState };
		InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;

			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		return new GeometryCollectionWrapper(PhysicalMaterial,RestCollection,DynamicCollection,PhysObject);
	}


	template<>
	WrapperBase* NewSimulationObject<GeometryType::RigidBodyAnalyticCube>(const CreationParameters Params)
	{
		return nullptr;
	}


	template<class T>
	Framework<T>::Framework(FrameworkParameters Parameters)
		: Dt(Parameters.Dt)
		, Module(FChaosSolversModule::GetModule())
		, Solver(Module->CreateSolver(nullptr, ESolverFlags::Standalone))
	{
		Module->ChangeThreadingMode(Parameters.ThreadingMode);
	}

	template<class T>
	Framework<T>::~Framework()
	{
		for (WrapperBase* Object : PhysicsObjects)
		{
			if (GeometryCollectionWrapper* GCW = Object->As<GeometryCollectionWrapper>())
			{
				delete GCW->PhysObject;
			}
			//else if (RigidBodyWrapper* BCW = Object->As<WrapperType::RigidBody>())
			//{
			//	delete BCW->PhysObject;
			//}
			delete Object;
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
	}

	template<class T>
	void Framework<T>::AddSimulationObject(WrapperBase * Object)
	{
		PhysicsObjects.Add(Object);
	}

	template<class T>
	void Framework<T>::Initialize()
	{
		Solver->SetEnabled(true);

		for (WrapperBase* Object : PhysicsObjects)
		{
			if (GeometryCollectionWrapper* GCW = Object->As<GeometryCollectionWrapper>())
			{
				Solver->RegisterObject(GCW->PhysObject);
				GCW->PhysObject->ActivateBodies();
				Solver->AddDirtyProxy(GCW->PhysObject);
			}
			//else if (RigidBodyWrapper* RBW = Object->As<RigidBodyWrapper>())
			//{
			//	Solver->RegisterObject(RBW->PhysObject);
			//	RBW->PhysObject->ActivateBodies();
			//	Solver->AddDirtyProxy(RBW->PhysObject);
			//}
		}

		Solver->PushPhysicsState(Module->GetDispatcher());
	}

	template<class T>
	void Framework<T>::Advance()
	{
		Solver->AdvanceSolverBy(Dt);

		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();
	}
	

} // end namespace GeometryCollectionExample

