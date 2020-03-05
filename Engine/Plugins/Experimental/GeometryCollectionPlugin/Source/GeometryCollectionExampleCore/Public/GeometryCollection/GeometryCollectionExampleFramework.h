// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosSolversModule.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "PhysicsProxy/PhysicsProxies.h"

namespace GeometryCollectionExample
{
	enum WrapperType
	{
		RigidBody,
		GeometryCollection
	};

	struct WrapperBase
	{
		WrapperType Type;
		WrapperBase(WrapperType TypeIn) : Type(TypeIn) {}
		template<class AS_T> AS_T* As() { return AS_T::StaticType() == Type ? static_cast<AS_T*>(this) : nullptr; }
		template<class AS_T> const AS_T* As() const { return AS_T::StaticType() == Type ? static_cast<const AS_T*>(this) : nullptr; }
	};

	struct GeometryCollectionWrapper : public WrapperBase
	{
		GeometryCollectionWrapper() : WrapperBase(WrapperType::GeometryCollection) {}
		GeometryCollectionWrapper(
			TSharedPtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterialIn,
			TSharedPtr<FGeometryCollection> RestCollectionIn,
			TSharedPtr<FGeometryDynamicCollection> DynamicCollectionIn,
			FGeometryCollectionPhysicsProxy* PhysObjectIn)
			: WrapperBase(WrapperType::GeometryCollection)
			, PhysicalMaterial(PhysicalMaterialIn)
			, RestCollection(RestCollectionIn)
			, DynamicCollection(DynamicCollectionIn)
			, PhysObject(PhysObjectIn) {}
		static WrapperType StaticType() { return WrapperType::GeometryCollection; }
		TSharedPtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
		TSharedPtr<FGeometryCollection> RestCollection;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection;
		FGeometryCollectionPhysicsProxy* PhysObject;
	};

	struct RigidBodyWrapper : public WrapperBase
	{
		RigidBodyWrapper() : WrapperBase(WrapperType::RigidBody) {}
		static WrapperType StaticType() { return WrapperType::RigidBody; }
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
		FSingleParticlePhysicsProxy<float>* PhysObject;
	};


	struct CreationParameters
	{
		FTransform Position = FTransform::Identity;
		FVector Scale = FVector(1.0);
		int32 DynamicState = (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic;
	};


	enum GeometryType {
		GeometryCollectionWithSingleCube,
		RigidBodyAnalyticCube
	};


	template<GeometryType>
	WrapperBase* NewSimulationObject(const CreationParameters Params = CreationParameters());


	struct FrameworkParameters
	{
		float Dt = 1 / 24.;
		Chaos::EThreadingMode ThreadingMode = Chaos::EThreadingMode::SingleThread;
	};

	template<class T>
	class Framework
	{
	public:

		Framework(FrameworkParameters Properties = FrameworkParameters());
		virtual ~Framework();

		void AddSimulationObject(WrapperBase* Object);
		void Initialize();
		void Advance();

		T Dt;
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Chaos::FPBDRigidsSolver* Solver;
		TArray< WrapperBase* > PhysicsObjects;
	};

}