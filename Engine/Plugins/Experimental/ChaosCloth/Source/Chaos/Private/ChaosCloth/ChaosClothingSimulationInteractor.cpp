// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothingSimulationInteractor.h"
#include "ChaosCloth/ChaosClothingSimulationCloth.h"
#include "ChaosCloth/ChaosClothingSimulation.h"

using namespace Chaos;

void UChaosClothingInteractor::Sync(IClothingSimulation* Simulation)
{
	check(Simulation);

	if (FClothingSimulationCloth* const Cloth = static_cast<FClothingSimulation*>(Simulation)->GetCloth(ClothingId))
	{
		for (FChaosClothingInteractorCommand& Command : Commands)
		{
			Command.Execute(Cloth);
		}
		Commands.Reset();
	}

	// Call to base class' sync
	UClothingInteractor::Sync(Simulation);
}

void UChaosClothingInteractor::SetMaterialLinear(float EdgeStiffness, float BendingStiffness, float AreaStiffness)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([EdgeStiffness, BendingStiffness, AreaStiffness](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetMaterialProperties(EdgeStiffness, BendingStiffness, AreaStiffness);
	}));
}

void UChaosClothingInteractor::SetLongRangeAttachmentLinear(float TetherStiffnessLinear)
{
	// Deprecated
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([TetherStiffnessLinear](FClothingSimulationCloth* Cloth)
	{
		const FVec2 TetherStiffness((FMath::Clamp(FMath::Loge(TetherStiffnessLinear) / FMath::Loge(1.e3f) + 1.f, 0.f, 1.f)), 1.f);
		Cloth->SetLongRangeAttachmentProperties(TetherStiffness);
	}));
}

void UChaosClothingInteractor::SetLongRangeAttachment(FVector2D TetherStiffness)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([TetherStiffness](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetLongRangeAttachmentProperties(TetherStiffness);
	}));
}

void UChaosClothingInteractor::SetCollision(float CollisionThickness, float FrictionCoefficient, bool bUseCCD, float SelfCollisionThickness)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([CollisionThickness, FrictionCoefficient, bUseCCD, SelfCollisionThickness](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetCollisionProperties(CollisionThickness, FrictionCoefficient, bUseCCD, SelfCollisionThickness);
	}));
}

void UChaosClothingInteractor::SetDamping(float DampingCoefficient)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([DampingCoefficient](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetDampingProperties(DampingCoefficient);
	}));
}

void UChaosClothingInteractor::SetAerodynamics(float DragCoefficient, float LiftCoefficient, FVector WindVelocity)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([DragCoefficient, LiftCoefficient, WindVelocity](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetAerodynamicsProperties(DragCoefficient, LiftCoefficient, WindVelocity);
	}));
}

void UChaosClothingInteractor::SetGravity(float GravityScale, bool bIsGravityOverridden, FVector GravityOverride)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([GravityScale, bIsGravityOverridden, GravityOverride](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetGravityProperties(GravityScale, bIsGravityOverridden, GravityOverride);
	}));
}

void UChaosClothingInteractor::SetAnimDriveLinear(float AnimDriveStiffnessLinear)
{
	// Deprecated
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([AnimDriveStiffnessLinear](FClothingSimulationCloth* Cloth)
	{
		// The Anim Drive stiffness Low value needs to be 0 in order to keep backward compatibility with existing mask (this wouldn't be an issue if this property had no legacy mask)
		const FVec2 AnimDriveStiffness(0.f, FMath::Clamp(FMath::Loge(AnimDriveStiffnessLinear) / FMath::Loge(1.e3f) + 1.f, 0.f, 1.f));
		const FVec2 AnimDriveDamping(0.f, 1.f);
		Cloth->SetAnimDriveProperties(AnimDriveStiffness, AnimDriveDamping);
	}));
}

void UChaosClothingInteractor::SetAnimDrive(FVector2D AnimDriveStiffness, FVector2D AnimDriveDamping)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([AnimDriveStiffness, AnimDriveDamping](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetAnimDriveProperties(FVec2(AnimDriveStiffness.X, AnimDriveStiffness.Y), FVec2(AnimDriveDamping.X, AnimDriveDamping.Y));
	}));
}

void UChaosClothingInteractor::SetVelocityScale(FVector LinearVelocityScale, float AngularVelocityScale, float FictitiousAngularScale)
{
	Commands.Add(FChaosClothingInteractorCommand::CreateLambda([LinearVelocityScale, AngularVelocityScale, FictitiousAngularScale](FClothingSimulationCloth* Cloth)
	{
		Cloth->SetVelocityScaleProperties(LinearVelocityScale, AngularVelocityScale, FictitiousAngularScale);
	}));
}

void UChaosClothingInteractor::ResetAndTeleport(bool bReset, bool bTeleport)
{
	if (bReset)
	{
		Commands.Add(FChaosClothingInteractorCommand::CreateLambda([](FClothingSimulationCloth* Cloth)
		{
			Cloth->Reset();
		}));
	}
	if (bTeleport)
	{
		Commands.Add(FChaosClothingInteractorCommand::CreateLambda([](FClothingSimulationCloth* Cloth)
		{
			Cloth->Teleport();
		}));
	}
}

void UChaosClothingSimulationInteractor::Sync(IClothingSimulation* Simulation, IClothingSimulationContext* Context)
{
	check(Simulation);
	check(Context);

	for (FChaosClothingSimulationInteractorCommand& Command : Commands)
	{
		Command.Execute(static_cast<FClothingSimulation*>(Simulation), static_cast<FClothingSimulationContext*>(Context));
	}
	Commands.Reset();

	// Call base class' sync 
	UClothingSimulationInteractor::Sync(Simulation, Context);
}

void UChaosClothingSimulationInteractor::PhysicsAssetUpdated()
{
	Commands.Add(FChaosClothingSimulationInteractorCommand::CreateLambda([](FClothingSimulation* Simulation, FClothingSimulationContext* /*Context*/)
	{
		Simulation->RefreshPhysicsAsset();
	}));
}

void UChaosClothingSimulationInteractor::ClothConfigUpdated()
{
	Commands.Add(FChaosClothingSimulationInteractorCommand::CreateLambda([](FClothingSimulation* Simulation, FClothingSimulationContext* Context)
	{
		Simulation->RefreshClothConfig(Context);
	}));
}

void UChaosClothingSimulationInteractor::SetAnimDriveSpringStiffness(float Stiffness)
{
	// Set the anim drive stiffness through the ChaosClothInteractor to allow the value to be overridden by the cloth interactor if needed
	for (const TPair<FName, UClothingInteractor*>& ClothingInteractor : UClothingSimulationInteractor::ClothingInteractors)
	{
		if (UChaosClothingInteractor* const ChaosClothingInteractor = Cast<UChaosClothingInteractor>(ClothingInteractor.Value))
		{
			ChaosClothingInteractor->SetAnimDriveLinear(Stiffness);
		}
	}
}

void UChaosClothingSimulationInteractor::EnableGravityOverride(const FVector& Gravity)
{
	Commands.Add(FChaosClothingSimulationInteractorCommand::CreateLambda([Gravity](FClothingSimulation* Simulation, FClothingSimulationContext* /*Context*/)
	{
		Simulation->SetGravityOverride(Gravity);
	}));
}

void UChaosClothingSimulationInteractor::DisableGravityOverride()
{
	Commands.Add(FChaosClothingSimulationInteractorCommand::CreateLambda([](FClothingSimulation* Simulation, FClothingSimulationContext* /*Context*/)
	{
		Simulation->DisableGravityOverride();
	}));
}

void UChaosClothingSimulationInteractor::SetNumIterations(int32 InNumIterations)
{
	Commands.Add(FChaosClothingSimulationInteractorCommand::CreateLambda([InNumIterations](FClothingSimulation* Simulation, FClothingSimulationContext* /*Context*/)
	{
		Simulation->SetNumIterations(InNumIterations);
	}));
}

void UChaosClothingSimulationInteractor::SetNumSubsteps(int32 InNumSubsteps)
{
	Commands.Add(FChaosClothingSimulationInteractorCommand::CreateLambda([InNumSubsteps](FClothingSimulation* Simulation, FClothingSimulationContext* /*Context*/)
	{
		Simulation->SetNumSubsteps(InNumSubsteps);
	}));
}

UClothingInteractor* UChaosClothingSimulationInteractor::CreateClothingInteractor()
{
	return NewObject<UChaosClothingInteractor>(this);
}
