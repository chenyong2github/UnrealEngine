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

template <Chaos::EParticleType ParticleType, typename TParticleHandle>
void PushToPhysicsStateImp(const Chaos::FDirtyPropertiesManager& Manager, TParticleHandle* Handle, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, Chaos::FPhysicsSolver* Solver, const bool bInitialized)
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

		if(NewXR || NewNonFrequentData || NewVelocities)
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

			Solver->GetEvolution()->DirtyParticle(*Handle);
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
				Solver->GetEvolution()->SetParticleObjectState(RigidHandle,NewData->ObjectState());
				Solver->GetEvolution()->GetGravityForces().SetEnabled(*RigidHandle,NewData->GravityEnabled());

				RigidHandle->SetDynamicMisc(*NewData);
			}
		}

		//shape properties
		for(int32 ShapeDataIdx : Dirty.ShapeDataIndices)
		{
			const FShapeDirtyData& ShapeData = ShapesData[ShapeDataIdx];
			const int32 ShapeIdx = ShapeData.GetShapeIdx();
			if(auto NewData = ShapeData.FindCollisionData(Manager, ShapeDataIdx)){ Handle->ShapesArray()[ShapeIdx]->SetCollisionData(*NewData); }
			if(auto NewData = ShapeData.FindMaterials(Manager, ShapeDataIdx)){ Handle->ShapesArray()[ShapeIdx]->SetMaterialData(*NewData); }
		}
	}
}

//
// TGeometryParticle<float, 3> template specialization 
//

template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData)
{
	PushToPhysicsStateImp<Chaos::EParticleType::Static>(Manager,Handle,DataIdx,Dirty,ShapesData,GetSolver(),bInitialized);
#if 0
	// move the copied game thread data into the handle
	if (auto* RigidHandle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Handle))
	{
		auto NewX = Data->FindX();
		auto NewR = Data->FindR();
		auto NewGeometry = Data->FindGeometry();

		if(NewX){ RigidHandle->SetX(*NewX); }
		if(NewR){ RigidHandle->SetR(*NewR); }
		if(NewGeometry){ RigidHandle->SetSharedGeometry(*NewGeometry); }

		if(NewX || NewR || NewGeometry)
		{
			const auto& Geometry = RigidHandle->Geometry();
			if(Geometry && Geometry->HasBoundingBox())
			{
				RigidHandle->SetHasBounds(true);
				RigidHandle->SetLocalBounds(Geometry->BoundingBox());
				RigidHandle->SetWorldSpaceInflatedBounds(Geometry->BoundingBox().TransformedAABB(Chaos::TRigidTransform<float,3>(RigidHandle->X(),RigidHandle->R())));
			}
		}

	}
#endif
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
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData)
{
	PushToPhysicsStateImp<Chaos::EParticleType::Kinematic>(Manager,Handle->CastToKinematicParticle(),DataIdx,Dirty,ShapesData,GetSolver(),bInitialized);
#if 0
	// move the copied game thread data into the handle
	if(auto* RigidHandle = static_cast<Chaos::TKinematicGeometryParticleHandle<float,3>*>(Handle))
	{
		auto NewX = Data->FindX();
		auto NewR = Data->FindR();
		auto NewGeometry = Data->FindGeometry();

		if(NewX){ RigidHandle->SetX(*NewX); }
		if(NewR){ RigidHandle->SetR(*NewR); }
		if(NewGeometry){ RigidHandle->SetSharedGeometry(*NewGeometry); }

		if(NewX || NewR || NewGeometry)
		{
			const auto& Geometry = RigidHandle->Geometry();
			if(Geometry && Geometry->HasBoundingBox())
			{
				RigidHandle->SetHasBounds(true);
				RigidHandle->SetLocalBounds(Geometry->BoundingBox());
				RigidHandle->SetWorldSpaceInflatedBounds(Geometry->BoundingBox().TransformedAABB(Chaos::TRigidTransform<float,3>(RigidHandle->X(),RigidHandle->R())));
			}
		}

		if(auto NewSpatialIdx = Data->FindSpatialIdx()){ RigidHandle->SetSpatialIdx(*NewSpatialIdx); }
		if(auto NewUniqueIdx = Data->FindUniqueIdx()){ RigidHandle->SetUniqueIdx(*NewUniqueIdx); }
		if(auto NewUserData = Data->FindUserData()){ RigidHandle->SetUserData(*NewUserData); }
#if CHAOS_CHECKED
		if(auto NewDebugName = Data->FindDebugName()){ RigidHandle->SetDebugName(*NewDebugName); }
#endif

		int32 ShapeIdx = 0;
		const auto& Shapes = RigidHandle->ShapesArray();
		for(Chaos::FShapePropertiesData* ShapeData : ShapesData->GetRemoteDatas())
		{
			//todo: avoid iterating over every shape regardless of if it's dirty or not
			if(auto NewData = ShapeData->FindQueryData()){ Shapes[ShapeIdx]->SetQueryData(*NewData); }
			if(auto NewData = ShapeData->FindSimData()){ Shapes[ShapeIdx]->SetSimData(*NewData); }
			if(auto NewData = ShapeData->FindUserData()){ Shapes[ShapeIdx]->SetUserData(*NewData); }
			if(auto NewData = ShapeData->FindGeometry()){ Shapes[ShapeIdx]->SetGeometry(*NewData); }
			//todo: fully marshal materials
#if 0
			if(auto NewData = ShapeData->FindMaterials()){ Shapes[ShapeIdx]->SetMaterials(*NewData); }
			if(auto NewData = ShapeData->FindMaterialMasks()){ Shapes[ShapeIdx]->SetMaterialMasks(*NewData); }
			if(auto NewData = ShapeData->FindMaterialMaskMaps()){ Shapes[ShapeIdx]->SetMaterialMaskMaps(*NewData); }
			if(auto NewData = ShapeData->FindMaterialMaskMapMaterials()){ Shapes[ShapeIdx]->SetMaterialMaskMapMaterials(*NewData); }
#endif
			if(auto NewData = ShapeData->FindDisable()){ Shapes[ShapeIdx]->SetDisable(*NewData); }
			if(auto NewData = ShapeData->FindSimulate()){ Shapes[ShapeIdx]->SetSimulate(*NewData); }
			if(auto NewData = ShapeData->FindCollisionTraceType()){ Shapes[ShapeIdx]->SetCollisionTraceType(*NewData); }
		}
	}

#if 0
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
		RigidHandle->SetUniqueIdx(Data->UniqueIdx);
		RigidHandle->SetUserData(Data->UserData);
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
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::CollisionTraceType))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->CollisionTraceType = Data->CollisionTraceType[CurrShape++];
			}
		}
		//if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::ShapeSimData))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->SimData = Data->ShapeSimData[CurrShape];
				Shape->QueryData = Data->ShapeQueryData[CurrShape++];
			}
		}

		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->Materials = Data->ShapeMaterials[CurrShape++];
			}
		}


	}
#endif
#endif
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
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData)
{
	PushToPhysicsStateImp<Chaos::EParticleType::Rigid>(Manager,Handle->CastToRigidParticle(),DataIdx,Dirty,ShapesData,GetSolver(),bInitialized);
#if 0
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
		RigidHandle->SetUniqueIdx(Data->UniqueIdx);	//todo: this needs to only happen once during initialization
		RigidHandle->SetUserData(Data->UserData);
#if CHAOS_CHECKED
		RigidHandle->SetDebugName(Data->DebugName);
#endif

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::X))
		{
			RigidHandle->SetX(Data->X);
			RigidHandle->SetP(Data->X);
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::R))
		{
			RigidHandle->SetR(Data->R);
			RigidHandle->SetQ(Data->R);
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::Geometry))
		{
			RigidHandle->SetSharedGeometry(Data->Geometry);
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::V))
		{
			RigidHandle->SetV(Data->MV);
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::W))
		{
			RigidHandle->SetW(Data->MW);
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
		}
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::Torque))
		{
			RigidHandle->SetTorque(Data->MTorque);
		}

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::LinearImpulse))
		{
			RigidHandle->SetLinearImpulse(Data->MLinearImpulse);
		}

		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::AngularImpulse))
		{
			RigidHandle->SetAngularImpulse(Data->MAngularImpulse);
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
		if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::CollisionTraceType))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->CollisionTraceType = Data->CollisionTraceType[CurrShape++];
			}
		}
		//if (Data->DirtyFlags.IsDirty(Chaos::EParticleFlags::ShapeSimData))
		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->SimData = Data->ShapeSimData[CurrShape];
				Shape->QueryData = Data->ShapeQueryData[CurrShape++];
			}
		}

		{
			int32 CurrShape = 0;
			for (const TUniquePtr<Chaos::TPerShapeData<Chaos::FReal, 3>>& Shape : RigidHandle->ShapesArray())
			{
				Shape->Materials = Data->ShapeMaterials[CurrShape++];
			}
		}
		
		if(Data->MInitialized)
		{
			// wait for the first pass with nothing updated to claim its initialized
			bInitialized = true;
		}
	}
#endif
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
		//if (!Particle->IsDirty(Chaos::EParticleFlags::ObjectState))
		//question: is it ok to call this when it was one of the other properties that changed?
		if (!Particle->IsDirty(Chaos::EParticleFlags::DynamicMisc))
		{
			Particle->SetObjectState(Buffer->MObjectState, true);
		}
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
