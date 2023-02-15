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

					if (BodySetup->AggGeom.SphereElems.Num())
					{
						FKSphereElem& S = BodySetup->AggGeom.SphereElems[0];
						Geometry = new ::Chaos::TSphere<Chaos::FReal, 3>(S.Center, S.Radius * Scale.GetMax());
					}
					else if (BodySetup->AggGeom.BoxElems.Num())
					{
						FKBoxElem& B = BodySetup->AggGeom.BoxElems[0];
						Geometry = new TBox<Chaos::FReal, 3>(
							B.Center - 0.5 * Scale * FVector(B.X, B.Y, B.Z),
							B.Center + 0.5 * Scale * FVector(B.X, B.Y, B.Z));
					}
					else if (BodySetup->AggGeom.ConvexElems.Num())
					{
						FKConvexElem& C = BodySetup->AggGeom.ConvexElems[0];
						if (C.VertexData.Num())
						{
							TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe> ConvexMesh = C.GetChaosConvexMesh();	
							TArray<FConvex::FVec3Type> Vertices;
							Vertices.SetNum(C.VertexData.Num());
							for (int i = 0; i < Vertices.Num(); i++)
							{
								Vertices[i] = C.VertexData[i] * Scale;
							}
							Geometry = new FConvex(Vertices, 0.f);
						}
					}
					//else if (BodySetup->ChaosTriMeshes.Num())
					//{
					//	TArray<TSharedPtr<Chaos::FTriangleMeshImplicitObject, ESPMode::ThreadSafe>> ChaosTriMeshes;
					//	Geometry = BodySetup->ChaosTriMeshes[0]->CopySlow().Release();
					//}
					if (Geometry)
					{
						AddedBodiesData.Add(Chaos::Softs::FCollisionObjectAddedBodies(CollisionBody, P, "", Geometry));
					}
				}
			}
		}
	}


	for (auto& RemovedBody : RemovedBodies) 
	{
		if (RemovedBody)
		{
			RemovedBodiesData.Add({ RemovedBody });
		}
	}
	AddedBodies.Empty();
	RemovedBodies.Empty();

	for (auto& CollisionBody : CollisionBodies) 
	{
		if (CollisionBody)
		{
			FTransform P = CollisionBody->GetComponentToWorld();
			UpdateBodiesData.Add({ CollisionBody,P });
		}
	}


	return FDataMapValue(new Chaos::Softs::FCollisionManagerProxy::FCollisionsInputBuffer(
		AddedBodiesData, RemovedBodiesData, UpdateBodiesData, this));
}










     
