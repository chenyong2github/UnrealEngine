// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/PhysicsObject.h"
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

		TArray<FPerShapeData*> GetAllShapes(TArrayView<FPhysicsObjectHandle> InObjects);
		bool GetPhysicsObjectOverlap(FPhysicsObjectHandle ObjectA, FPhysicsObjectHandle ObjectB, bool bTraceComplex, Chaos::FMTDInfo* OutMTD);

		bool AreAllValid(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllKinematic(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllSleeping(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllRigidBody(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllDynamic(TArrayView<FPhysicsObjectHandle> InObjects);
		bool AreAllDisabled(TArrayView<FPhysicsObjectHandle> InObjects);
		float GetMass(TArrayView<FPhysicsObjectHandle> InObjects);
		FBox GetBounds(TArrayView<FPhysicsObjectHandle> InObjects);
		FBox GetWorldBounds(TArrayView<FPhysicsObjectHandle> InObjects);
		FClosestPhysicsObjectResult GetClosestPhysicsBodyFromLocation(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& WorldLocation);

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
		void PutToSleep(TArrayView<FPhysicsObjectHandle> InObjects);
		void WakeUp(TArrayView<FPhysicsObjectHandle> InObjects);
		void AddForce(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& Force, bool bInvalidate);
		void AddTorque(TArrayView<FPhysicsObjectHandle> InObjects, const FVector& Torque, bool bInvalidate);

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