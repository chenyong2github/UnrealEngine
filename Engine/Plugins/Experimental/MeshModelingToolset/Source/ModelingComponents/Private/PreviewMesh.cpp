// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewMesh.h"

#include "Containers/StaticArray.h"


APreviewMeshActor::APreviewMeshActor()
{
#if WITH_EDITORONLY_DATA
	// hide this actor in the scene outliner
	bListedInSceneOutliner = false;
#endif
}


UPreviewMesh::UPreviewMesh()
{
	bBuildSpatialDataStructure = false;
	bDrawOnTop = false;
}

UPreviewMesh::~UPreviewMesh()
{
	checkf(DynamicMeshComponent == nullptr, TEXT("You must explicitly Disconnect() PreviewMesh before it is GCd"));
	checkf(TemporaryParentActor == nullptr, TEXT("You must explicitly Disconnect() PreviewMesh before it is GCd"));
}


void UPreviewMesh::CreateInWorld(UWorld* World, const FTransform& WithTransform)
{
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	TemporaryParentActor = World->SpawnActor<APreviewMeshActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(TemporaryParentActor, "Mesh");
	TemporaryParentActor->SetRootComponent(DynamicMeshComponent);
	//DynamicMeshComponent->SetupAttachment(TemporaryParentActor->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();

	TemporaryParentActor->SetActorTransform(WithTransform);
	//Builder.NewMeshComponent->SetWorldTransform(PlaneFrame.ToFTransform());
}


void UPreviewMesh::Disconnect()
{
	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}

	if (TemporaryParentActor != nullptr)
	{
		TemporaryParentActor->Destroy();
		TemporaryParentActor = nullptr;
	}
}


void UPreviewMesh::SetMaterial(UMaterialInterface* Material)
{
	check(DynamicMeshComponent);
	DynamicMeshComponent->SetMaterial(0, Material);
}

UMaterialInterface*
UPreviewMesh::GetMaterial() const
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->GetMaterial(0);
}


void UPreviewMesh::EnableWireframe(bool bEnable)
{
	check(DynamicMeshComponent);
	DynamicMeshComponent->bExplicitShowWireframe = bEnable;
}


FTransform UPreviewMesh::GetTransform() const
{
	if (TemporaryParentActor != nullptr)
	{
		return TemporaryParentActor->GetTransform();
	}
	return FTransform();
}

void UPreviewMesh::SetTransform(const FTransform& UseTransform)
{
	if (TemporaryParentActor != nullptr)
	{
		TemporaryParentActor->SetActorTransform(UseTransform);
	}
}


void UPreviewMesh::SetVisible(bool bVisible)
{
	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->SetVisibility(bVisible);
	}
}


bool UPreviewMesh::IsVisible() const
{
	if (DynamicMeshComponent != nullptr)
	{
		return DynamicMeshComponent->IsVisible();
	}
	return false;
}



void UPreviewMesh::ClearPreview() 
{
	FDynamicMesh3 Empty;
	UpdatePreview(&Empty);
	
	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(&Empty, true);
	}
}


void UPreviewMesh::UpdatePreview(const FDynamicMesh3* Mesh)
{
	DynamicMeshComponent->SetDrawOnTop(this->bDrawOnTop);

	DynamicMeshComponent->GetMesh()->Copy(*Mesh);
	DynamicMeshComponent->NotifyMeshUpdated();

	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}


const FDynamicMesh3* UPreviewMesh::GetPreviewDynamicMesh() const
{
	if (DynamicMeshComponent != nullptr)
	{
		return DynamicMeshComponent->GetMesh();
	}
	return nullptr;
}


TUniquePtr<FDynamicMesh3> UPreviewMesh::ExtractPreviewMesh() const
{
	if (DynamicMeshComponent != nullptr)
	{
		return DynamicMeshComponent->ExtractMesh(true);
	}
	return nullptr;
}



bool UPreviewMesh::TestRayIntersection(const FRay3d& WorldRay)
{
	if (IsVisible() && TemporaryParentActor != nullptr && bBuildSpatialDataStructure)
	{
		FFrame3d TransformFrame(TemporaryParentActor->GetActorTransform());
		FRay3d LocalRay = TransformFrame.ToFrame(WorldRay);
		int HitTriID = MeshAABBTree.FindNearestHitTriangle(LocalRay);
		if (HitTriID != FDynamicMesh3::InvalidID)
		{
			return true;
		}
	}
	return false;
}



bool UPreviewMesh::FindRayIntersection(const FRay3d& WorldRay, FHitResult& HitOut)
{
	if (IsVisible() && TemporaryParentActor != nullptr && bBuildSpatialDataStructure)
	{
		FTransform Transform = TemporaryParentActor->GetActorTransform();
		FRay3d LocalRay(Transform.InverseTransformPosition(WorldRay.Origin),
			Transform.InverseTransformVector(WorldRay.Direction));
		LocalRay.Direction.Normalize();
		int HitTriID = MeshAABBTree.FindNearestHitTriangle(LocalRay);
		if (HitTriID != FDynamicMesh3::InvalidID)
		{
			const FDynamicMesh3* UseMesh = GetPreviewDynamicMesh();
			FTriangle3d Triangle;
			UseMesh->GetTriVertices(HitTriID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(LocalRay, Triangle);
			Query.Find();

			//auto Query = MeshQueries::RayTriangleIntersection(UseMesh, HitTID, LocalRay);

			HitOut.FaceIndex = HitTriID;
			HitOut.Distance = Query.RayParameter;
			HitOut.Normal = Transform.TransformVectorNoScale(UseMesh->GetTriNormal(HitTriID));
			HitOut.ImpactNormal = HitOut.Normal;
			HitOut.ImpactPoint = Transform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
			return true;
		}
	}
	return false;
}



void UPreviewMesh::InitializeMesh(FMeshDescription* MeshDescription)
{
	check(DynamicMeshComponent != nullptr);
	DynamicMeshComponent->InitializeMesh(MeshDescription);

	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}



void UPreviewMesh::EditMesh(TFunctionRef<void(FDynamicMesh3&)> EditFunc)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	EditFunc(*Mesh);

	DynamicMeshComponent->NotifyMeshUpdated();

	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}


TUniquePtr<FMeshChange> UPreviewMesh::TrackedEditMesh(TFunctionRef<void(FDynamicMesh3&, FDynamicMeshChangeTracker&)> EditFunc)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	EditFunc(*Mesh, ChangeTracker);
	TUniquePtr<FMeshChange> Change = MakeUnique<FMeshChange>(ChangeTracker.EndChange());

	DynamicMeshComponent->NotifyMeshUpdated();
	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}

	return MoveTemp(Change);
}


void UPreviewMesh::ApplyChange(const FMeshVertexChange* Change, bool bRevert)
{
	check(DynamicMeshComponent != nullptr);
	DynamicMeshComponent->ApplyChange(Change, bRevert);
	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}
void UPreviewMesh::ApplyChange(const FMeshChange* Change, bool bRevert)
{
	check(DynamicMeshComponent != nullptr);
	DynamicMeshComponent->ApplyChange(Change, bRevert);
	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}

FSimpleMulticastDelegate& UPreviewMesh::GetOnMeshChanged()
{
	check(DynamicMeshComponent != nullptr);
	return DynamicMeshComponent->OnMeshChanged;
}


void UPreviewMesh::Bake(FMeshDescription* MeshDescription, bool bHaveModifiedToplogy)
{
	check(DynamicMeshComponent != nullptr);
	DynamicMeshComponent->Bake(MeshDescription, bHaveModifiedToplogy);
}

void UPreviewMesh::SetTriangleColorFunction(TFunction<FColor(int)> TriangleColorFunc, ERenderUpdateMode UpdateMode)
{
	DynamicMeshComponent->TriangleColorFunc = TriangleColorFunc;
	if (UpdateMode == ERenderUpdateMode::FastUpdate)
	{
		DynamicMeshComponent->FastNotifyColorsUpdated();
	}
	else if (UpdateMode == ERenderUpdateMode::FullUpdate)
	{
		DynamicMeshComponent->NotifyMeshUpdated();
	}
}

void UPreviewMesh::ClearTriangleColorFunction(ERenderUpdateMode UpdateMode)
{
	if (DynamicMeshComponent->TriangleColorFunc)
	{
		DynamicMeshComponent->TriangleColorFunc = nullptr;
		if (UpdateMode == ERenderUpdateMode::FastUpdate)
		{
			DynamicMeshComponent->FastNotifyColorsUpdated();
		}
		else if (UpdateMode == ERenderUpdateMode::FullUpdate)
		{
			DynamicMeshComponent->NotifyMeshUpdated();
		}
	}
}