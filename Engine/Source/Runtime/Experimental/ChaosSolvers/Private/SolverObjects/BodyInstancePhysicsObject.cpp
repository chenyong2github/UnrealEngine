// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SolverObjects/BodyInstancePhysicsObject.h"
#include "PBDRigidsSolver.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "ChaosStats.h"

#if INCLUDE_CHAOS


FBodyInstancePhysicsObject::FBodyInstancePhysicsObject(UObject* InOwner, FInitialState InInitialState)
	: TSolverObject<FBodyInstancePhysicsObject>(InOwner)
	, bInitialized(false)
	, InitialState(InInitialState)
{
}


FBodyInstancePhysicsObject::~FBodyInstancePhysicsObject()
{
	for (int i = 0; i < ImplicitObjects_GameThread.Num(); i++)
	{
		delete ImplicitObjects_GameThread[i];
	}
	ImplicitObjects_GameThread.Empty();
}

void FBodyInstancePhysicsObject::CreateRigidBodyCallback(FParticlesType& InOutParticles)
{
	if(!bInitialized)
	{
		bInitialized = true;
	}
}

void FBodyInstancePhysicsObject::OnRemoveFromScene()
{
	// Disable the particle we added
	Chaos::FPBDRigidsSolver* CurrSolver = GetSolver();

	if(CurrSolver && InitializedIndices.Num() > 0)
	{
		// #BG TODO Special case here because right now we reset/realloc the evolution per geom component
		// in endplay which clears this out. That needs to not happen and be based on world shutdown
		if(CurrSolver->GetRigidParticles().Size() == 0)
		{
			return;
		}

		for(const int32 Index : InitializedIndices)
		{
			CurrSolver->GetRigidParticles().Disabled(Index) = true;
			CurrSolver->ActiveIndices().Remove(Index);
			CurrSolver->NonDisabledIndices().Remove(Index);
		}
	}

	SetSolver(nullptr);
	bInitialized = false;
}

#endif