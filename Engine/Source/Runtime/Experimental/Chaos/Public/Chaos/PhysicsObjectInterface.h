// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

		UE_DEPRECATED(5.3, "GetPhysicsObjectOverlap has been deprecated. Please use the function for the specific overlap metric you wish to compute instead in the FPhysicsObjectCollisionInterface.")
		bool GetPhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FConstPhysicsObjectHandle ObjectB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap);

		UE_DEPRECATED(5.3, "GetPhysicsObjectOverlapWithTransform has been deprecated. Please use the function for the specific overlap metric you wish to compute instead in the FPhysicsObjectCollisionInterface.")
		bool GetPhysicsObjectOverlapWithTransform(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, Chaos::FOverlapInfo& OutOverlap);

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

		friend class FPhysicsObjectInterface;
	protected:
		FReadPhysicsObjectInterface() = default;
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