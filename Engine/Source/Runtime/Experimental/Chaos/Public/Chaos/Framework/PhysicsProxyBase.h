// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Declares.h"
#include "UObject/GCObject.h"
#include "Chaos/Core.h"

enum class EPhysicsProxyType : uint32
{
	NoneType = 0,
	StaticMeshType = 1,
	GeometryCollectionType = 2,
	FieldType = 3,
	SkeletalMeshType = 4,
	JointConstraintType = 8,	//left gap when removed some types in case these numbers actually matter to someone, should remove
	SuspensionConstraintType = 9,
	SingleParticleProxy,
	Count
};

namespace Chaos
{
	class FPhysicsSolverBase;
}

struct CHAOS_API FProxyTimestamp
{
	int32 XTimestamp = INDEX_NONE;
	int32 RTimestamp = INDEX_NONE;
	int32 VTimestamp = INDEX_NONE;
	int32 WTimestamp = INDEX_NONE;
	int32 ObjectStateTimestamp = INDEX_NONE;
	Chaos::FVec3 OverWriteX;
	Chaos::FRotation3 OverWriteR;
	Chaos::FVec3 OverWriteV;
	Chaos::FVec3 OverWriteW;
	bool bDeleted = false;
};

class CHAOS_API IPhysicsProxyBase
{
public:
	IPhysicsProxyBase(EPhysicsProxyType InType, UObject* InOwner)
		: Solver(nullptr)
		, Owner(InOwner)
		, DirtyIdx(INDEX_NONE)
		, Type(InType)
		, SyncTimestamp(new FProxyTimestamp)
	{}

	UObject* GetOwner() const { return Owner; }

	template< class SOLVER_TYPE>
	SOLVER_TYPE* GetSolver() const { return static_cast<SOLVER_TYPE*>(Solver); }

	Chaos::FPhysicsSolverBase* GetSolverBase() const { return Solver; }

	//Should this be in the public API? probably not
	template< class SOLVER_TYPE = Chaos::FPhysicsSolver>
	void SetSolver(SOLVER_TYPE* InSolver) { Solver = InSolver; }

	EPhysicsProxyType GetType() const { return Type; }

	//todo: remove this
	virtual void* GetHandleUnsafe() const { check(false); return nullptr; }

	int32 GetDirtyIdx() const { return DirtyIdx; }
	void SetDirtyIdx(const int32 Idx) { DirtyIdx = Idx; }
	void ResetDirtyIdx() { DirtyIdx = INDEX_NONE; }

	void MarkDeleted() { SyncTimestamp->bDeleted = true; }

	TSharedPtr<FProxyTimestamp,ESPMode::ThreadSafe> GetSyncTimestamp() const { return SyncTimestamp; }

	bool IsInitialized() const { return InitializedOnStep != INDEX_NONE; }
	void SetInitialized(const int32 InitializeStep) { InitializedOnStep = InitializeStep; }
	int32 GetInitializedStep() const { return InitializedOnStep; }


protected:
	// Ensures that derived classes can successfully call this destructor
	// but no one can delete using a IPhysicsProxyBase*
	virtual ~IPhysicsProxyBase();
	
	/** The solver that owns the solver object */
	Chaos::FPhysicsSolverBase* Solver;
	UObject* Owner;

private:
	int32 DirtyIdx;
protected:
	/** Proxy type */
	EPhysicsProxyType Type;
	TSharedPtr<FProxyTimestamp,ESPMode::ThreadSafe> SyncTimestamp;
	int32 InitializedOnStep = INDEX_NONE;
};

struct PhysicsProxyWrapper
{
	IPhysicsProxyBase* PhysicsProxy;
	EPhysicsProxyType Type;
};
