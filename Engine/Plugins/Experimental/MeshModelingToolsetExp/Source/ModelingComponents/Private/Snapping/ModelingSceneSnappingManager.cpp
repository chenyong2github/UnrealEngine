// Copyright Epic Games, Inc. All Rights Reserved.

#include "Snapping/ModelingSceneSnappingManager.h"

#include "InteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "ContextObjectStore.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"


#define LOCTEXT_NAMESPACE "USceneSnappingManager"


static float SnapToIncrement(float fValue, float fIncrement, float offset = 0)
{
	if (!FMath::IsFinite(fValue))
	{
		return 0;
	}
	fValue -= offset;
	float sign = FMath::Sign(fValue);
	fValue = FMath::Abs(fValue);
	int nInc = (int)(fValue / fIncrement);
	float fRem = (float)fmod(fValue, fIncrement);
	if (fRem > fIncrement / 2)
	{
		++nInc;
	}
	return sign * (float)nInc * fIncrement + offset;
}


//@ todo this are mirrored from GeometryProcessing, which is still experimental...replace w/ direct calls once GP component is standardized
static float OpeningAngleDeg(FVector A, FVector B, const FVector& P)
{
	A -= P;
	A.Normalize();
	B -= P;
	B.Normalize();
	float Dot = FMath::Clamp(FVector::DotProduct(A,B), -1.0f, 1.0f);
	return acos(Dot) * (180.0f / 3.141592653589f);
}

static FVector NearestSegmentPt(FVector A, FVector B, const FVector& P)
{
	FVector Direction = (B - A);
	float Length = Direction.Size();
	Direction /= Length;
	float t = FVector::DotProduct( (P - A), Direction);
	if (t >= Length)
	{
		return B;
	}
	if (t <= 0)
	{
		return A;
	}
	return A + t * Direction;
}




void UModelingSceneSnappingManager::Initialize(TObjectPtr<UInteractiveToolsContext> ToolsContext)
{
	ParentContext = ToolsContext;

	QueriesAPI = (ParentContext && ParentContext->ToolManager) ?
		ParentContext->ToolManager->GetContextQueriesAPI() : nullptr;
}

void UModelingSceneSnappingManager::Shutdown()
{

}


bool UModelingSceneSnappingManager::ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
{
	switch (Request.RequestType)
	{
	case ESceneSnapQueryType::Position:
		return ExecuteSceneSnapQueryPosition(Request, Results);
		break;
	case ESceneSnapQueryType::Rotation:
		return ExecuteSceneSnapQueryRotation(Request, Results);
		break;
	default:
		check(!"Only Position and Rotation Snap Queries are supported");
	}
	return false;
}


bool UModelingSceneSnappingManager::ExecuteSceneSnapQueryRotation(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
{
	if (!QueriesAPI)
	{
		return false;
	}

	if ((Request.TargetTypes & ESceneSnapQueryTargetType::Grid) != ESceneSnapQueryTargetType::None)
	{
		FToolContextSnappingConfiguration SnappingConfig = QueriesAPI->GetCurrentSnappingSettings();
		if (SnappingConfig.bEnableRotationGridSnapping)
		{
			FRotator Rotator(Request.DeltaRotation);
			FRotator RotGrid = Request.RotGridSize.Get(SnappingConfig.RotationGridAngles);
			Rotator = Rotator.GridSnap(RotGrid);

			FSceneSnapQueryResult SnapResult;
			SnapResult.TargetType = ESceneSnapQueryTargetType::Grid;
			SnapResult.DeltaRotation = Rotator.Quaternion();
			Results.Add(SnapResult);
			return true;
		}
	}
	return false;
}



static bool IsHiddenActor(AActor* Actor)
{
#if WITH_EDITOR
	return (Actor != nullptr) && ( Actor->IsHidden() || Actor->IsHiddenEd() );
#else
	return (Actor != nullptr) && ( Actor->IsHidden() );
#endif
}

static bool IsHiddenComponent(UPrimitiveComponent* Component)
{
#if WITH_EDITOR
	return (Component != nullptr) && ( (Component->IsVisible() == false) || (Component->IsVisibleInEditor() == false) );
#else
	return (Component != nullptr) && (Component->IsVisible() == false);
#endif
}


static bool IsVisibleObjectHit_Internal(const FHitResult& HitResult)
{
	AActor* Actor = HitResult.GetActor();
	if (IsHiddenActor(Actor))
	{
		return false;
	}
	UPrimitiveComponent* Component = HitResult.GetComponent();
	if ( IsHiddenComponent(Component) )
	{
		return false;
	}
	return true;
}

static bool FindNearestVisibleObjectHit_Internal(UWorld* World, FHitResult& HitResultOut, const FVector& Start, const FVector& End,
	bool bIsSceneGeometrySnapQuery, const TArray<const UPrimitiveComponent*>* ComponentsToIgnore, 
	const TArray<const UPrimitiveComponent*>* InvisibleComponentsToInclude)
{
	FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
	QueryParams.bTraceComplex = true;
	QueryParams.bReturnFaceIndex = bIsSceneGeometrySnapQuery;

	TArray<FHitResult> OutHits;
	if (World->LineTraceMultiByObjectType(OutHits, Start, End, ObjectQueryParams, QueryParams) == false)
	{
		return false;
	}

	float NearestVisible = TNumericLimits<float>::Max();
	for (const FHitResult& CurResult : OutHits)
	{
		if (CurResult.Distance < NearestVisible)
		{
			if (IsVisibleObjectHit_Internal(CurResult) 
				|| (InvisibleComponentsToInclude && InvisibleComponentsToInclude->Contains(CurResult.GetComponent())))
			{
				if (!ComponentsToIgnore || !ComponentsToIgnore->Contains(CurResult.GetComponent()))
				{
					HitResultOut = CurResult;
					NearestVisible = CurResult.Distance;
				}
			}
		}
	}

	return NearestVisible < TNumericLimits<float>::Max();
}





bool UModelingSceneSnappingManager::ExecuteSceneSnapQueryPosition(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
{
	if (!QueriesAPI)
	{
		return false;
	}

	int FoundResultCount = 0;

	if ((Request.TargetTypes & ESceneSnapQueryTargetType::Grid) != ESceneSnapQueryTargetType::None)
	{
		FToolContextSnappingConfiguration SnappingConfig = QueriesAPI->GetCurrentSnappingSettings();

		if (SnappingConfig.bEnablePositionGridSnapping)
		{
			FSceneSnapQueryResult SnapResult;
			SnapResult.TargetType = ESceneSnapQueryTargetType::Grid;

			FVector GridSize = Request.GridSize.Get(SnappingConfig.PositionGridDimensions);

			SnapResult.Position.X = SnapToIncrement(Request.Position.X, GridSize.X);
			SnapResult.Position.Y = SnapToIncrement(Request.Position.Y, GridSize.Y);
			SnapResult.Position.Z = SnapToIncrement(Request.Position.Z, GridSize.Z);

			Results.Add(SnapResult);
			FoundResultCount++;
		}
	}

	FViewCameraState ViewState;
	QueriesAPI->GetCurrentViewState(ViewState);

	//
	// Run a snap query by casting ray into the world.
	// If a hit is found, we look up what triangle was hit, and then test its vertices and edges
	//

	// cast ray into world
	FVector RayStart = ViewState.Position;
	FVector RayDirection = Request.Position - RayStart; RayDirection.Normalize();
	FVector RayEnd = RayStart + HALF_WORLD_MAX * RayDirection;
	FHitResult HitResult;
	bool bHitWorld = FindNearestVisibleObjectHit_Internal(
		QueriesAPI->GetCurrentEditingWorld(), 
		HitResult, 
		RayStart, RayEnd, 
		true, 
		Request.ComponentsToIgnore, 
		Request.InvisibleComponentsToInclude);

	if (bHitWorld && HitResult.FaceIndex >= 0)
	{
		float VisualAngle = OpeningAngleDeg(Request.Position, HitResult.ImpactPoint, RayStart);
		//UE_LOG(LogTemp, Warning, TEXT("[HIT] visualangle %f faceindex %d"), VisualAngle, HitResult.FaceIndex);
		if (VisualAngle < Request.VisualAngleThresholdDegrees)
		{
			UPrimitiveComponent* Component = HitResult.Component.Get();
			if (Cast<UStaticMeshComponent>(Component) != nullptr)
			{
				// HitResult.FaceIndex is apparently an index into the TriMeshCollisionData, not sure how
				// to directly access it. Calling GetPhysicsTriMeshData is expensive!
				//UBodySetup* BodySetup = Cast<UStaticMeshComponent>(Component)->GetBodySetup();
				//UObject* CDPObj = BodySetup->GetOuter();
				//IInterface_CollisionDataProvider* CDP = Cast<IInterface_CollisionDataProvider>(CDPObj);
				//FTriMeshCollisionData TriMesh;
				//CDP->GetPhysicsTriMeshData(&TriMesh, true);
				//FTriIndices Triangle = TriMesh.Indices[HitResult.FaceIndex];
				//FVector Positions[3] = { TriMesh.Vertices[Triangle.v0], TriMesh.Vertices[Triangle.v1], TriMesh.Vertices[Triangle.v2] };

				// physics collision data is created from StaticMesh RenderData
				// so use HitResult.FaceIndex to extract triangle from the LOD0 mesh
				// (note: this may be incorrect if there are multiple sections...in that case I think we have to
				//  first find section whose accumulated index range would contain .FaceIndexX)
				UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();
				FStaticMeshLODResources& LOD = StaticMesh->GetRenderData()->LODResources[0];
				FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
				int32 TriIdx = 3 * HitResult.FaceIndex;
				FVector Positions[3];
				Positions[0] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx]);
				Positions[1] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx+1]);
				Positions[2] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx+2]);

				// transform to world space
				FTransform ComponentTransform = Component->GetComponentTransform();
				Positions[0] = ComponentTransform.TransformPosition(Positions[0]);
				Positions[1] = ComponentTransform.TransformPosition(Positions[1]);
				Positions[2] = ComponentTransform.TransformPosition(Positions[2]);

				FSceneSnapQueryResult SnapResult;
				SnapResult.TriVertices[0] = Positions[0];
				SnapResult.TriVertices[1] = Positions[1];
				SnapResult.TriVertices[2] = Positions[2];

				// try snapping to vertices
				float SmallestAngle = Request.VisualAngleThresholdDegrees;
				if ( (Request.TargetTypes & ESceneSnapQueryTargetType::MeshVertex) != ESceneSnapQueryTargetType::None)
				{
					for (int j = 0; j < 3; ++j)
					{
						VisualAngle = OpeningAngleDeg(Request.Position, Positions[j], RayStart);
						if (VisualAngle < SmallestAngle)
						{
							SmallestAngle = VisualAngle;
							SnapResult.Position = Positions[j];
							SnapResult.TargetType = ESceneSnapQueryTargetType::MeshVertex;
							SnapResult.TriSnapIndex = j;
						}
					}
				}

				// try snapping to nearest points on edges
				if ( ((Request.TargetTypes & ESceneSnapQueryTargetType::MeshEdge) != ESceneSnapQueryTargetType::None) &&
					(SnapResult.TargetType != ESceneSnapQueryTargetType::MeshVertex) )
				{
					for (int j = 0; j < 3; ++j)
					{
						FVector EdgeNearestPt = NearestSegmentPt(Positions[j], Positions[(j+1)%3], Request.Position);
						VisualAngle = OpeningAngleDeg(Request.Position, EdgeNearestPt, RayStart);
						if (VisualAngle < SmallestAngle )
						{
							SmallestAngle = VisualAngle;
							SnapResult.Position = EdgeNearestPt;
							SnapResult.TargetType = ESceneSnapQueryTargetType::MeshEdge;
							SnapResult.TriSnapIndex = j;
						}
					}
				}

				// if we found a valid snap, return it
				if (SmallestAngle < Request.VisualAngleThresholdDegrees)
				{
					SnapResult.TargetActor = HitResult.HitObjectHandle.FetchActor();
					SnapResult.TargetComponent = HitResult.Component.Get();
					Results.Add(SnapResult);
					FoundResultCount++;
				}
			}
		}

	}

	return (FoundResultCount > 0);
}



bool UE::Geometry::RegisterSceneSnappingManager(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UModelingSceneSnappingManager* Found = ToolsContext->ContextObjectStore->FindContext<UModelingSceneSnappingManager>();
		if (Found == nullptr)
		{
			UModelingSceneSnappingManager* SelectionManager = NewObject<UModelingSceneSnappingManager>(ToolsContext->ToolManager);
			if (ensure(SelectionManager))
			{
				SelectionManager->Initialize(ToolsContext);
				ToolsContext->ContextObjectStore->AddContextObject(SelectionManager);
				return true;
			}
			else
			{
				return false;
			}
		}
		return true;
	}
	return false;
}



bool UE::Geometry::DeregisterSceneSnappingManager(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UModelingSceneSnappingManager* Found = ToolsContext->ContextObjectStore->FindContext<UModelingSceneSnappingManager>();
		if (Found != nullptr)
		{
			Found->Shutdown();
			ToolsContext->ContextObjectStore->RemoveContextObject(Found);
		}
		return true;
	}
	return false;
}


UModelingSceneSnappingManager* UE::Geometry::FindModelingSceneSnappingManager(UInteractiveToolManager* ToolManager)
{
	if (ensure(ToolManager))
	{
		UModelingSceneSnappingManager* Found = ToolManager->GetContextObjectStore()->FindContext<UModelingSceneSnappingManager>();
		if (Found != nullptr)
		{
			return Found;
		}
	}
	return nullptr;
}


#undef LOCTEXT_NAMESPACE