// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Declares.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "UObject/GCObject.h"

enum class EPhysicsProxyType
{
	NoneType = 0,
	StaticMeshType = 1,
	GeometryCollectionType = 2,
	FieldType = 3,
	SkeletalMeshType = 4,
	SingleGeometryParticleType = 5,
	SingleKinematicParticleType = 6,
	SingleRigidParticleType = 7
};

class IPhysicsProxyBase
{
public:
	IPhysicsProxyBase(EPhysicsProxyType InType)
		: Solver(nullptr)
		, Type(InType)
	{}

	virtual UObject* GetOwner() const = 0;

	template< class SOLVER_TYPE = Chaos::FPhysicsSolver>
	SOLVER_TYPE* GetSolver() const { return static_cast<SOLVER_TYPE*>(Solver); }

	//Should this be in the public API? probably not
	template< class SOLVER_TYPE = Chaos::FPhysicsSolver>
	void SetSolver(SOLVER_TYPE* InSolver) { Solver = InSolver; }

	EPhysicsProxyType GetType() { return Type; }

	//todo: remove this
	virtual void* GetHandleUnsafe() const { check(false); return nullptr; }

protected:
	// Ensures that derived classes can successfully call this destructor
	// but no one can delete using a IPhysicsProxyBase*
	virtual ~IPhysicsProxyBase()
	{
		if (GetSolver<Chaos::FPhysicsSolverBase>())
		{
			GetSolver<Chaos::FPhysicsSolverBase>()->RemoveDirtyProxy(this);
		}
	}

	
	/** The solver that owns the solver object */
	Chaos::FPhysicsSolverBase* Solver;

	/** Proxy type */
	EPhysicsProxyType Type;
};

struct PhysicsProxyWrapper
{
	IPhysicsProxyBase* PhysicsProxy;
	EPhysicsProxyType Type;
};
