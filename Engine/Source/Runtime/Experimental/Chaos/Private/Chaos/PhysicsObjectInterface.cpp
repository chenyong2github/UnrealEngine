// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PhysicsObjectInterface.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/PhysicsObjectCollisionInterface.h"
#include "Chaos/PhysicsObjectInternal.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Math/Transform.h"

namespace
{
	template<Chaos::EThreadContext Id>
	void SetParticleStateHelper(const Chaos::FPhysicsObjectHandle PhysicsObject, Chaos::EObjectStateType State)
	{
		if (!PhysicsObject)
		{
			return;
		}

		IPhysicsProxyBase* Proxy = PhysicsObject->PhysicsProxy();
		Chaos::TThreadParticle<Id>* Particle = PhysicsObject->GetParticle<Id>();
		if (!Particle || !Proxy)
		{
			return;
		}

		if (Chaos::TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
		{
			if constexpr (Id == Chaos::EThreadContext::External)
			{
				if (Proxy->GetType() == EPhysicsProxyType::SingleParticleProxy)
				{
					// Easiest way to maintain the same behavior as what we currently have for the single particle case on the game thread.
					static_cast<Chaos::FSingleParticlePhysicsProxy*>(Proxy)->GetGameThreadAPI().SetObjectState(State);
				}
				else
				{
					Rigid->SetObjectState(State, false, false);

					// In the case of the geometry collection, it won't marshal the state from the game thread to the physics thread
					// so we need to do it for it manually. 
					if (Proxy->GetType() == EPhysicsProxyType::GeometryCollectionType)
					{
						if (Chaos::FPhysicsSolverBase* Solver = Proxy->GetSolverBase())
						{
							Solver->EnqueueCommandImmediate(
								[PhysicsObject, State]() {
									SetParticleStateHelper<Chaos::EThreadContext::Internal>(PhysicsObject, State);
								}
							);
						}
					}
				}
			}
			else
			{
				if (Chaos::FPBDRigidsSolver* Solver = Proxy->GetSolver<Chaos::FPBDRigidsSolver>())
				{
					if (Chaos::FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution())
					{
						Evolution->SetParticleObjectState(Rigid, State);
					}
				}
			}
		}
	}
}

FName FClosestPhysicsObjectResult::HitName() const
{
	if (!PhysicsObject)
	{
		return NAME_None;
	}
	return Chaos::FPhysicsObjectInterface::GetName(PhysicsObject);
}

namespace Chaos
{
	template<EThreadContext Id>
	FPhysicsObjectHandle FReadPhysicsObjectInterface<Id>::GetRootObject(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return nullptr;
		}

		return Object->GetRootObject<Id>();
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::HasChildren(const FConstPhysicsObjectHandle Object)
	{
		return Object ? Object->HasChildren<Id>() : false;
	}

	template<EThreadContext Id>
	FTransform FReadPhysicsObjectInterface<Id>::GetTransform(const FConstPhysicsObjectHandle Object)
	{
		return FTransform{ GetR(Object), GetX(Object) };
	}

	template<EThreadContext Id>
	FVector FReadPhysicsObjectInterface<Id>::GetX(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return FVector::Zero();
		}

		if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
		{
			return Particle->X();
		}

		return FVector::Zero();
	}

	template<EThreadContext Id>
	FVector FReadPhysicsObjectInterface<Id>::GetCoM(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return FVector::Zero();
		}

		if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
		{
			if (Chaos::TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->CenterOfMass();
			}
		}

		return FVector::Zero();
	}

	template<EThreadContext Id>
	FVector FReadPhysicsObjectInterface<Id>::GetWorldCoM(const FConstPhysicsObjectHandle Object)
	{
		return GetX(Object) + GetR(Object).RotateVector(GetCoM(Object));
	}

	template<EThreadContext Id>
	FQuat FReadPhysicsObjectInterface<Id>::GetR(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return FQuat::Identity;
		}

		if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
		{
			return Particle->R();
		}

		return FQuat::Identity;
	}

	template<EThreadContext Id>
	FSpatialAccelerationIdx FReadPhysicsObjectInterface<Id>::GetSpatialIndex(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return {};
		}

		if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
		{
			return Particle->SpatialIdx();
		}

		return {};
	}

	template<EThreadContext Id>
	TThreadParticle<Id>* FReadPhysicsObjectInterface<Id>::GetParticle(const FConstPhysicsObjectHandle Handle)
	{
		if (!Handle)
		{
			return nullptr;
		}
		return Handle->GetParticle<Id>();
	}

	template<EThreadContext Id>
	TArray<TThreadParticle<Id>*> FReadPhysicsObjectInterface<Id>::GetAllParticles(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		TArray<TThreadParticle<Id>*> Particles;
		Particles.Reserve(InObjects.Num());

		for (const FConstPhysicsObjectHandle& Handle : InObjects)
		{
			if (!Handle)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Handle->GetParticle<Id>())
			{
				Particles.Add(Particle);
			}
		}

		return Particles;
	}

	template<EThreadContext Id>
	TArray<TThreadRigidParticle<Id>*> FReadPhysicsObjectInterface<Id>::GetAllRigidParticles(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		TArray<TThreadRigidParticle<Id>*> Particles;
		Particles.Reserve(InObjects.Num());

		for (const FConstPhysicsObjectHandle& Handle : InObjects)
		{
			if (!Handle)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Handle->GetParticle<Id>())
			{
				if (TThreadRigidParticle<Id>* RigidParticle = Particle->CastToRigidParticle())
				{
					Particles.Add(RigidParticle);
				}
			}
		}

		return Particles;
	}

	template<EThreadContext Id>
	TArray<FPerShapeData*> FReadPhysicsObjectInterface<Id>::GetAllShapes(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		TArray<FPerShapeData*> AllShapes;

		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				const Chaos::FShapesArray& ShapesArray = Particle->ShapesArray();
				for (const TUniquePtr<Chaos::FPerShapeData>& Shape : ShapesArray)
				{
					AllShapes.Add(Shape.Get());
				}
			}
		}

		return AllShapes;
	}

	template<EThreadContext Id>
	TArray<TThreadShapeInstance<Id>*> FReadPhysicsObjectInterface<Id>::GetAllThreadShapes(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		TArray<TThreadShapeInstance<Id>*> AllShapes;

		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				for (const TUniquePtr<TThreadShapeInstance<Id>>& Shape : Particle->ShapeInstances())
				{
					AllShapes.Add(Shape.Get());
				}
			}
		}

		return AllShapes;
	}

	template<EThreadContext Id>
	void FReadPhysicsObjectInterface<Id>::VisitEveryShape(TArrayView<const FConstPhysicsObjectHandle> InObjects, TFunctionRef<bool(const FConstPhysicsObjectHandle, TThreadShapeInstance<Id>*)> Lambda)
	{
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				for (const TUniquePtr<TThreadShapeInstance<Id>>& Shape : Particle->ShapeInstances())
				{
					if (Lambda(Object, Shape.Get()))
					{
						return;
					}
				}
			}
		}
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::GetPhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FConstPhysicsObjectHandle ObjectB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::GetPhysicsObjectOverlap);
		FPhysicsObjectCollisionInterface Interface{ *this };
		// This is slow and inefficient and hence deprecated.
		bool bRetOverlap = false;
		if (OutOverlap.MTD)
		{
			bRetOverlap |= Interface.PhysicsObjectOverlapWithMTD(ObjectA, FTransform::Identity, ObjectB, FTransform::Identity, bTraceComplex, *OutOverlap.MTD);
		}

		if (OutOverlap.AxisOverlap)
		{
			bRetOverlap |= Interface.PhysicsObjectOverlapWithAABB(ObjectA, FTransform::Identity, ObjectB, FTransform::Identity, bTraceComplex, FVector::Zero(), *OutOverlap.AxisOverlap);
		}

		return bRetOverlap;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::GetPhysicsObjectOverlapWithTransform(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::GetPhysicsObjectOverlapWithTransform);
		FPhysicsObjectCollisionInterface Interface{ *this };
		// This is slow and inefficient and hence deprecated.
		bool bRetOverlap = false;
		if (OutOverlap.MTD)
		{
			bRetOverlap |= Interface.PhysicsObjectOverlapWithMTD(ObjectA, InTransformA, ObjectB, InTransformB, bTraceComplex, *OutOverlap.MTD);
		}

		if (OutOverlap.AxisOverlap)
		{
			bRetOverlap |= Interface.PhysicsObjectOverlapWithAABB(ObjectA, InTransformA, ObjectB, InTransformB, bTraceComplex, FVector::Zero(), *OutOverlap.AxisOverlap);
		}

		return bRetOverlap;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllValid(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		bool bCheck = !InObjects.IsEmpty();
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			bCheck &= (Object != nullptr && Object->IsValid());
		}
		return bCheck;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllKinematic(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		bool bCheck = !InObjects.IsEmpty();
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			bCheck &= (Object && Object->IsValid() && Object->ObjectState<Id>() == EObjectStateType::Kinematic);
		}
		return bCheck;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllSleeping(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		bool bCheck = !InObjects.IsEmpty();
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			bCheck &= (Object && Object->IsValid() && Object->ObjectState<Id>() == EObjectStateType::Sleeping);
		}
		return bCheck;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllRigidBody(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		bool bCheck = !InObjects.IsEmpty();
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			bCheck &= (Object && Object->IsValid() && Object->ObjectState<Id>() != EObjectStateType::Static);
		}
		return bCheck;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllDynamic(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		bool bCheck = !InObjects.IsEmpty();
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			bCheck &= (Object && Object->IsValid() && Object->ObjectState<Id>() == EObjectStateType::Dynamic);
		}
		return bCheck;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllDisabled(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		bool bDisabled = !InObjects.IsEmpty();
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			bool bParticleDisabled = true;
			if (Object)
			{
				if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
				{
					bParticleDisabled = FPhysicsObject::IsParticleDisabled<Id>(Particle);
				}
			}
			bDisabled &= bParticleDisabled;
		}
		return bDisabled;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::AreAllShapesQueryEnabled(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		if (InObjects.IsEmpty())
		{
			return false;
		}

		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (Object)
			{
				if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
				{
					for (const TUniquePtr<FPerShapeData>& ShapeData : Particle->ShapesArray())
					{
						if (!ShapeData->GetCollisionData().bQueryCollision)
						{
							return false;
						}
					}
				}
			}
		}
		return true;
	}

	template<EThreadContext Id>
	float FReadPhysicsObjectInterface<Id>::GetMass(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		float Mass = 0.f;
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (Object)
			{
				if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
				{
					if (TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
					{
						Mass += static_cast<float>(Rigid->M());
					}
				}
			}
		}
		return Mass;
	}

	template<EThreadContext Id>
	FBox FReadPhysicsObjectInterface<Id>::GetBounds(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		FBox RetBox(ForceInit);
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			const TThreadParticle<Id>* Particle = Object->GetParticle<Id>();
			if (!Particle)
			{
				continue;
			}

			FBox ParticleBox(ForceInit);
			if (const FImplicitObject* Geometry = Particle->Geometry().Get(); Geometry && Geometry->HasBoundingBox())
			{
				const Chaos::FAABB3 Box = Geometry->BoundingBox();
				ParticleBox = FBox{ Box.Min(), Box.Max() };
			}

			if (ParticleBox.IsValid)
			{
				RetBox += ParticleBox;
			}
		}
		return RetBox;
	}

	template<EThreadContext Id>
	FBox FReadPhysicsObjectInterface<Id>::GetWorldBounds(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		FBox RetBox(ForceInit);
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			const TThreadParticle<Id>* Particle = Object->GetParticle<Id>();
			if (!Particle)
			{
				continue;
			}

			const FTransform WorldTransform = GetTransform(Object);

			FBox ParticleBox(ForceInit);
			if (const FImplicitObject* Geometry = Particle->Geometry().Get(); Geometry && Geometry->HasBoundingBox())
			{
				const Chaos::FAABB3 WorldBox = Geometry->CalculateTransformedBounds(WorldTransform);
				ParticleBox = FBox{ WorldBox.Min(), WorldBox.Max() };
			}

			if (ParticleBox.IsValid)
			{
				RetBox += ParticleBox;
			}
		}
		return RetBox;
	}

	template<EThreadContext Id>
	FClosestPhysicsObjectResult FReadPhysicsObjectInterface<Id>::GetClosestPhysicsBodyFromLocation(TArrayView<const FConstPhysicsObjectHandle> InObjects, const FVector& WorldLocation)
	{
		FClosestPhysicsObjectResult AggregateResult;
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			const TThreadParticle<Id>* Particle = Object->GetParticle<Id>();
			if (!Particle)
			{
				continue;
			}

			const FTransform WorldTransform = GetTransform(Object);
			const FVector LocalLocation = WorldTransform.InverseTransformPosition(WorldLocation);

			FClosestPhysicsObjectResult Result;

			if (const FImplicitObject* Geometry = Particle->Geometry().Get())
			{
				Result.PhysicsObject = const_cast<FPhysicsObjectHandle>(Object);

				Chaos::FVec3 Normal;
				Result.ClosestDistance = static_cast<double>(Geometry->PhiWithNormal(LocalLocation, Normal));
				Result.ClosestLocation = WorldTransform.TransformPosition(LocalLocation - Result.ClosestDistance * Normal);
			}

			if (!Result)
			{
				continue;
			}

			if (!AggregateResult || Result.ClosestDistance < AggregateResult.ClosestDistance)
			{
				AggregateResult = Result;
			}
		}
		return AggregateResult;
	}

	template<EThreadContext Id>
	FAccelerationStructureHandle FReadPhysicsObjectInterface<Id>::CreateAccelerationStructureHandle(const FConstPhysicsObjectHandle InObject)
	{
		return FAccelerationStructureHandle{InObject->GetParticle<Id>()};
	}

	template<EThreadContext Id>
	void FWritePhysicsObjectInterface<Id>::PutToSleep(TArrayView<const FPhysicsObjectHandle> InObjects)
	{
		for (const FPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			EObjectStateType State = Object->ObjectState<Id>();
			if (State == EObjectStateType::Dynamic || State == EObjectStateType::Sleeping)
			{
				SetParticleStateHelper<Id>(Object, EObjectStateType::Sleeping);
			}
		}
	}

	template<EThreadContext Id>
	void FWritePhysicsObjectInterface<Id>::WakeUp(TArrayView<const FPhysicsObjectHandle> InObjects)
	{
		for (const FPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				EObjectStateType State = Object->ObjectState<Id>();
				if (State == EObjectStateType::Dynamic || State == EObjectStateType::Sleeping)
				{
					SetParticleStateHelper<Id>(Object, EObjectStateType::Dynamic);
					if constexpr (Id == EThreadContext::External)
					{
						if (TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
						{
							Rigid->ClearEvents();
						}
					}
				}
			}
		}
	}

	template<EThreadContext Id>
	void FWritePhysicsObjectInterface<Id>::AddForce(TArrayView<const FPhysicsObjectHandle> InObjects, const FVector& Force, bool bInvalidate)
	{
		for (const FPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				if (Chaos::TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
				{
					if (Rigid->ObjectState() == EObjectStateType::Sleeping || Rigid->ObjectState() == EObjectStateType::Dynamic)
					{
						if (bInvalidate)
						{
							SetParticleStateHelper<Id>(Object, EObjectStateType::Dynamic);
						}

						Rigid->AddForce(Force, bInvalidate);
					}
				}
			}
		}
	}

	template<EThreadContext Id>
	void FWritePhysicsObjectInterface<Id>::AddTorque(TArrayView<const FPhysicsObjectHandle> InObjects, const FVector& Torque, bool bInvalidate)
	{
		for (const FPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				if (Chaos::TThreadRigidParticle<Id>* Rigid = Particle->CastToRigidParticle())
				{
					if (Rigid->ObjectState() == EObjectStateType::Sleeping || Rigid->ObjectState() == EObjectStateType::Dynamic)
					{
						if (bInvalidate)
						{
							SetParticleStateHelper<Id>(Object, EObjectStateType::Dynamic);
						}

						Rigid->AddTorque(Torque, bInvalidate);
					}
				}
			}
		}
	}

	template<EThreadContext Id>
	void FWritePhysicsObjectInterface<Id>::UpdateShapeCollisionFlags(TArrayView<const FPhysicsObjectHandle> InObjects, bool bSimCollision, bool bQueryCollision)
	{
		for (const FPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				for (const TUniquePtr<Chaos::FPerShapeData>& ShapeData : Particle->ShapesArray())
				{
					FCollisionData Data = ShapeData->GetCollisionData();
					Data.bSimCollision = bSimCollision;
					Data.bQueryCollision = bQueryCollision;
					ShapeData->SetCollisionData(Data);
				}
			}
		}
	}

	template<EThreadContext Id>
	void FWritePhysicsObjectInterface<Id>::UpdateShapeFilterData(TArrayView<const FPhysicsObjectHandle> InObjects, const FCollisionFilterData& QueryData, const FCollisionFilterData& SimData)
	{
		for (const FPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			if (TThreadParticle<Id>* Particle = Object->GetParticle<Id>())
			{
				for (const TUniquePtr<Chaos::FPerShapeData>& ShapeData : Particle->ShapesArray())
				{
					ShapeData->SetQueryData(QueryData);
					ShapeData->SetSimData(SimData);
				}
			}
		}
	}

	void FPhysicsObjectInterface::SetName(const FPhysicsObjectHandle Object, const FName& InName)
	{
		if (!Object)
		{
			return;
		}

		Object->SetName(InName);
	}

	FName FPhysicsObjectInterface::GetName(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return NAME_None;
		}

		return Object->GetBodyName();
	}

	void FPhysicsObjectInterface::SetId(const FPhysicsObjectHandle Object, int32 InId)
	{
		if (!Object)
		{
			return;
		}

		Object->SetBodyIndex(InId);
	}

	int32 FPhysicsObjectInterface::GetId(const FConstPhysicsObjectHandle Object)
	{
		if (!Object)
		{
			return INDEX_NONE;
		}

		return Object->GetBodyIndex();
	}

	FPBDRigidsSolver* FPhysicsObjectInterface::GetSolver(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		FPBDRigidsSolver* RetSolver = nullptr;
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			FPBDRigidsSolver* Solver = nullptr;
			if (const IPhysicsProxyBase* Proxy = Object->PhysicsProxy())
			{
				Solver = Proxy->GetSolver<FPBDRigidsSolver>();
			}

			if (!Solver)
			{
				return nullptr;
			}
			else if (!RetSolver)
			{
				RetSolver = Solver;
			}
			else if (Solver != RetSolver)
			{
				return nullptr;
			}
		}
		return RetSolver;
	}

	IPhysicsProxyBase* FPhysicsObjectInterface::GetProxy(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		IPhysicsProxyBase* RetProxy = nullptr;
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			if (!Object)
			{
				continue;
			}

			IPhysicsProxyBase* Proxy = const_cast<IPhysicsProxyBase*>(Object->PhysicsProxy());
			if (!Proxy)
			{
				return nullptr;
			}
			else if (!RetProxy)
			{
				RetProxy = Proxy;
			}
			else if (Proxy != RetProxy)
			{
				return nullptr;
			}
		}
		return RetProxy;
	}

	template class FReadPhysicsObjectInterface<EThreadContext::External>;
	template class FReadPhysicsObjectInterface<EThreadContext::Internal>;

	template class FWritePhysicsObjectInterface<EThreadContext::External>;
	template class FWritePhysicsObjectInterface<EThreadContext::Internal>;
}