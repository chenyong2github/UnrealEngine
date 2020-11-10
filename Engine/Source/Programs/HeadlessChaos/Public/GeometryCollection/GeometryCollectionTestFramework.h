// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosSolversModule.h"
#include "Chaos/ParticleHandle.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionProxyData.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/EvolutionTraits.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "Chaos/Defines.h"

namespace GeometryCollectionTest
{
	using namespace Chaos;

	enum WrapperType
	{
		RigidBody,
		GeometryCollection
	};

	enum ESimplicialType
	{
		Chaos_Simplicial_Box,
		Chaos_Simplicial_Sphere,
		Chaos_Simplicial_GriddleBox,
		Chaos_Simplicial_Tetrahedron,
		Chaos_Simplicial_Imported_Sphere,
		Chaos_Simplicial_None
	};

	struct WrapperBase
	{
		WrapperType Type;
		WrapperBase(WrapperType TypeIn) : Type(TypeIn) {}
		template<class AS_T> AS_T* As() { return AS_T::StaticType() == Type ? static_cast<AS_T*>(this) : nullptr; }
		template<class AS_T> const AS_T* As() const { return AS_T::StaticType() == Type ? static_cast<const AS_T*>(this) : nullptr; }
	};

	template <typename Traits>
	struct TGeometryCollectionWrapper : public WrapperBase
	{
		TGeometryCollectionWrapper() : WrapperBase(WrapperType::GeometryCollection) {}
		TGeometryCollectionWrapper(
			TSharedPtr<FGeometryCollection> RestCollectionIn,
			TSharedPtr<FGeometryDynamicCollection> DynamicCollectionIn,
			TGeometryCollectionPhysicsProxy<Traits>* PhysObjectIn)
			: WrapperBase(WrapperType::GeometryCollection)
			, RestCollection(RestCollectionIn)
			, DynamicCollection(DynamicCollectionIn)
			, PhysObject(PhysObjectIn) {}
		static WrapperType StaticType() { return WrapperType::GeometryCollection; }
		TSharedPtr<FGeometryCollection> RestCollection;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection;
		TGeometryCollectionPhysicsProxy<Traits>* PhysObject;
	};

	struct RigidBodyWrapper : public WrapperBase
	{
		RigidBodyWrapper(
			TSharedPtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterialIn,
			TGeometryParticle<float, 3>* ParticleIn)
			: WrapperBase(WrapperType::RigidBody)
			, PhysicalMaterial(PhysicalMaterialIn)
			, Particle(ParticleIn) {}
		static WrapperType StaticType() { return WrapperType::RigidBody; }
		TSharedPtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
		TGeometryParticle<float, 3>* Particle;
	};


	struct CreationParameters
	{
		FTransform RootTransform = FTransform::Identity;
		/**
		 * Implicit box uses Scale X, Y, Z for dimensions.
		 * Implicit sphere uses Scale X for radius.
		 */
		FVector InitialLinearVelocity = FVector::ZeroVector;
		EObjectStateTypeEnum DynamicState = EObjectStateTypeEnum::Chaos_Object_Dynamic;
		bool Simulating = true;
		float Mass = 1.0;
		bool bMassAsDensity = false;
		ECollisionTypeEnum CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		ESimplicialType SimplicialType = ESimplicialType::Chaos_Simplicial_Sphere;
		EImplicitTypeEnum ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
		EInitialVelocityTypeEnum InitialVelocityType = EInitialVelocityTypeEnum::Chaos_Initial_Velocity_None;
		TArray<FTransform> NestedTransforms;
		bool EnableClustering = true;
		FTransform GeomTransform = FTransform::Identity;
		TSharedPtr<FGeometryCollection> RestCollection = nullptr;
		int32 MaxClusterLevel = 100;
		TArray<float> DamageThreshold = { 1000.0f };
		Chaos::FClusterCreationParameters<float>::EConnectionMethod ClusterConnectionMethod = Chaos::FClusterCreationParameters<float>::EConnectionMethod::PointImplicit;
		bool RemoveOnFractureEnabled = false;
		int32 CollisionGroup = 0;
		int32 MinLevelSetResolution = 5;
		int32 MaxLevelSetResolution = 10;
		int32 ClusterGroupIndex = 0;
	};


	enum GeometryType {
		GeometryCollectionWithSingleRigid,
		RigidFloor,
		GeometryCollectionWithSuppliedRestCollection
	};


	template <GeometryType>
	struct TNewSimulationObject
	{
		template<typename Traits>
		static WrapperBase* Init(const CreationParameters Params = CreationParameters());
	};

	struct FrameworkParameters
	{
		FrameworkParameters() : Dt(1/60.) {}
		FrameworkParameters(float dt) : Dt(Dt) {}
		float Dt;
		Chaos::EThreadingMode ThreadingMode = Chaos::EThreadingMode::SingleThread;
	};

	template<typename Traits>
	class TFramework
	{
	public:

		TFramework(FrameworkParameters Properties = FrameworkParameters());
		virtual ~TFramework();

		void AddSimulationObject(WrapperBase* Object);
		void Initialize();
		void Advance();
		FReal Dt;
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Chaos::TPBDRigidsSolver<Traits>* Solver;
		TArray< WrapperBase* > PhysicsObjects;
	};

#define EVOLUTION_TRAIT(Trait) extern template class CHAOS_TEMPLATE_API TFramework<Trait>;
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
}