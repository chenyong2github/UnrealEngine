// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Evolution/SolverBody.h"
#include "Chaos/Particle/ParticleUtilities.h"

namespace Chaos
{

	FSolverBody::FSolverBody()
		//: Particle(nullptr)
	{
	}

	//FSolverBody::FSolverBody(FGenericParticleHandle InParticle)
	//	//: Particle(InParticle)
	//{
	//	//GatherInput();

	//	// This will get set to something lower if there is a contact, based on the contact graph
	//	State.Level = TNumericLimits<int32>::Max();
	//}

	//void FSolverBody::GatherInput()
	//{
	//	if (Particle.IsValid())
	//	{
	//		FRigidTransform3 CoMTransform = FParticleUtilitiesPQ::GetCoMWorldTransform(Particle);
	//		State.P = CoMTransform.GetLocation();
	//		State.Q = CoMTransform.GetRotation();
	//		State.V = Particle->V();
	//		State.W = Particle->W();

	//		if (Particle->IsDynamic())
	//		{
	//			FRigidTransform3 PrevCoMTransform = FParticleUtilitiesXR::GetCoMWorldTransform(Particle);
	//			State.X = PrevCoMTransform.GetLocation();
	//			State.R = PrevCoMTransform.GetRotation();

	//			State.InvM = Particle->InvM();
	//			State.InvILocal = Particle->InvI().GetDiagonal();

	//			// This will get set to something lower if there is a contact, based on the contact graph
	//			State.Level = TNumericLimits<int32>::Max();
	//		}
	//		else
	//		{
	//			State.X = State.P;
	//			State.R = State.Q;
	//		}

	//		UpdateRotationDependentState();
	//	}
	//}

	//void FSolverBody::ScatterOutput()
	//{
	//	if (Particle.IsValid())
	//	{
	//		if (IsDynamic())
	//		{
	//			// Set the particle state
	//			FParticleUtilities::SetCoMWorldTransform(Particle, State.P, State.Q);
	//			Particle->SetV(State.V);
	//			Particle->SetW(State.W);

	//			if (State.bHasActiveCollision)
	//			{
	//				//Particle->AuxilaryValue(*ParticleParameters.Collided) = true;
	//			}
	//		}

	//		// Reset SolverBodyIndex cookie every step - it will be reassigned next step
	//		Particle->SetSolverBodyIndex(INDEX_NONE);
	//	}
	//}

	void FSolverBody::UpdateRotationDependentState()
	{
		if (IsDynamic())
		{
			State.InvI = Utilities::ComputeWorldSpaceInertia(State.Q, State.InvILocal);
		}
	}
}