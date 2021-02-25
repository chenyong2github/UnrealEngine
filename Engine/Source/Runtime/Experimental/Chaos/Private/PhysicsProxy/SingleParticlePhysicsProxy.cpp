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


FSingleParticlePhysicsProxy::FSingleParticlePhysicsProxy(TUniquePtr<PARTICLE_TYPE>&& InParticle, FParticleHandle* InHandle, UObject* InOwner, FInitialState InInitialState)
	: IPhysicsProxyBase(EPhysicsProxyType::SingleParticleProxy)
	, bInitialized(false)
	, InitialState(InInitialState)
	, Particle(MoveTemp(InParticle))
	, Handle(InHandle)
	, Owner(InOwner)
	, PullDataInterpIdx_External(INDEX_NONE)
{
	Particle->SetProxy(this);
}


FSingleParticlePhysicsProxy::~FSingleParticlePhysicsProxy()
{
}


const FInitialState& 
FSingleParticlePhysicsProxy::GetInitialState() const 
{ 
	return InitialState; 
}

template <Chaos::EParticleType ParticleType, typename TEvolution>
void PushToPhysicsStateImp(const Chaos::FDirtyPropertiesManager& Manager, Chaos::TGeometryParticleHandle<Chaos::FReal,3>* Handle, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, TEvolution& Evolution, const bool bInitialized)
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
				FAABB3 WorldSpaceBounds = Geometry->BoundingBox().TransformedAABB(TRigidTransform<float,3>(Handle->X(),Handle->R()));
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
				RigidHandle->ResetVSmoothFromForces();
			}

			if(auto NewData = ParticleData.FindDynamicMisc(Manager,DataIdx))
			{
				Evolution.SetParticleObjectState(RigidHandle,NewData->ObjectState());

				// @todo(chaos) re-enable this below when the GT/PT two way sync for this flag is fixed - see similar comment in ParticleHandle.h
				/*if (RigidHandle->Disabled() != NewData->Disabled())
				{
					if (NewData->Disabled())
					{
						Evolution.DisableParticle(Handle);
					}
					else
					{
						Evolution.EnableParticle(Handle, nullptr);
					}
				}*/
				Evolution.SetParticleObjectState(RigidHandle, NewData->ObjectState());

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

template <typename Traits>
void FSingleParticlePhysicsProxy::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution)
{
	using namespace Chaos;
	switch(Dirty.ParticleData.GetParticleBufferType())
	{
		
	case EParticleType::Static: PushToPhysicsStateImp<EParticleType::Static>(Manager, Handle, DataIdx, Dirty, ShapesData, Evolution, bInitialized); break;
	case EParticleType::Kinematic: PushToPhysicsStateImp<EParticleType::Kinematic>(Manager, Handle, DataIdx, Dirty, ShapesData, Evolution, bInitialized); break;
	case EParticleType::Rigid: PushToPhysicsStateImp<EParticleType::Rigid>(Manager, Handle, DataIdx, Dirty, ShapesData, Evolution, bInitialized); break;
	default: check(false); //unexpected path
	}
}

void FSingleParticlePhysicsProxy::ClearAccumulatedData()
{
	if(auto Rigid = Particle->CastToRigidParticle())
	{
		Rigid->ClearForces(false);
		Rigid->ClearTorques(false);
		Rigid->SetLinearImpulse(Chaos::FVec3(0), false);
		Rigid->SetAngularImpulse(Chaos::FVec3(0), false);
	}
	
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

void FSingleParticlePhysicsProxy::BufferPhysicsResults(Chaos::FDirtyRigidParticleData& PullData)
{
	using namespace Chaos;
	// Move simulation results into the double buffer.
	TPBDRigidParticleHandle<float, 3>* RigidHandle = Handle ? Handle->CastToRigidParticle() : nullptr;	//TODO: can handle be null?
	if(RigidHandle)
	{
		PullData.SetProxy(*this);
		BufferPhysicsResultsImp(PullData, RigidHandle);
	}
}

void FSingleParticlePhysicsProxy::BufferPhysicsResults_External(Chaos::FDirtyRigidParticleData& PullData)
{
	if(auto Rigid = Particle->CastToRigidParticle())
	{
		PullData.SetProxy(*this);
		BufferPhysicsResultsImp(PullData, Rigid);
	}
}

bool FSingleParticlePhysicsProxy::PullFromPhysicsState(const Chaos::FDirtyRigidParticleData& PullData,int32 SolverSyncTimestamp, const Chaos::FDirtyRigidParticleData* NextPullData, const float* Alpha)
{
	// Move buffered data into the TPBDRigidParticle without triggering invalidation of the physics state.
	auto Rigid = Particle ? Particle->CastToRigidParticle() : nullptr;
	if(Rigid)
	{
		if(NextPullData)
		{
			Rigid->SetX(FMath::Lerp(PullData.X, NextPullData->X, *Alpha), false);
			Rigid->SetR(FMath::Lerp(PullData.R, NextPullData->R, *Alpha), false);
			Rigid->SetV(FMath::Lerp(PullData.V, NextPullData->V, *Alpha), false);
			Rigid->SetW(FMath::Lerp(PullData.W, NextPullData->W, *Alpha), false);
		}
		else
		{
			Rigid->SetX(PullData.X, false);
			Rigid->SetR(PullData.R, false);
			Rigid->SetV(PullData.V, false);
			Rigid->SetW(PullData.W, false);
		}
		
		Rigid->UpdateShapeBounds();
		//if (!Particle->IsDirty(Chaos::EParticleFlags::ObjectState))
		//question: is it ok to call this when it was one of the other properties that changed?
		if(!Rigid->IsDirty(Chaos::EParticleFlags::DynamicMisc))
		{
			Rigid->SetObjectState(PullData.ObjectState,true, /*bInvalidate=*/false);
		}
	}
	return true;
}

bool FSingleParticlePhysicsProxy::IsDirty()
{
	return Particle->IsDirty();
}

Chaos::EWakeEventEntry FSingleParticlePhysicsProxy::GetWakeEvent() const
{
	//question: should this API exist on proxy?
	auto Rigid = Particle->CastToRigidParticle();
	return Rigid ? Rigid->GetWakeEvent() : Chaos::EWakeEventEntry::None;
}

void FSingleParticlePhysicsProxy::ClearEvents()
{
	//question: should this API exist on proxy?
	if(auto Rigid = Particle->CastToRigidParticle())
	{
		Rigid->ClearEvents();
	}
}

#define EVOLUTION_TRAIT(Traits)\
template void FSingleParticlePhysicsProxy::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,\
	int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Chaos::Traits>& Evolution);
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT