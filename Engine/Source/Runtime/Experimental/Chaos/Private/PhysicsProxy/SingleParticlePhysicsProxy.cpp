// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

#include "ChaosStats.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsSolver.h"
#include "Chaos/ChaosMarshallingManager.h"
#include "Chaos/PullPhysicsDataImp.h"

template< class PARTICLE_TYPE >
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::FSingleParticlePhysicsProxy(PARTICLE_TYPE* InParticle, FParticleHandle* InHandle, UObject* InOwner, FInitialState InInitialState)
	: Base(InOwner)
	, bInitialized(false)
	, InitialState(InInitialState)
	, Particle(InParticle)
	, Handle(InHandle)
	, PullDataInterpIdx_External(INDEX_NONE)
{
	BufferedData = Chaos::FMultiBufferFactory< typename PARTICLE_TYPE::FData >::CreateBuffer(Chaos::EMultiBufferMode::Double);
}

template< class PARTICLE_TYPE >
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::~FSingleParticlePhysicsProxy()
{
}

template< class PARTICLE_TYPE >
const FInitialState& 
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::GetInitialState() const 
{ 
	return InitialState; 
}

template <Chaos::EParticleType ParticleType, typename TParticleHandle, typename TEvolution>
void PushToPhysicsStateImp(const Chaos::FDirtyPropertiesManager& Manager, TParticleHandle* Handle, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, TEvolution& Evolution, const bool bInitialized)
{
	using namespace Chaos;
	constexpr bool bHasKinematicData = ParticleType != EParticleType::Static;
	constexpr bool bHasDynamicData = ParticleType == EParticleType::Rigid;
	auto KinematicHandle = bHasKinematicData ? static_cast<Chaos::TKinematicGeometryParticleHandle<float,3>*>(Handle) : nullptr;
	auto RigidHandle = bHasDynamicData ? static_cast<Chaos::TPBDRigidParticleHandle<float,3>*>(Handle) : nullptr;
	const FParticleDirtyData& ParticleData = Dirty.ParticleData;
	// move the copied game thread data into the handle
	{
		auto NewXR = ParticleData.FindXR(Manager, DataIdx);
		auto NewNonFrequentData = ParticleData.FindNonFrequentData(Manager, DataIdx);

		if(NewXR)
		{
			Handle->SetXR(*NewXR);
		}

		if(NewNonFrequentData)
		{
			Handle->SetNonFrequentData(*NewNonFrequentData);
		}

		auto NewVelocities = bHasKinematicData ? ParticleData.FindVelocities(Manager, DataIdx) : nullptr;
		if(NewVelocities)
		{
			KinematicHandle->SetVelocities(*NewVelocities);
		}

		auto NewKinematicTarget = bHasKinematicData ? ParticleData.FindKinematicTarget(Manager, DataIdx) : nullptr;
		if (NewKinematicTarget)
		{
			KinematicHandle->SetKinematicTarget(*NewKinematicTarget);
		}

		if(NewXR || NewNonFrequentData || NewVelocities || NewKinematicTarget)
		{
			const auto& Geometry = Handle->Geometry();
			if(Geometry && Geometry->HasBoundingBox())
			{
				Handle->SetHasBounds(true);
				Handle->SetLocalBounds(Geometry->BoundingBox());
				TAABB<float,3> WorldSpaceBounds = Geometry->BoundingBox().TransformedAABB(TRigidTransform<float,3>(Handle->X(),Handle->R()));
				if(bHasKinematicData)
				{
					WorldSpaceBounds.ThickenSymmetrically(KinematicHandle->V());
				}
				Handle->SetWorldSpaceInflatedBounds(WorldSpaceBounds);
			}

			Evolution.DirtyParticle(*Handle);
		}

		if(bHasDynamicData)
		{
			if(auto NewData = ParticleData.FindMassProps(Manager,DataIdx))
			{
				RigidHandle->SetMassProps(*NewData);
			}

			if(auto NewData = ParticleData.FindDynamics(Manager, DataIdx))
			{
				RigidHandle->SetDynamics(*NewData);
			}

			if(auto NewData = ParticleData.FindDynamicMisc(Manager,DataIdx))
			{
				Evolution.SetParticleObjectState(RigidHandle,NewData->ObjectState());

				RigidHandle->SetDynamicMisc(*NewData);
			}
		}

		//shape properties
		for(int32 ShapeDataIdx : Dirty.ShapeDataIndices)
		{
			const FShapeDirtyData& ShapeData = ShapesData[ShapeDataIdx];
			const int32 ShapeIdx = ShapeData.GetShapeIdx();
			if(auto NewData = ShapeData.FindCollisionData(Manager, ShapeDataIdx))
			{
				Handle->ShapesArray()[ShapeIdx]->SetCollisionData(*NewData);
			}
			if(auto NewData = ShapeData.FindMaterials(Manager, ShapeDataIdx))
			{
				Handle->ShapesArray()[ShapeIdx]->SetMaterialData(*NewData);
			}
		}
	}
}

//
// TGeometryParticle<float, 3> template specialization 
//

template< >
template <typename Traits>
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution)
{
	PushToPhysicsStateImp<Chaos::EParticleType::Static>(Manager,Handle,DataIdx,Dirty,ShapesData,Evolution,bInitialized);
}



template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->ClearDirtyFlags();
}


template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float,3>>::BufferPhysicsResults(Chaos::FDirtyRigidParticleData& PullData)
{
	// Move simulation results into the double buffer.
}

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::BufferPhysicsResults_External(Chaos::FDirtyRigidParticleData& PullData)
{
	// Move simulation results into the double buffer.
}


template< >
bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float,3>>::PullFromPhysicsState(const Chaos::FDirtyRigidParticleData& PullData,int32 SolverSyncTimestamp, const Chaos::FDirtyRigidParticleData* NextPullData, const float* Alpha)
{
	// Move buffered data into the TGeometryParticle
	return true;
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ConcreteType()
{
	return EPhysicsProxyType::SingleGeometryParticleType;
}

template< >
Chaos::EWakeEventEntry  FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::GetWakeEvent() const
{
	return Chaos::EWakeEventEntry::None;
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearEvents()
{

}
//
// TKinematicGeometryParticle template specialization 
//

template <>
template<typename Traits>
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution)
{
	PushToPhysicsStateImp<Chaos::EParticleType::Kinematic>(Manager,Handle->CastToKinematicParticle(),DataIdx,Dirty,ShapesData,Evolution,bInitialized);
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->ClearDirtyFlags();
}

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float,3>>::BufferPhysicsResults(Chaos::FDirtyRigidParticleData& PullData)
{
	// Move simulation results into the double buffer.
}

template< >
CHAOS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::BufferPhysicsResults_External(Chaos::FDirtyRigidParticleData& PullData)
{
	// Move simulation results into the double buffer.
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float,3>>::PullFromPhysicsState(const Chaos::FDirtyRigidParticleData& PullData,int32 SolverSyncTimestamp, const Chaos::FDirtyRigidParticleData* NextPullData, const float* Alpha)
{
	// Move buffered data into the TKinematicGeometryParticle
	return true;
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ConcreteType()
{
	return EPhysicsProxyType::SingleKinematicParticleType;
}

template< >
Chaos::EWakeEventEntry  FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::GetWakeEvent() const
{
	return Chaos::EWakeEventEntry::None;
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearEvents()
{

}

//
// TPBDRigidParticle template specialization 
//

template<>
template<typename Traits>
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution)
{
	PushToPhysicsStateImp<Chaos::EParticleType::Rigid>(Manager,Handle->CastToRigidParticle(),DataIdx,Dirty,ShapesData,Evolution,bInitialized);
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->SetF(Chaos::TVector<float, 3>(0), false);
	Particle->SetTorque(Chaos::TVector<float, 3>(0), false);
	Particle->SetLinearImpulse(Chaos::TVector<float, 3>(0), false);
	Particle->SetAngularImpulse(Chaos::TVector<float, 3>(0), false);
	Particle->ClearDirtyFlags();
}

template <typename T>
void BufferPhysicsResultsImp(Chaos::FDirtyRigidParticleData& PullData, T* Particle)
{
	PullData.X = Particle->X();
	PullData.R = Particle->R();
	PullData.V = Particle->V();
	PullData.W = Particle->W();
	PullData.ObjectState = Particle->ObjectState();
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float,3>>::BufferPhysicsResults(Chaos::FDirtyRigidParticleData& PullData)
{
	using namespace Chaos;
	// Move simulation results into the double buffer.
	if(TPBDRigidParticleHandle<float,3>* RigidHandle = static_cast<TPBDRigidParticleHandle<float,3>*>(Handle))
	{
		PullData.SetProxy(*this);
		BufferPhysicsResultsImp(PullData, RigidHandle);
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::BufferPhysicsResults_External(Chaos::FDirtyRigidParticleData& PullData)
{
	PullData.SetProxy(*this);
	BufferPhysicsResultsImp(PullData, Particle);
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float,3>>::PullFromPhysicsState(const Chaos::FDirtyRigidParticleData& PullData,int32 SolverSyncTimestamp, const Chaos::FDirtyRigidParticleData* NextPullData, const float* Alpha)
{
	// Move buffered data into the TPBDRigidParticle without triggering invalidation of the physics state.
	if(Particle)
	{
		if(NextPullData)
		{
			Particle->SetX(FMath::Lerp(PullData.X, NextPullData->X, *Alpha), false);
			Particle->SetR(FMath::Lerp(PullData.R, NextPullData->R, *Alpha), false);
			Particle->SetV(FMath::Lerp(PullData.V, NextPullData->V, *Alpha), false);
			Particle->SetW(FMath::Lerp(PullData.W, NextPullData->W, *Alpha), false);
		}
		else
		{
			Particle->SetX(PullData.X, false);
			Particle->SetR(PullData.R, false);
			Particle->SetV(PullData.V, false);
			Particle->SetW(PullData.W, false);
		}
		
		Particle->UpdateShapeBounds();
		//if (!Particle->IsDirty(Chaos::EParticleFlags::ObjectState))
		//question: is it ok to call this when it was one of the other properties that changed?
		if(!Particle->IsDirty(Chaos::EParticleFlags::DynamicMisc))
		{
			Particle->SetObjectState(PullData.ObjectState,true, /*bInvalidate=*/false);
		}
	}
	return true;
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
Chaos::EWakeEventEntry FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >::GetWakeEvent() const
{
	return Particle->GetWakeEvent();
}

template< >
void FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >::ClearEvents()
{
	Particle->ClearEvents();
}

template< >
EPhysicsProxyType  FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ConcreteType() 
{ 
	return EPhysicsProxyType::SingleRigidParticleType;
}

template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >;


#define EVOLUTION_TRAIT(Traits)\
template void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<Chaos::FReal,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);\
\
template void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<Chaos::FReal,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData,Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);\
\
template void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<Chaos::FReal,3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT