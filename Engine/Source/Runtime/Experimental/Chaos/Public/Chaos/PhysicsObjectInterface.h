// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Interface/SQTypes.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/ShapeInstanceFwd.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Framework/Threading.h"
#include "Containers/ArrayView.h"
#include "Math/MathFwd.h"
#include "UObject/ObjectMacros.h"

#include "PhysicsObjectInterface.generated.h"

class FChaosScene;
class IPhysicsProxyBase;

USTRUCT(BlueprintType)
struct CHAOS_API FClosestPhysicsObjectResult
{
	GENERATED_BODY()

	Chaos::FPhysicsObjectHandle PhysicsObject = nullptr;
	FVector ClosestLocation;
	double ClosestDistance = 0.0;
	operator bool() const
	{
		return IsValid();
	}
	bool IsValid() const
	{
		return PhysicsObject != nullptr;
	}

	FName HitName() const;
};

namespace Chaos
{
	class FPBDRigidsSolver;
	class FPerShapeData;
	struct FMTDInfo;

	struct CHAOS_API FOverlapInfo
	{
		FMTDInfo* MTD = nullptr;
		FBox* AxisOverlap = nullptr;
	};

	/**
	 * FReadPhysicsObjectInterface will assume that these operations are safe to call (i.e. the relevant scenes have been read locked on the game thread).
	 */
	template<EThreadContext Id>
	class CHAOS_API FReadPhysicsObjectInterface
	{
	public:
		FPhysicsObjectHandle GetRootObject(const FConstPhysicsObjectHandle Object);
		bool HasChildren(const FConstPhysicsObjectHandle Object);

		FTransform GetTransform(const FConstPhysicsObjectHandle Object);
		FVector GetX(const FConstPhysicsObjectHandle Object);
		FVector GetCoM(const FConstPhysicsObjectHandle Object);
		FVector GetWorldCoM(const FConstPhysicsObjectHandle Object);
		FQuat GetR(const FConstPhysicsObjectHandle Object);
		FSpatialAccelerationIdx GetSpatialIndex(const FConstPhysicsObjectHandle Object);

		TThreadParticle<Id>* GetParticle(const FConstPhysicsObjectHandle Object);
		TArray<TThreadParticle<Id>*> GetAllParticles(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		TArray<TThreadRigidParticle<Id>*> GetAllRigidParticles(TArrayView<const FConstPhysicsObjectHandle> InObjects);

		UE_DEPRECATED(5.3, "GetAllShapes has been deprecated. Please use GetAllThreadShapes instead.")
		TArray<FPerShapeData*> GetAllShapes(TArrayView<const FConstPhysicsObjectHandle> InObjects);

		TArray<TThreadShapeInstance<Id>*> GetAllThreadShapes(TArrayView<const FConstPhysicsObjectHandle> InObjects);

		// Returns true if a shape is found and we can stop iterating.
		void VisitEveryShape(TArrayView<const FConstPhysicsObjectHandle> InObjects, TFunctionRef<bool(const FConstPhysicsObjectHandle, TThreadShapeInstance<Id>*)> Lambda);

		UE_DEPRECATED(5.3, "GetPhysicsObjectOverlap has been deprecated. Please use the function for the specific overlap metric you wish to compute instead.")
		bool GetPhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FConstPhysicsObjectHandle ObjectB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap);

		UE_DEPRECATED(5.3, "GetPhysicsObjectOverlapWithTransform has been deprecated. Please use the function for the specific overlap metric you wish to compute instead.")
		bool GetPhysicsObjectOverlapWithTransform(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap);

		// This function will not compute any overlap heuristic.
		bool PhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex);

		// Returns all the overlaps within A given a shape B.
		template<typename TOverlapHit>
		bool PhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, TArray<TOverlapHit>& OutOverlaps)
		{
			static_assert(std::is_same_v<TOverlapHit, ChaosInterface::TThreadOverlapHit<Id>>);
			return PairwiseShapeOverlapHelper(
				ObjectA,
				InTransformA,
				ObjectB,
				InTransformB,
				bTraceComplex,
				false,
				FVector::Zero(),
				[this, ObjectA, &OutOverlaps](const FShapeOverlapData& A , const FShapeOverlapData& B, const FMTDInfo&)
				{
					ChaosInterface::FOverlapHit Overlap;
					Overlap.Shape = A.Shape;
					Overlap.Actor = GetParticle(ObjectA);
					OutOverlaps.Add(Overlap);
					return false;
				}
			);
		}

		// This function does the same as GetPhysicsObjectOverlap but also computes the MTD metric.
		bool PhysicsObjectOverlapWithMTD(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, FMTDInfo& OutMTD);

		// This function does the same as GetPhysicsObjectOverlap but also computes the AABB overlap metric.
		bool PhysicsObjectOverlapWithAABB(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FBox& OutOverlap);
		bool PhysicsObjectOverlapWithAABBSize(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FVector& OutOverlapSize);

		bool AreAllValid(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		bool AreAllKinematic(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		bool AreAllSleeping(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		bool AreAllRigidBody(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		bool AreAllDynamic(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		bool AreAllDisabled(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		bool AreAllShapesQueryEnabled(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		float GetMass(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		FBox GetBounds(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		FBox GetWorldBounds(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		FClosestPhysicsObjectResult GetClosestPhysicsBodyFromLocation(TArrayView<const FConstPhysicsObjectHandle> InObjects, const FVector& WorldLocation);
		FAccelerationStructureHandle CreateAccelerationStructureHandle(const FConstPhysicsObjectHandle Handle);

		template<typename TRaycastHit>
		bool LineTrace(TArrayView<const FConstPhysicsObjectHandle> InObjects, const FVector& WorldStart, const FVector& WorldEnd, bool bTraceComplex, TRaycastHit& OutBestHit)
		{
			static_assert(std::is_same_v<TRaycastHit, ChaosInterface::TThreadRaycastHit<Id>>);
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
					[this, &bHit, &WorldTM, &LocalStart, &LocalDelta, &Delta, DeltaMag, &BestWorldTM, bTraceComplex, &OutBestHit](const FConstPhysicsObjectHandle IterObject, TThreadShapeInstance<Id>* Shape)
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
									OutBestHit.Actor = GetParticle(IterObject);
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

		template<typename TOverlapHit>
		bool ShapeOverlap(TArrayView<const FConstPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& GeomTransform, TArray<TOverlapHit>& OutOverlaps)
		{
			static_assert(std::is_same_v<TOverlapHit, ChaosInterface::TThreadOverlapHit<Id>>);
			bool bHasOverlap = false;
			for (const FConstPhysicsObjectHandle Object : InObjects)
			{
				const FTransform WorldTM = GetTransform(Object);

				VisitEveryShape(
					{ &Object, 1 },
					[this, &bHasOverlap, &WorldTM, &InGeom, &GeomTransform, &OutOverlaps](const FConstPhysicsObjectHandle IterObject, TThreadShapeInstance<Id>* Shape)
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
							Overlap.Actor = GetParticle(IterObject);
							OutOverlaps.Add(Overlap);
						}
						return false;
					}
				);
			}
			return bHasOverlap;
		}

		template<typename TSweepHit>
		bool ShapeSweep(TArrayView<const FConstPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& StartTM, const FVector& EndPos, bool bSweepComplex, TSweepHit& OutBestHit)
		{
			static_assert(std::is_same_v<TSweepHit, ChaosInterface::TThreadSweepHit<Id>>);
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
					[this, &WorldTM, &InGeom, &StartTM, &bHit, &Delta, DeltaMag, &Dir, &OutBestHit, bSweepComplex](const FConstPhysicsObjectHandle IterObject, TThreadShapeInstance<Id>* Shape)
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
								OutBestHit.Actor = GetParticle(IterObject);
							}
						}
						return false;
					}
				);

			}
			return bHit;
		}

		friend class FPhysicsObjectInterface;
	protected:
		FReadPhysicsObjectInterface() = default;

	private:
		struct FShapeOverlapData
		{
			TThreadShapeInstance<Id>* Shape;
			FAABB3 BoundingBox;
		};

		/**
		 * For every pair of shapes that overlap, allows the caller to perform some computation. If additional pairs of shapes need to be examined, the input TFunction should return true.
		 */
		bool PairwiseShapeOverlapHelper(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, bool bComputeMTD, const FVector& Tolerance, const TFunction<bool(const FShapeOverlapData&, const FShapeOverlapData&, const FMTDInfo&)>& Lambda);
	};

	using FReadPhysicsObjectInterface_External = FReadPhysicsObjectInterface<EThreadContext::External>;
	using FReadPhysicsObjectInterface_Internal = FReadPhysicsObjectInterface<EThreadContext::Internal>;

	/**
	 * FReadPhysicsObjectInterface will assume that these operations are safe to call (i.e. the relevant scenes have been read locked on the physics thread).
	 */
	template<EThreadContext Id>
	class CHAOS_API FWritePhysicsObjectInterface: public FReadPhysicsObjectInterface<Id>
	{
	public:
		void PutToSleep(TArrayView<const FPhysicsObjectHandle> InObjects);
		void WakeUp(TArrayView<const FPhysicsObjectHandle> InObjects);
		void AddForce(TArrayView<const FPhysicsObjectHandle> InObjects, const FVector& Force, bool bInvalidate);
		void AddTorque(TArrayView<const FPhysicsObjectHandle> InObjects, const FVector& Torque, bool bInvalidate);
		
		void UpdateShapeCollisionFlags(TArrayView<const FPhysicsObjectHandle> InObjects, bool bSimCollision, bool bQueryCollision);
		void UpdateShapeFilterData(TArrayView<const FPhysicsObjectHandle> InObjects, const FCollisionFilterData& QueryData, const FCollisionFilterData& SimData);

		template<typename TPayloadType, typename T, int d>
		void AddToSpatialAcceleration(TArrayView<const FPhysicsObjectHandle> InObjects, ISpatialAcceleration<TPayloadType, T, d>* SpatialAcceleration)
		{
			if (!SpatialAcceleration)
			{
				return;
			}

			for (const FConstPhysicsObjectHandle Handle : InObjects)
			{
				const FBox WorldBounds = this->GetWorldBounds({ &Handle, 1 });
				const FAABB3 ChaosWorldBounds{ WorldBounds.Min, WorldBounds.Max };
				FAccelerationStructureHandle AccelerationHandle = this->CreateAccelerationStructureHandle(Handle);
				SpatialAcceleration->UpdateElementIn(AccelerationHandle, ChaosWorldBounds, true, this->GetSpatialIndex(Handle));
			}
		}

		template<typename TPayloadType, typename T, int d>
		void RemoveFromSpatialAcceleration(TArrayView<const FPhysicsObjectHandle> InObjects, ISpatialAcceleration<TPayloadType, T, d>* SpatialAcceleration)
		{
			if (!SpatialAcceleration)
			{
				return;
			}

			for (const FConstPhysicsObjectHandle Handle : InObjects)
			{
				FAccelerationStructureHandle AccelerationHandle = this->CreateAccelerationStructureHandle(Handle);
				SpatialAcceleration->RemoveElementFrom(AccelerationHandle, this->GetSpatialIndex(Handle));
			}
		}

		friend class FPhysicsObjectInterface;
	protected:
		FWritePhysicsObjectInterface() = default;
	};

	using FWritePhysicsObjectInterface_External = FWritePhysicsObjectInterface<EThreadContext::External>;
	using FWritePhysicsObjectInterface_Internal = FWritePhysicsObjectInterface<EThreadContext::Internal>;

	/**
	 * The FPhysicsObjectInterface is primarily used to perform maintenance operations on the FPhysicsObject.
	 * Any operations on the underlying particle/particle handle should use the FReadPhysicsObjectInterface and
	 * FWritePhysicsObjectInterface.
	 */
	class CHAOS_API FPhysicsObjectInterface
	{
	public:
		static void SetName(const FPhysicsObjectHandle Object, const FName& InName);
		static FName GetName(const FConstPhysicsObjectHandle Object);

		static void SetId(const FPhysicsObjectHandle Object, int32 InId);
		static int32 GetId(const FConstPhysicsObjectHandle Object);

		static FPBDRigidsSolver* GetSolver(TArrayView<const FConstPhysicsObjectHandle> InObjects);
		static IPhysicsProxyBase* GetProxy(TArrayView<const FConstPhysicsObjectHandle> InObjects);

	protected:
		// This function should not be called without an appropriate read-lock on the relevant scene.
		template<EThreadContext Id>
		static FReadPhysicsObjectInterface<Id> CreateReadInterface() { return FReadPhysicsObjectInterface<Id>{}; }

		// This function should not be called without an appropriate write-lock on the relevant scene.
		template<EThreadContext Id>
		static FWritePhysicsObjectInterface<Id> CreateWriteInterface() { return FWritePhysicsObjectInterface<Id>{}; }
	};
}