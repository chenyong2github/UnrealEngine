// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/SingleParticlePhysicsProxy.h"

#include "ChaosStats.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/MultiBufferResource.h"

#if INCLUDE_CHAOS

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
void 
FSingleParticlePhysicsProxy<PARTICLE_TYPE>::FlipBuffer() 
{ 
	BufferedData->FlipProducer(); 
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
	}
}



template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
}


template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TGeometryParticle
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
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::BufferPhysicsResults()
{
	// Move simulation results into the double buffer.
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TKinematicGeometryParticle
}

//
// TPBDRigidParticle template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData)
{
	// move the copied game thread data into the handle
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TPBDRigidParticleHandle<float, 3>*>(Handle))
	{
		typedef Chaos::TPBDRigidParticle<float, 3> PARTICLE_TYPE;
		const auto* Data = static_cast<const typename PARTICLE_TYPE::FData*>(InData);

		RigidHandle->SetX(Data->X);
		RigidHandle->SetR(Data->R);
		RigidHandle->SetSharedGeometry(Data->Geometry);
		RigidHandle->SetV(Data->MV);
		RigidHandle->SetW(Data->MW);
	}
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
	}
}

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PullFromPhysicsState()
{
	// Move buffered data into the TPBDRigidParticle
	if (Particle)
	{
		const Chaos::TPBDRigidParticleData<float, 3>* Buffer = BufferedData->GetConsumerBuffer();
		Particle->SetX(Buffer->X);
		Particle->SetR(Buffer->R);
		Particle->SetV(Buffer->MV);
		Particle->SetW(Buffer->MW);
	}
}


template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
template class CHAOS_API FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >;
#endif