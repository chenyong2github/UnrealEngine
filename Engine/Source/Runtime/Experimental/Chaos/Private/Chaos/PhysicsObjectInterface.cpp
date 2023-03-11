// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PhysicsObjectInterface.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/CollisionFilterData.h"
#include "Chaos/GeometryQueries.h"
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
	TThreadParticle<Id>* FReadPhysicsObjectInterface<Id>::GetParticle(const FConstPhysicsObjectHandle Object)
	{
		return Object->GetParticle<Id>();
	}

	template<EThreadContext Id>
	TArray<TThreadParticle<Id>*> FReadPhysicsObjectInterface<Id>::GetAllParticles(TArrayView<const FConstPhysicsObjectHandle> InObjects)
	{
		TArray<TThreadParticle<Id>*> Particles;
		Particles.Reserve(InObjects.Num());

		for (const FConstPhysicsObjectHandle& Handle : InObjects)
		{
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
	void FReadPhysicsObjectInterface<Id>::VisitEveryShape(TArrayView<const FConstPhysicsObjectHandle> InObjects, TFunctionRef<bool(const FConstPhysicsObjectHandle, FPerShapeData*)> Lambda)
	{
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
		// This is slow and inefficient and hence deprecated.
		bool bRetOverlap = false;
		if (OutOverlap.MTD)
		{
			bRetOverlap |= PhysicsObjectOverlapWithMTD(ObjectA, FTransform::Identity, ObjectB, FTransform::Identity, bTraceComplex, *OutOverlap.MTD);
		}

		if (OutOverlap.AxisOverlap)
		{
			bRetOverlap |= PhysicsObjectOverlapWithAABB(ObjectA, FTransform::Identity, ObjectB, FTransform::Identity, bTraceComplex, FVector::Zero(), *OutOverlap.AxisOverlap);
		}

		return bRetOverlap;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::GetPhysicsObjectOverlapWithTransform(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::GetPhysicsObjectOverlapWithTransform);
		// This is slow and inefficient and hence deprecated.
		bool bRetOverlap = false;
		if (OutOverlap.MTD)
		{
			bRetOverlap |= PhysicsObjectOverlapWithMTD(ObjectA, InTransformA, ObjectB, InTransformB, bTraceComplex, *OutOverlap.MTD);
		}

		if (OutOverlap.AxisOverlap)
		{
			bRetOverlap |= PhysicsObjectOverlapWithAABB(ObjectA, InTransformA, ObjectB, InTransformB, bTraceComplex, FVector::Zero(), *OutOverlap.AxisOverlap);
		}

		return bRetOverlap;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlap);
		return PairwiseShapeOverlapHelper(ObjectA, InTransformA, ObjectB, InTransformB, bTraceComplex, false, FVector::Zero(), [](const FShapeOverlapData&, const FShapeOverlapData&, const FMTDInfo&) { return false; });
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlapWithMTD(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, FMTDInfo& OutMTD)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlapWithMTD);
		OutMTD.Penetration = 0.0;
		return PairwiseShapeOverlapHelper(
			ObjectA,
			InTransformA,
			ObjectB,
			InTransformB,
			bTraceComplex,
			true,
			FVector::Zero(),
			[&OutMTD](const FShapeOverlapData&, const FShapeOverlapData&, const FMTDInfo& MTDInfo)
			{
				OutMTD.Penetration = FMath::Max(OutMTD.Penetration, MTDInfo.Penetration);
				return true;
			}
		);
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlapWithAABB(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FBox& OutOverlap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlapWithAABB);
		OutOverlap = FBox{ EForceInit::ForceInitToZero };
		return PairwiseShapeOverlapHelper(
			ObjectA,
			InTransformA,
			ObjectB,
			InTransformB,
			bTraceComplex,
			false,
			Tolerance,
			[&OutOverlap, &Tolerance](const FShapeOverlapData& ShapeA, const FShapeOverlapData& ShapeB, const FMTDInfo&)
			{
				const FAABB3 Intersection = ShapeA.BoundingBox.GetIntersection(ShapeB.BoundingBox);
				OutOverlap += FBox{ Intersection.Min(), Intersection.Max() };
				return true;
			}
		);
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlapWithAABBSize(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FVector& OutOverlapSize)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FReadPhysicsObjectInterface<Id>::PhysicsObjectOverlapWithAABBSize);
		OutOverlapSize = FVector::Zero();
		return PairwiseShapeOverlapHelper(
			ObjectA,
			InTransformA,
			ObjectB,
			InTransformB,
			bTraceComplex,
			false,
			Tolerance,
			[&OutOverlapSize](const FShapeOverlapData& ShapeA, const FShapeOverlapData& ShapeB, const FMTDInfo&)
			{
				const FAABB3 Intersection = ShapeA.BoundingBox.GetIntersection(ShapeB.BoundingBox);
				OutOverlapSize += Intersection.Extents();
				return true;
			}
		);
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::PairwiseShapeOverlapHelper(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, bool bComputeMTD, const FVector& Tolerance, const TFunction<bool(const FShapeOverlapData&, const FShapeOverlapData&, const FMTDInfo&)>& Lambda)
	{
		TArray<FPerShapeData*> ShapesA = GetAllShapes({ &ObjectA, 1 });
		const FTransform TransformA = FTransform{ GetR(ObjectA), GetX(ObjectA) } *InTransformA;
		const FBox BoxA = GetWorldBounds({ &ObjectA, 1 }).TransformBy(InTransformA);

		TArray<FPerShapeData*> ShapesB = GetAllShapes({ &ObjectB, 1 });
		const FTransform TransformB = FTransform{ GetR(ObjectB), GetX(ObjectB) } *InTransformB;
		const FBox BoxB = GetWorldBounds({ &ObjectB, 1 }).TransformBy(InTransformB);

		if (!BoxA.Intersect(BoxB))
		{
			return false;
		}

		bool bFoundOverlap = false;
		for (FPerShapeData* B : ShapesB)
		{
			if (!B)
			{
				continue;
			}

			const TSerializablePtr<FImplicitObject> GeomB = B->GetGeometry();
			if (!GeomB || !GeomB->IsConvex())
			{
				continue;
			}

			const FAABB3 BoxShapeB = GeomB->CalculateTransformedBounds(TransformB).ShrinkSymmetrically(Tolerance);
			// At this point on, this function should be mirror the Overlap_GeomInternal function in PhysInterface_Chaos.cpp.
			// ShapeA is equivalent to InInstance and GeomB is equivalent to InGeom.

			for (FPerShapeData* A : ShapesA)
			{
				if (!A)
				{
					continue;
				}

				FCollisionFilterData ShapeFilter = A->GetQueryData();
				const bool bShapeIsComplex = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::ComplexCollision)) != 0;
				const bool bShapeIsSimple = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::SimpleCollision)) != 0;
				const bool bShouldTrace = (bTraceComplex && bShapeIsComplex) || (!bTraceComplex && bShapeIsSimple);
				if (!bShouldTrace)
				{
					continue;
				}

				const FAABB3 BoxShapeA = A->GetGeometry()->CalculateTransformedBounds(TransformA).ShrinkSymmetrically(Tolerance);
				if (!BoxShapeA.Intersects(BoxShapeB))
				{
					continue;
				}

				Chaos::FMTDInfo TmpMTDInfo;
				const bool bOverlap = Chaos::Utilities::CastHelper(
					*GeomB,
					TransformB,
					[A, &TransformA, bComputeMTD, &TmpMTDInfo](const auto& Downcast, const auto& FullTransformB)
					{
						return Chaos::OverlapQuery(*A->GetGeometry(), TransformA, Downcast, FullTransformB, 0, bComputeMTD ? &TmpMTDInfo : nullptr);
					}
				);

				if (bOverlap)
				{
					bFoundOverlap = true;

					FShapeOverlapData OverlapDataA = { A, BoxShapeA };
					FShapeOverlapData OverlapDataB = { B, BoxShapeB };

					if (!Lambda(OverlapDataA, OverlapDataB, TmpMTDInfo))
					{
						return true;
					}
				}
			}
		}

		return bFoundOverlap;
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
	bool FReadPhysicsObjectInterface<Id>::LineTrace(TArrayView<const FConstPhysicsObjectHandle> InObjects, const FVector& WorldStart, const FVector& WorldEnd, bool bTraceComplex, ChaosInterface::FRaycastHit& OutBestHit)
	{
		bool bHit = false;
		OutBestHit.Distance = TNumericLimits<float>::Max();

		const FVector Delta = WorldEnd - WorldStart;
		const FReal DeltaMag = Delta.Size();
		if (DeltaMag < UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		FTransform BestWorldTM = FTransform::Identity;

		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			const FTransform WorldTM = GetTransform(Object);
			const FVector LocalStart = WorldTM.InverseTransformPositionNoScale(WorldStart);
			const FVector LocalDelta = WorldTM.InverseTransformVectorNoScale(Delta);

			VisitEveryShape(
				{ &Object, 1 },
				[this, &bHit, &WorldTM, &LocalStart, &LocalDelta, &Delta, DeltaMag, &BestWorldTM, bTraceComplex, &OutBestHit](const FConstPhysicsObjectHandle IterObject, FPerShapeData* Shape)
				{
					check(Shape);

					FCollisionFilterData ShapeFilter = Shape->GetQueryData();
					const bool bShapeIsComplex = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::ComplexCollision)) != 0;
					const bool bShapeIsSimple = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::SimpleCollision)) != 0;
					if ((bTraceComplex && bShapeIsComplex) || (!bTraceComplex && bShapeIsSimple))
					{
						FReal Distance;
						FVec3 LocalPosition;
						FVec3 LocalNormal;
						int32 FaceIndex;

						const bool bRaycastHit = Shape->GetGeometry()->Raycast(
							LocalStart,
							LocalDelta / DeltaMag,
							DeltaMag,
							0,
							Distance,
							LocalPosition,
							LocalNormal,
							FaceIndex
						);

						if (bRaycastHit)
						{
							if (Distance < OutBestHit.Distance)
							{
								bHit = true;
								BestWorldTM = WorldTM;
								OutBestHit.Distance = static_cast<float>(Distance);
								OutBestHit.WorldNormal = LocalNormal;
								OutBestHit.WorldPosition = LocalPosition;
								OutBestHit.Shape = Shape;
								if constexpr (Id == EThreadContext::External)
								{
									OutBestHit.Actor = IterObject->GetParticle<Id>();
								}
								OutBestHit.FaceIndex = FaceIndex;
							}
						}
					}
					return false;
				}
			);
		}

		if (bHit)
		{
			OutBestHit.WorldNormal = BestWorldTM.TransformVectorNoScale(OutBestHit.WorldNormal);
			OutBestHit.WorldPosition = BestWorldTM.TransformPositionNoScale(OutBestHit.WorldPosition);
		}

		return bHit;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::ShapeOverlap(TArrayView<const FConstPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& GeomTransform, TArray<ChaosInterface::FOverlapHit>& OutOverlaps)
	{
		bool bHasOverlap = false;
		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			const FTransform WorldTM = GetTransform(Object);

			VisitEveryShape(
				{ &Object, 1 },
				[this, &bHasOverlap, &WorldTM, &InGeom, &GeomTransform, &OutOverlaps](const FConstPhysicsObjectHandle IterObject, FPerShapeData* Shape)
				{
					check(Shape);
					const bool bOverlap = Chaos::Utilities::CastHelper(
						InGeom,
						GeomTransform,
						[Shape, &WorldTM](const auto& Downcast, const auto& FullTransformB)
						{
							return Chaos::OverlapQuery(*Shape->GetGeometry(), WorldTM, Downcast, FullTransformB, 0, nullptr);
						}
					);

					if (bOverlap)
					{
						bHasOverlap = true;

						ChaosInterface::FOverlapHit Overlap;
						Overlap.Shape = Shape;
						if constexpr (Id == EThreadContext::External)
						{
							Overlap.Actor = IterObject->GetParticle<Id>();
						}
						OutOverlaps.Add(Overlap);
					}
					return false;
				}
			);
		}
		return bHasOverlap;
	}

	template<EThreadContext Id>
	bool FReadPhysicsObjectInterface<Id>::ShapeSweep(TArrayView<const FConstPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& StartTM, const FVector& EndPos, bool bSweepComplex, ChaosInterface::FSweepHit& OutBestHit)
	{
		bool bHit = false;
		const FVector StartPos = StartTM.GetTranslation();
		const FVector Delta = EndPos - StartPos;
		const FReal DeltaMag = Delta.Size();
		if (DeltaMag < UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FVec3 Dir = Delta / DeltaMag;

		for (const FConstPhysicsObjectHandle Object : InObjects)
		{
			const FTransform WorldTM = GetTransform(Object);

			VisitEveryShape(
				{ &Object, 1 },
				[&WorldTM, &InGeom, &StartTM, &bHit, &Delta, DeltaMag, &Dir, &OutBestHit, bSweepComplex](const FConstPhysicsObjectHandle IterObject, FPerShapeData* Shape)
				{
					check(Shape);

					FCollisionFilterData ShapeFilter = Shape->GetQueryData();
					const bool bShapeIsComplex = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::ComplexCollision)) != 0;
					const bool bShapeIsSimple = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::SimpleCollision)) != 0;
					if ((bSweepComplex && bShapeIsComplex) || (!bSweepComplex && bShapeIsSimple))
					{
						FVec3 WorldPosition;
						FVec3 WorldNormal;
						FReal Distance;
						int32 FaceIdx;
						FVec3 FaceNormal;

						const bool bShapeHit = Chaos::Utilities::CastHelper(
							InGeom,
							StartTM,
							[Shape, &WorldTM, &Dir, DeltaMag, &Distance, &WorldPosition, &WorldNormal, &FaceIdx, &FaceNormal](const auto& Downcast, const auto& FullTransformB)
							{
								return Chaos::SweepQuery(*Shape->GetGeometry(), WorldTM, Downcast, FullTransformB, Dir, DeltaMag, Distance, WorldPosition, WorldNormal, FaceIdx, FaceNormal, 0.f, false);
							}
						);

						if (bShapeHit)
						{
							bHit = true;

							OutBestHit.Shape = Shape;
							OutBestHit.WorldPosition = WorldPosition;
							OutBestHit.WorldNormal = WorldNormal;
							OutBestHit.Distance = static_cast<float>(Distance);
							OutBestHit.FaceIndex = FaceIdx;
							if (OutBestHit.Distance > 0.f)
							{
								const FVector LocalPosition = WorldTM.InverseTransformPositionNoScale(OutBestHit.WorldPosition);
								const FVector LocalUnitDir = WorldTM.InverseTransformVectorNoScale(Dir);
								OutBestHit.FaceIndex = Shape->GetGeometry()->FindMostOpposingFace(LocalPosition, LocalUnitDir, OutBestHit.FaceIndex, 1);
							}
							if constexpr (Id == EThreadContext::External)
							{
								OutBestHit.Actor = IterObject->GetParticle<Id>();
							}
						}
					}
					return false;
				}
			);

		}
		return bHit;
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