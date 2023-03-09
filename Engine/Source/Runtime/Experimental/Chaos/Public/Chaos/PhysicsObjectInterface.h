// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Interface/SQTypes.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PhysicsObject.h"
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
		FPhysicsObjectHandle GetRootObject(FPhysicsObjectHandle Object);
		bool HasChildren(FPhysicsObjectHandle Object);

		FTransform GetTransform(FPhysicsObjectHandle Object);
		FVector GetX(FPhysicsObjectHandle Object);
		FVector GetCoM(FPhysicsObjectHandle Object);
		FVector GetWorldCoM(FPhysicsObjectHandle Object);
		FQuat GetR(FPhysicsObjectHandle Object);
		FSpatialAccelerationIdx GetSpatialIndex(FPhysicsObjectHandle Object);

		TArray<TThreadParticle<Id>*> GetAllParticles(TArrayView<FPhysicsObjectHandle> InObjects);
		TArray<TThreadRigidParticle<Id>*> GetAllRigidParticles(TArrayView<FPhysicsObjectHandle> InObjects);
		TArray<FPerShapeData*> GetAllShapes(TArrayView<FPhysicsObjectHandle> InObjects);

		// Returns true if a shape is found and we can stop iterating.
		void VisitEveryShape(TArrayView<FPhysicsObjectHandle> InObjects, TFunctionRef<bool(FPhysicsObjectHandle, FPerShapeData*)> Lambda);

		UE_DEPRECATED(5.3, "GetPhysicsObjectOverlap has been deprecated. Please use the function for the specific overlap metric you wish to compute instead.")
		bool GetPhysicsObjectOverlap(FPhysicsObjectHandle ObjectA, FPhysicsObjectHandle ObjectB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap);

		UE_DEPRECATED(5.3, "GetPhysicsObjectOverlapWithTransform has been deprecated. Please use the function for the specific overlap metric you wish to compute instead.")
		bool GetPhysicsObjectOverlapWithTransform(FPhysicsObjectHandle ObjectA, const FTransform& InTransformA, FPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap);

		// This function will not compute any overlap heuristic.
		bool GetPhysicsObjectOverlap(FPhysicsObjectHandle ObjectA, const FTransform& InTransformA, FPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex);

		// This function does the same as GetPhysicsObjectOverlap but also computes the MTD metric.
		bool GetPhysicsObjectOverlapWithMTD(FPhysicsObjectHandle ObjectA, const FTransform& InTransformA, FPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, FMTDInfo& OutMTD);

		// This function does the same as GetPhysicsObjectOverlap but also computes the AABB overlap metric.
		bool GetPhysicsObjectOverlapWithAABB(FPhysicsObjectHandle ObjectA, const FTransform& InTransformA, FPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FBox& OutOverlap);
		bool GetPhysicsObjectOverlapWithAABBSize(FPhysicsObjectHandle ObjectA, const FTransform& InTransformA, FPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FVector& OutOverlapSize);

		bool AreAllValid(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllKinematic(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllSleeping(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllRigidBody(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllDynamic(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllDisabled(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllShapesQueryEnabled(TArrayView<FPhysicsObjectHandle> InObjects);
		float GetMass(TArrayView<FPhysicsObjectHandle> InObjects);
		FBox GetBounds(TArrayView<FPhysicsObjectHandle> InObjects);
		FBox GetWorldBounds(TArrayView<FPhysicsObjectHandle> InObjects);
		FClosestPhysicsObjectResult GetClosestPhysicsBodyFromLocation(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& WorldLocation);
		FAccelerationStructureHandle CreateAccelerationStructureHandle(FPhysicsObjectHandle Handle);

		bool LineTrace(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& WorldStart, const FVector& WorldEnd, bool bTraceComplex, ChaosInterface::FRaycastHit& OutBestHit);
		bool ShapeOverlap(TArrayView<FPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& GeomTransform, TArray<ChaosInterface::FOverlapHit>& OutOverlaps);
		bool ShapeSweep(TArrayView<FPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& StartTM, const FVector& EndPos, bool bSweepComplex, ChaosInterface::FSweepHit& OutBestHit);

		friend class FPhysicsObjectInterface;
	protected:
		FReadPhysicsObjectInterface() = default;

	private:
		struct FShapeOverlapData
		{
			FPerShapeData* Shape;
			FAABB3 BoundingBox;
		};

		/**
		 * For every pair of shapes that overlap, allows the caller to perform some computation. If additional pairs of shapes need to be examined, the input TFunction should return true.
		 */
		bool PairwiseShapeOverlapHelper(FPhysicsObjectHandle ObjectA, const FTransform& InTransformA, FPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, bool bComputeMTD, const FVector& Tolerance, const TFunction<bool(const FShapeOverlapData&, const FShapeOverlapData&, const FMTDInfo&)>& Lambda);
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
		void PutToSleep(TArrayView<FPhysicsObjectHandle> InObjects);
		void WakeUp(TArrayView<FPhysicsObjectHandle> InObjects);
		void AddForce(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& Force, bool bInvalidate);
		void AddTorque(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& Torque, bool bInvalidate);
		
		void UpdateShapeCollisionFlags(TArrayView<FPhysicsObjectHandle> InObjects, bool bSimCollision, bool bQueryCollision);
		void UpdateShapeFilterData(TArrayView<FPhysicsObjectHandle> InObjects, const FCollisionFilterData& QueryData, const FCollisionFilterData& SimData);

		template<typename TPayloadType, typename T, int d>
		void AddToSpatialAcceleration(TArrayView<FPhysicsObjectHandle> InObjects, ISpatialAcceleration<TPayloadType, T, d>* SpatialAcceleration)
		{
			if (!SpatialAcceleration)
			{
				return;
			}

			for (FPhysicsObjectHandle Handle : InObjects)
			{
				const FBox WorldBounds = this->GetWorldBounds({ &Handle, 1 });
				const FAABB3 ChaosWorldBounds{ WorldBounds.Min, WorldBounds.Max };
				FAccelerationStructureHandle AccelerationHandle = this->CreateAccelerationStructureHandle(Handle);
				SpatialAcceleration->UpdateElementIn(AccelerationHandle, ChaosWorldBounds, true, this->GetSpatialIndex(Handle));
			}
		}

		template<typename TPayloadType, typename T, int d>
		void RemoveFromSpatialAcceleration(TArrayView<FPhysicsObjectHandle> InObjects, ISpatialAcceleration<TPayloadType, T, d>* SpatialAcceleration)
		{
			if (!SpatialAcceleration)
			{
				return;
			}

			for (FPhysicsObjectHandle Handle : InObjects)
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
		static void SetName(FPhysicsObjectHandle Object, const FName& InName);
		static FName GetName(FPhysicsObjectHandle Object);

		static void SetId(FPhysicsObjectHandle Object, int32 InId);
		static int32 GetId(FPhysicsObjectHandle Object);

		static FPBDRigidsSolver* GetSolver(TArrayView<FPhysicsObjectHandle> InObjects);
		static IPhysicsProxyBase* GetProxy(TArrayView<FPhysicsObjectHandle> InObjects);

	protected:
		// This function should not be called without an appropriate read-lock on the relevant scene.
		template<EThreadContext Id>
		static FReadPhysicsObjectInterface<Id> CreateReadInterface() { return FReadPhysicsObjectInterface<Id>{}; }

		// This function should not be called without an appropriate write-lock on the relevant scene.
		template<EThreadContext Id>
		static FWritePhysicsObjectInterface<Id> CreateWriteInterface() { return FWritePhysicsObjectInterface<Id>{}; }
	};
}