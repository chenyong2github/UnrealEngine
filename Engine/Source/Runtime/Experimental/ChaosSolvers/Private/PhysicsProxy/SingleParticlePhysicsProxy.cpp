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

template< class PARTICLE_TYPE >
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::FSingleParticlePhysicsProxy(PARTICLE_TYPE* InParticle, FParticleHandle* InHandle, UObject* InOwner, FInitialState InInitialState)
	: Base(InOwner)
	, bInitialized(false)
	, InitialState(InInitialState)
	, Particle(InParticle)
	, Handle(InHandle)
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


//
// TGeometryParticle<float, 3> template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TKinematicGeometryParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		RigidHandle->SetX(Data->X);
		RigidHandle->SetR(Data->R);
		RigidHandle->SetSharedGeometry(Data->Geometry);
		if (Data->Geometry && Data->Geometry->HasBoundingBox())
		{
			RigidHandle->SetHasBounds(true);
			RigidHandle->SetLocalBounds(Data->Geometry->BoundingBox());
			RigidHandle->SetWorldSpaceInflatedBounds(Data->Geometry->BoundingBox().TransformedAABB(Chaos::TRigidTransform<float, 3>(Data->X, Data->R)));
		}
		RigidHandle->SetSpatialIdx(Data->SpatialIdx);	//todo: this needs to only happen once during initialization
		RigidHandle->SetHashResultLowLevel(Data->HashResult);
#if CHAOS_CHECKED
		RigidHandle->SetDebugName(Data->DebugName);
#endif

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::ShapeDisableCollision))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->bDisable = Data->ShapeCollisionDisableFlags[CurrShape++];
			}
		}
	}
}



template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->ClearDirtyFlags();
}


template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
}


template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TGeometryParticle
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
bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::HasAwakeEvent() const
{
	return false;
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearEvents()
{

}
//
// TKinematicGeometryParticle template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TKinematicGeometryParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TKinematicGeometryParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		RigidHandle->SetX(Data->X);
		RigidHandle->SetR(Data->R);
		RigidHandle->SetSharedGeometry(Data->Geometry);
		RigidHandle->SetV(Data->MV);
		RigidHandle->SetW(Data->MW);
		RigidHandle->SetCenterOfMass(Data->MCenterOfMass);
		RigidHandle->SetRotationOfMass(Data->MRotationOfMass);
		RigidHandle->SetSpatialIdx(Data->SpatialIdx);	//todo: this needs to only happen once during initialization
		RigidHandle->SetHashResultLowLevel(Data->HashResult);	//todo: this needs to only happen once during initialization
#if CHAOS_CHECKED
		RigidHandle->SetDebugName(Data->DebugName);
#endif
		
		if (Data->Geometry && Data->Geometry->HasBoundingBox())
		{
			RigidHandle->SetHasBounds(true);
			RigidHandle->SetLocalBounds(Data->Geometry->BoundingBox());
			Chaos::TAABB<float, 3> WorldSpaceBox = Data->Geometry->BoundingBox().TransformedAABB(Chaos::TRigidTransform<float, 3>(Data->X, Data->R));
			WorldSpaceBox.ThickenSymmetrically(Data->MV);
			RigidHandle->SetWorldSpaceInflatedBounds(WorldSpaceBox);
		}

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::ShapeDisableCollision))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->bDisable = Data->ShapeCollisionDisableFlags[CurrShape++];
			}
		}

	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->ClearDirtyFlags();
}

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TKinematicGeometryParticle
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
bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::HasAwakeEvent() const
{
	return false;
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearEvents()
{

}

//
// TPBDRigidParticle template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TPBDRigidParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TPBDRigidParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		//
		// TODO: Split all these setters into separate functions so that
		//       we don't have to check for each dirty type. We don't
		//       *actually* have to check each dirty type, but some of
		//       these set operations require extra actions. This avoids
		//       performing them for every set.
		//

		// TODO: Since this can't change, it doesn't have a dirty flag.
		// This should be moved to initialization.
		RigidHandle->SetSpatialIdx(Data->SpatialIdx);	//todo: this needs to only happen once during initialization
		RigidHandle->SetHashResultLowLevel(Data->HashResult);	//todo: this needs to only happen once during initialization
#if CHAOS_CHECKED
		RigidHandle->SetDebugName(Data->DebugName);
#endif

		//  DynamicProperties are anything that would wake up a sleeping
		//  particle. For example, if we set position on a sleeping particle
		//  we will need to wake it up
		bool bDynamicPropertyUpdated = false;

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::X))
		{
			RigidHandle->SetX(Data->X);
			RigidHandle->SetP(Data->X);
			bDynamicPropertyUpdated = true;
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::R))
		{
			RigidHandle->SetR(Data->R);
			RigidHandle->SetQ(Data->R);
			bDynamicPropertyUpdated = true;
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::Geometry))
		{
			RigidHandle->SetSharedGeometry(Data->Geometry);
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::V))
		{
			RigidHandle->SetV(Data->MV);
			bDynamicPropertyUpdated = true;
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::W))
		{
			RigidHandle->SetW(Data->MW);
			bDynamicPropertyUpdated = true;
		}

		RigidHandle->SetCenterOfMass(Data->MCenterOfMass);
		RigidHandle->SetRotationOfMass(Data->MRotationOfMass);
		RigidHandle->SetM(Data->MM);
		RigidHandle->SetInvM(Data->MInvM);
		RigidHandle->SetI(Data->MI);
		RigidHandle->SetInvI(Data->MInvI);
		RigidHandle->SetLinearEtherDrag(Data->MLinearEtherDrag);
		RigidHandle->SetAngularEtherDrag(Data->MAngularEtherDrag);

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::F))
		{
			RigidHandle->SetF(Data->MF);
			bDynamicPropertyUpdated = true;
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::Torque))
		{
			RigidHandle->SetTorque(Data->MTorque);
			bDynamicPropertyUpdated = true;
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::ObjectState))
		{
			GetSolver()->GetEvolution()->SetParticleObjectState(RigidHandle, Data->MObjectState);
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::GravityEnabled))
		{
			GetSolver()->GetEvolution()->GetGravityForces().SetEnabled(*RigidHandle, Data->MGravityEnabled);
		}

		if (Data->DirtyFlags.IsDirty((int32)Chaos::EParticleFlags::X | (int32)Chaos::EParticleFlags::R | (int32)Chaos::EParticleFlags::V | (int32)Chaos::EParticleFlags::Geometry))
		{
			if (Data->Geometry && Data->Geometry->HasBoundingBox())
			{
				RigidHandle->SetHasBounds(true);
				RigidHandle->SetLocalBounds(Data->Geometry->BoundingBox());
				Chaos::TAABB<float, 3> WorldSpaceBox = Data->Geometry->BoundingBox().TransformedAABB(Chaos::TRigidTransform<float, 3>(Data->X, Data->R));
				WorldSpaceBox.ThickenSymmetrically(Data->MV);
				RigidHandle->SetWorldSpaceInflatedBounds(WorldSpaceBox);
			}
			else
			{
				//todo: compute bounds based on sample points
			}
		}

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::ShapeDisableCollision))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->bDisable = Data->ShapeCollisionDisableFlags[CurrShape++];
			}
		}

		if (bDynamicPropertyUpdated)
		{
			if (bInitialized && RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping)
			{
				GetSolver()->GetEvolution()->SetParticleObjectState(RigidHandle, Chaos::EObjectStateType::Dynamic);
			}
		}
		else
		{
			// wait for the first pass with nothing updated to claim its initialized
			bInitialized = true;
		}
	}

}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearAccumulatedData()
{
	Particle->SetF(Chaos::TVector<float, 3>(0));
	Particle->SetTorque(Chaos::TVector<float, 3>(0));
	Particle->ClearDirtyFlags();
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
	if (Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = static_cast<Chaos::TPBDRigidParticleHandle<float, 3>*>(Handle))
	{
		Chaos::TPBDRigidParticleData<float, 3>* Buffer = BufferedData->AccessProducerBuffer();
		Buffer->X = RigidHandle->X();
		Buffer->R = RigidHandle->R();
		Buffer->MV = RigidHandle->V();
		Buffer->MW = RigidHandle->W();
		Buffer->MObjectState = RigidHandle->ObjectState();
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TPBDRigidParticle without triggering invalidation of the physics state.
	if (Particle)
	{
		const Chaos::TPBDRigidParticleData<float, 3>* Buffer = BufferedData->GetConsumerBuffer();
		Particle->SetX(Buffer->X, false);
		Particle->SetR(Buffer->R, false);
		Particle->SetV(Buffer->MV, false);
		Particle->SetW(Buffer->MW, false);
		Particle->UpdateShapeBounds();
		Particle->SetObjectState(Buffer->MObjectState, true);
	}
}

template< >
bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::IsDirty()
{
	return Particle->IsDirty();
}

template< >
bool FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >::HasAwakeEvent() const
{
	return Particle->HasAwakeEvent();
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

template class CHAOSSOLVERS_API FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
template class CHAOSSOLVERS_API FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
template class CHAOSSOLVERS_API FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >;
