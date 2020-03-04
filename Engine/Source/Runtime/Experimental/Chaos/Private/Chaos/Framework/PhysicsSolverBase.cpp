// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"

namespace Chaos
{	
	void FPhysicsSolverBase::ChangeBufferMode(EMultiBufferMode InBufferMode)
	{
		BufferMode = InBufferMode;
	}

	FPhysicsSolverBase::FPhysicsSolverBase(const EMultiBufferMode BufferingModeIn)
	: BufferMode(BufferingModeIn)
	{}

	FPhysicsSolverBase::~FPhysicsSolverBase() = default;
	FPhysicsSolverBase::FPhysicsSolverBase(FPhysicsSolverBase&&) = default;

	void FPhysicsSolverBase::SetEvolution(TUniquePtr<FPBDRigidsEvolutionGBF>&& Evolution)
	{
		MEvolution = MoveTemp(Evolution);
	}
}
