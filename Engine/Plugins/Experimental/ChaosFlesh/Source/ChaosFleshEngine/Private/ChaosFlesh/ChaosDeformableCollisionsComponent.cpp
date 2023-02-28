// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosDeformableCollisionsComponent.h"

#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "Chaos/Deformable/ChaosDeformableCollisionsProxy.h"
#include "Chaos/Sphere.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
DEFINE_LOG_CATEGORY_STATIC(LogUDeformableCollisionsComponentInternal, Log, All);

UDeformableCollisionsComponent::UDeformableCollisionsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bTickInEditor = false;
}

void UDeformableCollisionsComponent::AddStaticMeshComponent(UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent)
	{
		if (!CollisionBodies.Contains(StaticMeshComponent))
		{
			CollisionBodies.Add(TObjectPtr<UStaticMeshComponent>(StaticMeshComponent) );
			AddedBodies.Add(StaticMeshComponent);
		}
	}
}

void UDeformableCollisionsComponent::RemoveStaticMeshComponent(UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent)
	{
		if (CollisionBodies.Contains(StaticMeshComponent))
		{
			CollisionBodies.Remove(TObjectPtr<UStaticMeshComponent>(StaticMeshComponent));
			RemovedBodies.Add(StaticMeshComponent);
		}
	}
}


UDeformablePhysicsComponent::FThreadingProxy* UDeformableCollisionsComponent::NewProxy()
{
	for (auto& Body : CollisionBodies)
	{
		if (Body)
		{
			if (!AddedBodies.Contains(Body))
			{
				AddedBodies.Add(Body);
			}
		}
	}
	return new FCollisionThreadingProxy(this);
}

template<> class Chaos::TSphere<float, 3>;

UDeformableCollisionsComponent::FDataMapValue 
UDeformableCollisionsComponent::NewDeformableData()
{
	TArray < Chaos::Softs::FCollisionObjectAddedBodies> AddedBodiesData;
	TArray < Chaos::Softs::FCollisionObjectRemovedBodies> RemovedBodiesData;
	TArray < Chaos::Softs::FCollisionObjectUpdatedBodies> UpdateBodiesData;

	for (auto& CollisionBody : AddedBodies)
	{
		if( CollisionBody )
		{
			if (UStaticMesh* StaticMesh = CollisionBody->GetStaticMesh())
			{
				if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
				{
					using namespace Chaos;
					FImplicitObject* Geometry = nullptr;
					FTransform P = CollisionBody->GetComponentToWorld();
					FVector Scale = P.GetScale3D();
					P.RemoveScaling();

					for( int8 Index = 0; Index < BodySetup->AggGeom.SphereElems.Num(); Index++)
					{
						FKSphereElem& S = BodySetup->AggGeom.SphereElems[Index];
						FTransform ShapeTransform = FTransform(Scale * S.Center) * P;
						Geometry = new ::Chaos::TSphere<Chaos::FReal, 3>(FVector(0), S.Radius * Scale.GetMax());
						AddedBodiesData.Add(Chaos::Softs::FCollisionObjectAddedBodies(
							{ CollisionBody, Chaos::Softs::ERigidCollisionShapeType::Sphere, Index },
							ShapeTransform, "", Geometry));
					}

					for (int8 Index = 0; Index < BodySetup->AggGeom.BoxElems.Num(); Index++)
					{
						FKBoxElem& B = BodySetup->AggGeom.BoxElems[Index];
						FTransform ShapeTransform = FTransform(Scale * B.Center) * P;
						Geometry = new TBox<Chaos::FReal, 3>(
							-0.5 * Scale * FVector(B.X, B.Y, B.Z),
							0.5 * Scale * FVector(B.X, B.Y, B.Z));
						AddedBodiesData.Add(Chaos::Softs::FCollisionObjectAddedBodies(
							{ CollisionBody, Chaos::Softs::ERigidCollisionShapeType::Box, Index },
							ShapeTransform, "", Geometry));
					}

					for (int8 Index = 0; Index < BodySetup->AggGeom.ConvexElems.Num(); Index++)
					{
						FKConvexElem& C = BodySetup->AggGeom.ConvexElems[Index];
						if (C.VertexData.Num())
						{
							FTransform ShapeTransform = C.GetTransform() * P;
							TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe> ConvexMesh = C.GetChaosConvexMesh();	
							TArray<FConvex::FVec3Type> Vertices;
							Vertices.SetNum(C.VertexData.Num());
							for (int i = 0; i < Vertices.Num(); i++)
							{
								Vertices[i] = C.VertexData[i] * Scale;
							}
							Geometry = new FConvex(Vertices, 0.f);
							AddedBodiesData.Add(Chaos::Softs::FCollisionObjectAddedBodies(
								{ CollisionBody, Chaos::Softs::ERigidCollisionShapeType::Convex, Index},
								ShapeTransform, "", Geometry));
						}
					}
				}
			}
		}
	}


	for (auto& RemovedBody : RemovedBodies) 
	{
		if (RemovedBody)
		{
			RemovedBodiesData.Add({  {RemovedBody, Chaos::Softs::ERigidCollisionShapeType::Unknown, INDEX_NONE} });
		}
	}
	AddedBodies.Empty();
	RemovedBodies.Empty();

	for (auto& CollisionBody : CollisionBodies) 
	{
		if (CollisionBody)
		{
			FTransform P = CollisionBody->GetComponentToWorld();
			UpdateBodiesData.Add({ {CollisionBody, Chaos::Softs::ERigidCollisionShapeType::Unknown, INDEX_NONE},P });
		}
	}


	return FDataMapValue(new Chaos::Softs::FCollisionManagerProxy::FCollisionsInputBuffer(
		AddedBodiesData, RemovedBodiesData, UpdateBodiesData, this));
}










     
