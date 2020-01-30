// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsAssetEditorAnimInstanceProxy.h"
#include "PhysicsAssetEditorAnimInstance.h"

#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsJointHandle.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsSimulation.h"


FPhysicsAssetEditorAnimInstanceProxy::FPhysicsAssetEditorAnimInstanceProxy()
	: TargetActor(nullptr)
	, HandleActor(nullptr)
	, HandleJoint(nullptr)
{
}

FPhysicsAssetEditorAnimInstanceProxy::FPhysicsAssetEditorAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimPreviewInstanceProxy(InAnimInstance)
	, TargetActor(nullptr)
	, HandleActor(nullptr)
	, HandleJoint(nullptr)
{
}

FPhysicsAssetEditorAnimInstanceProxy::~FPhysicsAssetEditorAnimInstanceProxy()
{
}

void FPhysicsAssetEditorAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimPreviewInstanceProxy::Initialize(InAnimInstance);
	ConstructNodes();

#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
	UPhysicsAsset* PhysicsAsset = InAnimInstance->GetSkelMeshComponent()->GetPhysicsAsset();
	if (PhysicsAsset != nullptr)
	{
		SolverIterations = PhysicsAsset->SolverIterations;
	}
#endif
}

void FPhysicsAssetEditorAnimInstanceProxy::ConstructNodes()
{
	ComponentToLocalSpace.ComponentPose.SetLinkNode(&RagdollNode);
	
	RagdollNode.SimulationSpace = ESimulationSpace::WorldSpace;
	RagdollNode.ActualAlpha = 1.0f;
}

FAnimNode_Base* FPhysicsAssetEditorAnimInstanceProxy::GetCustomRootNode()
{
	return &ComponentToLocalSpace;
}

void FPhysicsAssetEditorAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(&RagdollNode);
	OutNodes.Add(&ComponentToLocalSpace);
}

void FPhysicsAssetEditorAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	if (CurrentAsset != nullptr)
	{
		FAnimPreviewInstanceProxy::UpdateAnimationNode(InContext);
	}
	else
	{
		ComponentToLocalSpace.Update_AnyThread(InContext);
	}
}

bool FPhysicsAssetEditorAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
	ImmediatePhysics::FSimulation* Simulation = RagdollNode.GetSimulation();
	if (Simulation != nullptr)
	{
		Simulation->SetSolverIterations(
			SolverIterations.SolverIterations,
			SolverIterations.JointIterations,
			SolverIterations.CollisionIterations,
			SolverIterations.SolverPushOutIterations,
			SolverIterations.JointPushOutIterations,
			SolverIterations.CollisionPushOutIterations
		);
	}
#endif

	if (CurrentAsset != nullptr)
	{
		return FAnimPreviewInstanceProxy::Evaluate_WithRoot(Output, InRootNode);
	}
	else
	{
		InRootNode->Evaluate_AnyThread(Output);
		return true;
	}
}

void FPhysicsAssetEditorAnimInstanceProxy::AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName)
{
	RagdollNode.AddImpulseAtLocation(Impulse, Location, BoneName);
}

void FPhysicsAssetEditorAnimInstanceProxy::Grab(FName InBoneName, const FVector& Location, const FRotator& Rotation, bool bRotationConstrained)
{
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
	ImmediatePhysics::FSimulation* Simulation = RagdollNode.GetSimulation();

	if (TargetActor != nullptr)
	{
		Ungrab();
	}

	for (int ActorIndex = 0; ActorIndex < Simulation->NumActors(); ++ActorIndex)
	{
		if (Simulation->GetActorHandle(ActorIndex)->GetName() == InBoneName)
		{
			TargetActor = Simulation->GetActorHandle(ActorIndex);
			break;
		}
	}

	if (TargetActor != nullptr)
	{
		FTransform HandleTransform = FTransform(Rotation, Location);
		HandleActor = Simulation->CreateKinematicActor(nullptr, HandleTransform);
		HandleActor->SetWorldTransform(HandleTransform);
		HandleActor->SetKinematicTarget(HandleTransform);

		HandleJoint = Simulation->CreateJoint(nullptr, TargetActor, HandleActor);
	}
#endif
}

void FPhysicsAssetEditorAnimInstanceProxy::Ungrab()
{
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
	if (TargetActor != nullptr)
	{
		ImmediatePhysics::FSimulation* Simulation = RagdollNode.GetSimulation();
		Simulation->DestroyJoint(HandleJoint);
		Simulation->DestroyActor(HandleActor);
		TargetActor = nullptr;
		HandleActor = nullptr;
		HandleJoint = nullptr;
	}
#endif
}

void FPhysicsAssetEditorAnimInstanceProxy::UpdateHandleTransform(const FTransform& NewTransform)
{
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
	if (HandleActor != nullptr)
	{
		HandleActor->SetKinematicTarget(NewTransform);
	}
#endif
}

void FPhysicsAssetEditorAnimInstanceProxy::UpdateDriveSettings(bool bLinearSoft, float LinearStiffness, float LinearDamping)
{
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
	using namespace Chaos;
	if (HandleJoint != nullptr)
	{
		HandleJoint->SetSoftLinearSettings(bLinearSoft, LinearStiffness, LinearDamping);
	}
#endif
}

