// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/StaticMeshSimulationComponent.h"

#include "Async/ParallelFor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/DebugDrawQueue.h"
#include "DrawDebugHelpers.h"
#include "Modules/ModuleManager.h"
#include "ChaosSolversModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "PhysicsSolver.h"
#include "Chaos/ChaosGameplayEventDispatcher.h"


DEFINE_LOG_CATEGORY_STATIC(UStaticMeshSimulationComponentLogging, NoLogging, All);

UStaticMeshSimulationComponent::UStaticMeshSimulationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Simulating(true)
	, bNotifyCollisions(false)
	, ObjectType(EObjectStateTypeEnum::Chaos_Object_Dynamic)
	, Mass(1.0)
	, CollisionType(ECollisionTypeEnum::Chaos_Surface_Volumetric)
	, ImplicitType(EImplicitTypeEnum::Chaos_Max)
	, MinLevelSetResolution(5)
	, MaxLevelSetResolution(10)
	, InitialVelocityType(EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
	, ChaosSolverActor(nullptr)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(true);
	ChaosMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<float>>();
}

// We tick to detect components that unreal has moved, so we can update the solver.
// The better solution long-term is to tie into UPrimitiveComponent::OnUpdateTransform() like we do for physx
void UStaticMeshSimulationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if INCLUDE_CHAOS
	// for kinematic objects, we assume that UE4 can and will move them, so we need to pass the new data to the phys solver
	if ((ObjectType == EObjectStateTypeEnum::Chaos_Object_Kinematic) && Simulating)
	{
		FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");
		checkSlow(ChaosModule);

		Chaos::IDispatcher* PhysicsDispatcher = ChaosModule->GetDispatcher();
		checkSlow(PhysicsDispatcher); // Should always have one of these

		for (int32 Idx = 0; Idx < PhysicsProxies.Num(); ++Idx)
		{
			FStaticMeshPhysicsProxy* const PhysicsProxy = PhysicsProxies[Idx];
			UPrimitiveComponent* const Comp = SimulatedComponents[Idx];

			FPhysicsProxyKinematicUpdate ParamUpdate;
			ParamUpdate.NewTransform = Comp->GetComponentTransform();
			ParamUpdate.NewVelocity = Comp->ComponentVelocity;

			PhysicsDispatcher->EnqueueCommandImmediate([PhysObj = PhysicsProxy, Params = ParamUpdate]()
			{
				PhysObj->BufferKinematicUpdate(Params);
			});
		}
	}
#endif
}

#if INCLUDE_CHAOS
Chaos::FPhysicsSolver* GetSolver(const UStaticMeshSimulationComponent& StaticMeshSimulationComponent)
{
	return	StaticMeshSimulationComponent.ChaosSolverActor != nullptr ? StaticMeshSimulationComponent.ChaosSolverActor->GetSolver() : StaticMeshSimulationComponent.GetOwner()->GetWorld()->PhysicsScene_Chaos->GetSolver();
}
#endif

void UStaticMeshSimulationComponent::OnCreatePhysicsState()
{
	// Skip the chain - don't care about body instance setup
	UActorComponent::OnCreatePhysicsState();

#if INCLUDE_CHAOS
	const bool bValidWorld = GetWorld() && GetWorld()->IsGameWorld();

	// Need to see if we actually have a target for the component
	AActor* OwningActor = GetOwner();
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	OwningActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
	TArray<UShapeComponent*> ShapeComponents;
	OwningActor->GetComponents<UShapeComponent>(ShapeComponents);
	TMap<UShapeComponent*, TArray<UStaticMeshComponent*>> ParentToChildMap;

	if (bValidWorld)
	{
		TSharedPtr<FPhysScene_Chaos> const Scene = GetPhysicsScene();
		AChaosSolverActor* const SolverActor = Scene ? Cast<AChaosSolverActor>(Scene->GetSolverActor()) : nullptr;
		UChaosGameplayEventDispatcher* const EventDispatcher = SolverActor ? SolverActor->GetGameplayEventDispatcher() : nullptr;

		for (UStaticMeshComponent* TargetComponent : StaticMeshComponents)
		{
			if (TargetComponent->GetAttachParent())
			{
				if (UShapeComponent* ShapeComponent = Cast<UShapeComponent>(TargetComponent->GetAttachParent()))
				{
					ParentToChildMap.FindOrAdd(ShapeComponent).Add(TargetComponent);
				}
			}
			else
			{
				if (PhysicalMaterial)
				{
					ChaosMaterial->Friction = PhysicalMaterial->Friction;
					ChaosMaterial->Restitution = PhysicalMaterial->Restitution;
					ChaosMaterial->SleepingLinearThreshold = PhysicalMaterial->SleepingLinearVelocityThreshold;
					ChaosMaterial->SleepingAngularThreshold = PhysicalMaterial->SleepingAngularVelocityThreshold;
				}
				auto InitFunc = [this, TargetComponent](FStaticMeshPhysicsProxy::Params& InParams)
				{
					GetPathName(this, InParams.Name);
					InParams.InitialTransform = GetOwner()->GetTransform();
					if (InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
					{
						InParams.InitialLinearVelocity = InitialLinearVelocity;
						InParams.InitialAngularVelocity = InitialAngularVelocity;
					}

					InParams.Mass = Mass;
					InParams.MinRes = MinLevelSetResolution;
					InParams.MaxRes = MaxLevelSetResolution;
					InParams.ObjectType = ObjectType;
					InParams.ShapeType = ImplicitType;
					InParams.PhysicalMaterial = MakeSerializable(ChaosMaterial);

					if (UStaticMesh* StaticMesh = TargetComponent->GetStaticMesh())
					{
						// TODO: Figure out where we want to get collision geometry from.  GetPhysicsTriMeshData() pulls
						// from the render mesh.
						FTriMeshCollisionData CollisionData;
						if (StaticMesh->GetPhysicsTriMeshData(&CollisionData, true)) // 0 = use LOD, 1 = all 
						{
							if ((InParams.InitialTransform.GetScale3D() - FVector(1.f, 1.f, 1.f)).SizeSquared() < SMALL_NUMBER)
							{
								auto& TVectorArray = reinterpret_cast<TArray<Chaos::TVector<float, 3>>&>(CollisionData.Vertices);
								InParams.MeshVertexPositions = MoveTemp(TVectorArray);
							}
							else
							{
								InParams.MeshVertexPositions.Resize(CollisionData.Vertices.Num());
								for (int32 i = 0; i < CollisionData.Vertices.Num(); ++i)
								{
									InParams.MeshVertexPositions.X(i) = CollisionData.Vertices[i] * InParams.InitialTransform.GetScale3D();
								}
							}
							check(sizeof(Chaos::TVector<int32, 3>) == sizeof(FTriIndices)); // binary compatible?
							InParams.TriIndices = MoveTemp(*reinterpret_cast<TArray<Chaos::TVector<int32, 3>>*>(&CollisionData.Indices));

							TargetComponent->SetMobility(EComponentMobility::Movable);
							InParams.bSimulating = Simulating;
						}
					}

					if (ImplicitType == EImplicitTypeEnum::Chaos_Max)
					{
						FVector Min, Max;
						TargetComponent->GetLocalBounds(Min, Max);

						InParams.ShapeType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;

						FVector Extents = (Max - Min);
						if (Extents.X < KINDA_SMALL_NUMBER || Extents.Y < KINDA_SMALL_NUMBER || Extents.Z < KINDA_SMALL_NUMBER)
						{
							InParams.ShapeType = EImplicitTypeEnum::Chaos_Implicit_None;
						}
					}

					if (InParams.ShapeType == EImplicitTypeEnum::Chaos_Implicit_Box)
					{
						FVector Min, Max;
						TargetComponent->GetLocalBounds(Min, Max);
						InParams.bSimulating = Simulating;
						InParams.ShapeParams.BoxExtents = (Max - Min) * InParams.InitialTransform.GetScale3D();
					}
					else if (InParams.ShapeType == EImplicitTypeEnum::Chaos_Implicit_Sphere)
					{
						FVector Min, Max;
						TargetComponent->GetLocalBounds(Min, Max);
						FVector Extents = (Max - Min) * InParams.InitialTransform.GetScale3D();
						float Radius = Extents.X;
						if (Extents.Y < Extents.X && Extents.Y < Extents.Z)
						{
							Radius = Extents.Y;
						}
						else if (Extents.Z < Extents.X)
						{
							Radius = Extents.Z;
						}
						InParams.bSimulating = Simulating;
						InParams.ShapeParams.SphereRadius = Radius / 2.f;
					}
					else if (InParams.ShapeType == EImplicitTypeEnum::Chaos_Implicit_Capsule)
					{
						FVector Min, Max;
						TargetComponent->GetLocalBounds(Min, Max);
						FVector Extents = (Max - Min) * InParams.InitialTransform.GetScale3D();
						float Radius = Extents.X;
						if (Extents.Y < Extents.X && Extents.Y < Extents.Z)
						{
							Radius = Extents.Y;
						}
						else if (Extents.Z < Extents.X)
						{
							Radius = Extents.Z;
						}
						float Height = Extents.X;
						if (Extents.Y > Extents.X && Extents.Y > Extents.Z)
						{
							Height = Extents.Y;
						}
						else if (Extents.Z > Extents.X)
						{
							Height = Extents.Z;
						}
						InParams.bSimulating = Simulating;
						InParams.ShapeParams.CapsuleHalfHeightAndRadius = FVector2D((Height - Radius) / 2.f, Radius / 2.f);
					}
				};

				auto SyncFunc = [TargetComponent](const FTransform& InTransform)
				{
					TargetComponent->SetWorldTransform(InTransform);
				};

				FStaticMeshPhysicsProxy* const NewPhysicsProxy = new FStaticMeshPhysicsProxy(this, InitFunc, SyncFunc);
				PhysicsProxies.Add(NewPhysicsProxy);
				SimulatedComponents.Add(TargetComponent);
				check(PhysicsProxies.Num() == SimulatedComponents.Num());

				Scene->AddObject(TargetComponent, NewPhysicsProxy);

				if (EventDispatcher)
				{
					if (bNotifyCollisions)
					{
						// I want the more-detailed Chaos events
						EventDispatcher->RegisterForCollisionEvents(TargetComponent, this);
					}

					if (FBodyInstance const* const BI = TargetComponent->GetBodyInstance())
					{
						if (BI->bNotifyRigidBodyCollision)
						{
							// target component wants the old-school events
							EventDispatcher->RegisterForCollisionEvents(TargetComponent, TargetComponent);
						}
					}
				}
			}
		}

		for (UShapeComponent* TargetComponent : ShapeComponents)
		{
			if (!TargetComponent->GetAttachParent())
			{
				if (PhysicalMaterial)
				{
					ChaosMaterial->Friction = PhysicalMaterial->Friction;
					ChaosMaterial->Restitution = PhysicalMaterial->Restitution;
					ChaosMaterial->SleepingLinearThreshold = PhysicalMaterial->SleepingLinearVelocityThreshold;
					ChaosMaterial->SleepingAngularThreshold = PhysicalMaterial->SleepingAngularVelocityThreshold;
				}
				const TArray<UStaticMeshComponent*>* StaticMeshComponentChildren = ParentToChildMap.Find(TargetComponent);
				auto InitFunc = [this, StaticMeshComponentChildren, TargetComponent](FStaticMeshPhysicsProxy::Params& InParams)
				{
					GetPathName(this, InParams.Name);
					InParams.InitialTransform = GetOwner()->GetTransform();
					if (InitialVelocityType == EInitialVelocityTypeEnum::Chaos_Initial_Velocity_User_Defined)
					{
						InParams.InitialLinearVelocity = InitialLinearVelocity;
						InParams.InitialAngularVelocity = InitialAngularVelocity;
					}

					InParams.Mass = Mass;
					InParams.ObjectType = ObjectType;
					InParams.bSimulating = Simulating;
					InParams.PhysicalMaterial = MakeSerializable(ChaosMaterial);

#if 0
					//TArray<USceneComponent*> SceneComponents = TargetComponent->GetAttachChildren();
					if (StaticMeshComponentChildren)
					{
						for (UStaticMeshComponent* StaticMeshComponent : *StaticMeshComponentChildren)
						{
							if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
							{
								// TODO: Figure out where we want to get collision geometry from.  GetPhysicsTriMeshData() pulls
								// from the render mesh.
								FTriMeshCollisionData CollisionData;
								if (StaticMesh->GetPhysicsTriMeshData(&CollisionData, true)) // 0 = use LOD, 1 = all 
								{
									if ((InParams.InitialTransform.GetScale3D() - FVector(1.f, 1.f, 1.f)).SizeSquared() < SMALL_NUMBER)
									{
										InParams.MeshVertexPositions = MoveTemp(CollisionData.Vertices);
									}
									else
									{
										InParams.MeshVertexPositions.SetNum(CollisionData.Vertices.Num());
										for (int32 i = 0; i < CollisionData.Vertices.Num(); ++i)
										{
											InParams.MeshVertexPositions[i] = CollisionData.Vertices[i] * InParams.InitialTransform.GetScale3D();
										}
									}
									check(sizeof(Chaos::TVector<int32, 3>) == sizeof(FTriIndices)); // binary compatible?
									InParams.TriIndices = MoveTemp(*reinterpret_cast<TArray<Chaos::TVector<int32, 3>>*>(&CollisionData.Indices));
									// If there are multiple static meshes we are just going to use the first one for now.
									break;
								}
							}
						}
					}
#endif

					if (UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(TargetComponent))
					{
						if (ImplicitType != EImplicitTypeEnum::Chaos_Implicit_Capsule && ImplicitType != EImplicitTypeEnum::Chaos_Max)
						{
							UE_LOG(LogStaticMesh, Warning, TEXT("ImplicitType does not match component type capsule, ignoring."), this);
						}
						InParams.ShapeType = EImplicitTypeEnum::Chaos_Implicit_Capsule;
						InParams.ShapeParams.CapsuleHalfHeightAndRadius = FVector2D(CapsuleComponent->GetScaledCapsuleHalfHeight(), CapsuleComponent->GetScaledCapsuleRadius());
					}
					else if (UBoxComponent* BoxComponent = Cast<UBoxComponent>(TargetComponent))
					{
						if (ImplicitType != EImplicitTypeEnum::Chaos_Implicit_Box && ImplicitType != EImplicitTypeEnum::Chaos_Max)
						{
							UE_LOG(LogStaticMesh, Warning, TEXT("ImplicitType does not match component type box, ignoring."), this);
						}
						InParams.ShapeType = EImplicitTypeEnum::Chaos_Implicit_Box;
						InParams.ShapeParams.BoxExtents = BoxComponent->GetScaledBoxExtent();
					}
					else if (USphereComponent* SphereComponent = Cast<USphereComponent>(TargetComponent))
					{
						if (ImplicitType != EImplicitTypeEnum::Chaos_Implicit_Sphere && ImplicitType != EImplicitTypeEnum::Chaos_Max)
						{
							UE_LOG(LogStaticMesh, Warning, TEXT("ImplicitType does not match component type sphere, ignoring."), this);
						}
						InParams.ShapeType = EImplicitTypeEnum::Chaos_Implicit_Sphere;
						InParams.ShapeParams.SphereRadius = SphereComponent->GetScaledSphereRadius();
					}
				};

				auto SyncFunc = [TargetComponent](const FTransform& InTransform)
				{
					TargetComponent->SetWorldTransform(InTransform);
				};

				FStaticMeshPhysicsProxy* const NewPhysicsProxy = new FStaticMeshPhysicsProxy(this, InitFunc, SyncFunc);
				PhysicsProxies.Add(NewPhysicsProxy);
				SimulatedComponents.Add(TargetComponent);
				check(PhysicsProxies.Num() == SimulatedComponents.Num());

				Scene->AddObject(TargetComponent, NewPhysicsProxy);

				if (EventDispatcher)
				{
					if (bNotifyCollisions)
					{
						// I want the more-detailed Chaos events
						EventDispatcher->RegisterForCollisionEvents(TargetComponent, this);
					}

					if (FBodyInstance const* const BI = TargetComponent->GetBodyInstance())
					{
						if (BI->bNotifyRigidBodyCollision)
						{
							EventDispatcher->RegisterForCollisionEvents(TargetComponent, TargetComponent);
						}
					}
				}
			}
		}
	}
#endif
}

void UStaticMeshSimulationComponent::OnDestroyPhysicsState()
{
	UActorComponent::OnDestroyPhysicsState();

#if INCLUDE_CHAOS
	TSharedPtr<FPhysScene_Chaos> Scene = GetPhysicsScene();
	AChaosSolverActor* const SolverActor = Scene ? Cast<AChaosSolverActor>(Scene->GetSolverActor()) : nullptr;
	UChaosGameplayEventDispatcher* const EventDispatcher = SolverActor ? SolverActor->GetGameplayEventDispatcher() : nullptr;

	if (Scene)
	{
		for (FStaticMeshPhysicsProxy* PhysicsProxy : PhysicsProxies)
		{
			if (PhysicsProxy)
			{
				// Handle scene remove, right now we rely on the reset of EndPlay to clean up
				Scene->RemoveObject(PhysicsProxy);

				if (EventDispatcher)
				{
					if (UPrimitiveComponent* const Comp = Scene->GetOwningComponent<UPrimitiveComponent>(PhysicsProxy))
					{
						EventDispatcher->UnRegisterForCollisionEvents(Comp, this);
						EventDispatcher->UnRegisterForCollisionEvents(Comp, Comp);
					}
				}
			}
		}
	}

	PhysicsProxies.Empty();
	SimulatedComponents.Empty();
#endif
}

bool UStaticMeshSimulationComponent::ShouldCreatePhysicsState() const
{
	return true;
}

bool UStaticMeshSimulationComponent::HasValidPhysicsState() const
{
	return PhysicsProxies.Num() > 0;
}

#if INCLUDE_CHAOS
const TSharedPtr<FPhysScene_Chaos> UStaticMeshSimulationComponent::GetPhysicsScene() const
{ 
	if (ChaosSolverActor)
	{
		return ChaosSolverActor->GetPhysicsScene();
	}
	else if (UWorld* W = GetOwner()->GetWorld())
	{
		return W->PhysicsScene_Chaos;
	}

	return nullptr;
}
#endif

void UStaticMeshSimulationComponent::DispatchChaosPhysicsCollisionBlueprintEvents(const FChaosPhysicsCollisionInfo& CollisionInfo)
{
	ReceivePhysicsCollision(CollisionInfo);
	OnChaosPhysicsCollision.Broadcast(CollisionInfo);
}

void UStaticMeshSimulationComponent::ForceRecreatePhysicsState()
{
	RecreatePhysicsState();
}



