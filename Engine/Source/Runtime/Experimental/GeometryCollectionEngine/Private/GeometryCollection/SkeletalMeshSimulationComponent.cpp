// Copyright Epic Games, Inc. All Rights Reserved.
#include "GeometryCollection/SkeletalMeshSimulationComponent.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "GeometryCollection/PhysicsAssetSimulation.h"

#include "PhysicsSolver.h"
#include "ChaosSolversModule.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ErrorReporter.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "Physics/Experimental/PhysScene_Chaos.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"

#include "PhysXIncludes.h"

#include "DrawDebugHelpers.h"
#include "Async/ParallelFor.h"
#include "Math/Box.h"
#include "Math/NumericLimits.h"
#include "Modules/ModuleManager.h"
#include "Chaos/ChaosGameplayEventDispatcher.h"

#include "Chaos/ChaosSolverActor.h"
#include "Chaos/ChaosPhysicalMaterial.h"
#include "Physics/Experimental/PhysScene_Chaos.h"


//DEFINE_LOG_CATEGORY_STATIC(USkeletalMeshSimulationComponentLogging, NoLogging, All);

PRAGMA_DISABLE_DEPRECATION_WARNINGS

USkeletalMeshSimulationComponent::USkeletalMeshSimulationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)

	, PhysicalMaterial(nullptr)
	, ChaosSolverActor(nullptr)

	, bSimulating(true)
	, bNotifyCollisions(false)
	, ObjectType(EObjectStateTypeEnum::Chaos_Object_Kinematic)

	, Density(2.4)  // dense brick
	, MinMass(0.001)
	, MaxMass(1.e6)

	, CollisionType(ECollisionTypeEnum::Chaos_Volumetric)
	, ImplicitShapeParticlesPerUnitArea(0.1)
	, ImplicitShapeMinNumParticles(0)
	, ImplicitShapeMaxNumParticles(50)
	, MinLevelSetResolution(5)
	, MaxLevelSetResolution(10)
	, CollisionGroup(0)
#if 0
	, bEnableClustering(false)
	, ClusterGroupIndex(0)
	, MaxClusterLevel(100)
	, DamageThreshold(250.)
#endif
	, InitialVelocityType(EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
	, InitialLinearVelocity(0.f)
	, InitialAngularVelocity(0.f)
	, PhysicsProxy(nullptr)
{
	// Enable calls to TickComponent()
	UActorComponent::PrimaryComponentTick.bCanEverTick = true;	
	ChaosMaterial = MakeUnique<Chaos::FChaosPhysicsMaterial>();
}

USkeletalMeshSimulationComponent::USkeletalMeshSimulationComponent(FVTableHelper& Helper)
	: Super(Helper)
{

}

USkeletalMeshSimulationComponent::~USkeletalMeshSimulationComponent() = default;

Chaos::FPhysicsSolver* GetSolver(const USkeletalMeshSimulationComponent& SkeletalMeshSimulationComponent)
{
#if INCLUDE_CHAOS
	return	SkeletalMeshSimulationComponent.ChaosSolverActor != nullptr ?
		SkeletalMeshSimulationComponent.ChaosSolverActor->GetSolver() :
		SkeletalMeshSimulationComponent.GetOwner()->GetWorld()->PhysicsScene_Chaos->GetSolver();
#else
	return nullptr;
#endif
}

void USkeletalMeshSimulationComponent::OnCreatePhysicsState()
{
	// Skip the chain - don't care about body instance setup
	UActorComponent::OnCreatePhysicsState();

	const bool bValidWorld = GetWorld() && GetWorld()->IsGameWorld();

	AActor* OwningActor = GetOwner();
	USkeletalMeshComponent* SkelMeshComponent = OwningActor->FindComponentByClass<USkeletalMeshComponent>();

	// Need to see if we actually have a target for the component
	if (bValidWorld && SkelMeshComponent)
	{
		// Make sure the Skeletal Mesh component is updated before this one is.  
		// We don't need to worry about duplicate registrations.
		AddTickPrerequisiteComponent(SkelMeshComponent);

		//
		// Initialization lambda
		//

		if (PhysicalMaterial)
		{
			ChaosMaterial->Friction = PhysicalMaterial->Friction;
			ChaosMaterial->Restitution = PhysicalMaterial->Restitution;
			ChaosMaterial->SleepingLinearThreshold = PhysicalMaterial->SleepingLinearVelocityThreshold;
			ChaosMaterial->SleepingAngularThreshold = PhysicalMaterial->SleepingAngularVelocityThreshold;
		}

		auto InitFunc = [this, OwningActor, SkelMeshComponent](FSkeletalMeshPhysicsProxyParams& OutPhysicsParams)
		{
			OutPhysicsParams.bSimulating = bSimulating;

			GetPathName(this, OutPhysicsParams.Name);
			if (InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
			{
				OutPhysicsParams.InitialLinearVelocity = InitialLinearVelocity;
				OutPhysicsParams.InitialAngularVelocity = InitialAngularVelocity;
			}

			OutPhysicsParams.PhysicalMaterial = MakeSerializable(ChaosMaterial);
			OutPhysicsParams.ObjectType = ObjectType;

			OutPhysicsParams.Density = Density;
			OutPhysicsParams.MinMass = MinMass;
			OutPhysicsParams.MaxMass = MaxMass;

			OutPhysicsParams.CollisionType = CollisionType;
			OutPhysicsParams.ParticlesPerUnitArea = ImplicitShapeParticlesPerUnitArea;
			OutPhysicsParams.MinNumParticles = ImplicitShapeMinNumParticles;
			OutPhysicsParams.MaxNumParticles = ImplicitShapeMaxNumParticles;
			OutPhysicsParams.MinRes = MinLevelSetResolution;
			OutPhysicsParams.MaxRes = MaxLevelSetResolution;
			OutPhysicsParams.CollisionGroup = CollisionGroup;

			USkeletalMesh* SkeletalMesh = SkelMeshComponent->SkeletalMesh;
			if (SkeletalMesh)
			{
				UPhysicsAsset* PhysicsAsset = OverridePhysicsAsset ? OverridePhysicsAsset : SkelMeshComponent->SkeletalMesh->GetPhysicsAsset();
				FPhysicsAssetSimulationUtil::BuildParams(this, OwningActor, SkelMeshComponent, PhysicsAsset, OutPhysicsParams);
			}

			FPhysicsAssetSimulationUtil::UpdateAnimState(this, OwningActor, SkelMeshComponent, 0.0f, OutPhysicsParams);
		};

		TSharedPtr<FPhysScene_Chaos> Scene = GetPhysicsScene();
		if(Scene.IsValid())
		{
			check(PhysicsProxy == nullptr);
			PhysicsProxy = new FSkeletalMeshPhysicsProxy(this, InitFunc);

			Scene->AddObject(SkelMeshComponent, PhysicsProxy);

			AChaosSolverActor* const SolverActor = Cast<AChaosSolverActor>(Scene->GetSolverActor());
			UChaosGameplayEventDispatcher* const EventDispatcher = SolverActor ? SolverActor->GetGameplayEventDispatcher() : nullptr;
			if(EventDispatcher)
			{
				if(bNotifyCollisions)
				{
					// I want the more-detailed Chaos events
					EventDispatcher->RegisterForCollisionEvents(SkelMeshComponent, this);
				}

				if(FBodyInstance const* const BI = SkelMeshComponent->GetBodyInstance())
				{
					if(BI->bNotifyRigidBodyCollision)
					{
						EventDispatcher->RegisterForCollisionEvents(SkelMeshComponent, SkelMeshComponent);
					}
				}
			}
		}
	}
}

void USkeletalMeshSimulationComponent::OnDestroyPhysicsState()
{
	UActorComponent::OnDestroyPhysicsState();

	if (PhysicsProxy)
	{
		// Remove our tick dependency on the Skeletal Mesh component.
		AActor* OwningActor = GetOwner();
		USkeletalMeshComponent* SkelMeshComponent = OwningActor->FindComponentByClass<USkeletalMeshComponent>();
		RemoveTickPrerequisiteComponent(SkelMeshComponent);

		// Handle scene remove, right now we rely on the reset of EndPlay to clean up
		TSharedPtr<FPhysScene_Chaos> Scene = GetPhysicsScene();
		Scene->RemoveObject(PhysicsProxy);

		// Discard the pointer, the scene will handle destroying it
		PhysicsProxy = nullptr;
	}
}

bool USkeletalMeshSimulationComponent::ShouldCreatePhysicsState() const
{
	return true;
}

bool USkeletalMeshSimulationComponent::HasValidPhysicsState() const
{
	return PhysicsProxy != nullptr;
}

const TSharedPtr<FPhysScene_Chaos> USkeletalMeshSimulationComponent::GetPhysicsScene() const
{
	if (ChaosSolverActor)
	{
		return ChaosSolverActor->GetPhysicsScene();
	}
	else
	{
#if INCLUDE_CHAOS
		return GetOwner()->GetWorld()->PhysicsScene_Chaos;
#else
		return nullptr;
#endif
	}
}

void USkeletalMeshSimulationComponent::DispatchChaosPhysicsCollisionBlueprintEvents(const FChaosPhysicsCollisionInfo& CollisionInfo)
{
	ReceivePhysicsCollision(CollisionInfo);
	OnChaosPhysicsCollision.Broadcast(CollisionInfo);
}

void USkeletalMeshSimulationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime < 1.0e-5f)
	{
		return;
	}

	switch (TickType)
	{
	case ELevelTick::LEVELTICK_TimeOnly: // 0
		break;
	case ELevelTick::LEVELTICK_ViewportsOnly: // 1
		break;
	case ELevelTick::LEVELTICK_All: // 2
		if (HasValidPhysicsState())
		{
			AActor* OwningActor = GetOwner();
			USkeletalMeshComponent* SkelMeshComponent = OwningActor->FindComponentByClass<USkeletalMeshComponent>();
			PhysicsProxy->CaptureInputs(DeltaTime,
				[this, OwningActor, SkelMeshComponent](const float Dt, FSkeletalMeshPhysicsProxyParams & InOutPhysicsParams) -> bool
				{
					return FPhysicsAssetSimulationUtil::UpdateAnimState(this, OwningActor, SkelMeshComponent, Dt, InOutPhysicsParams);
				});
		}
		break;
	case ELevelTick::LEVELTICK_PauseTick: // 3
		break;
	};
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS