// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewMesh.h"

#include "Containers/StaticArray.h"
#include "Engine/Classes/Materials/Material.h"


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

	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(TemporaryParentActor);
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
	SetMaterial(0, Material);
}

void UPreviewMesh::SetMaterial(int MaterialIndex, UMaterialInterface* Material)
{
	check(DynamicMeshComponent);
	DynamicMeshComponent->SetMaterial(MaterialIndex, Material);

	// force rebuild because we can't change materials yet - surprisingly complicated
	DynamicMeshComponent->NotifyMeshUpdated();
}

void UPreviewMesh::SetMaterials(const TArray<UMaterialInterface*>& Materials)
{
	check(DynamicMeshComponent);
	for (int k = 0; k < Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, Materials[k]);
	}

	// force rebuild because we can't change materials yet - surprisingly complicated
	DynamicMeshComponent->NotifyMeshUpdated();
}

int32 UPreviewMesh::GetNumMaterials() const
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->GetNumMaterials();
}

UMaterialInterface* UPreviewMesh::GetMaterial(int MaterialIndex) const
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->GetMaterial(MaterialIndex);
}

void UPreviewMesh::GetMaterials(TArray<UMaterialInterface*>& OutMaterials) const
{
	check(DynamicMeshComponent);
	for (int32 i = 0; i < DynamicMeshComponent->GetNumMaterials(); ++i)
	{
		OutMaterials.Add(DynamicMeshComponent->GetMaterial(i));
	}
}

void UPreviewMesh::SetOverrideRenderMaterial(UMaterialInterface* Material)
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->SetOverrideRenderMaterial(Material);
}

void UPreviewMesh::ClearOverrideRenderMaterial()
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->ClearOverrideRenderMaterial();
}


UMaterialInterface* UPreviewMesh::GetActiveMaterial(int MaterialIndex) const
{
	return DynamicMeshComponent->HasOverrideRenderMaterial(MaterialIndex) ?
		DynamicMeshComponent->GetOverrideRenderMaterial(MaterialIndex) : GetMaterial(MaterialIndex);
}



void UPreviewMesh::SetSecondaryRenderMaterial(UMaterialInterface* Material)
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->SetSecondaryRenderMaterial(Material);
}

void UPreviewMesh::ClearSecondaryRenderMaterial()
{
	check(DynamicMeshComponent);
	return DynamicMeshComponent->ClearSecondaryRenderMaterial();
}



void UPreviewMesh::EnableSecondaryTriangleBuffers(TUniqueFunction<bool(const FDynamicMesh3*, int32)> TriangleFilterFunc)
{
	check(DynamicMeshComponent);
	DynamicMeshComponent->EnableSecondaryTriangleBuffers(MoveTemp(TriangleFilterFunc));
}

void UPreviewMesh::DisableSecondaryTriangleBuffers()
{
	check(DynamicMeshComponent);
	DynamicMeshComponent->DisableSecondaryTriangleBuffers();
}


void UPreviewMesh::SetTangentsMode(EDynamicMeshTangentCalcType TangentsType)
{
	check(DynamicMeshComponent);
	DynamicMeshComponent->TangentsType = TangentsType;
}

const TMeshTangents<float>* UPreviewMesh::GetTangents() const
{
	return DynamicMeshComponent->GetTangents();
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
		DynamicMeshComponent->SetVisibility(bVisible, true);
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

void UPreviewMesh::UpdatePreview(FDynamicMesh3&& Mesh)
{
	DynamicMeshComponent->SetDrawOnTop(this->bDrawOnTop);

	*(DynamicMeshComponent->GetMesh()) = MoveTemp(Mesh);
	DynamicMeshComponent->NotifyMeshUpdated();

	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}


const FDynamicMesh3* UPreviewMesh::GetMesh() const
{
	if (DynamicMeshComponent != nullptr)
	{
		return DynamicMeshComponent->GetMesh();
	}
	return nullptr;
}

FDynamicMeshAABBTree3* UPreviewMesh::GetSpatial()
{
	if (DynamicMeshComponent != nullptr && bBuildSpatialDataStructure)
	{
		if (MeshAABBTree.IsValid())
		{
			return &MeshAABBTree;
		}
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
		FTransform3d Transform(TemporaryParentActor->GetActorTransform());
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

			HitOut.FaceIndex = HitTriID;
			HitOut.Distance = Query.RayParameter;
			HitOut.Normal = (FVector)Transform.TransformVectorNoScale(UseMesh->GetTriNormal(HitTriID));
			HitOut.ImpactNormal = HitOut.Normal;
			HitOut.ImpactPoint = (FVector)Transform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
			return true;
		}
	}
	return false;
}


FVector3d UPreviewMesh::FindNearestPoint(const FVector3d& WorldPoint, bool bLinearSearch)
{
	FTransform3d Transform(TemporaryParentActor->GetActorTransform());
	FVector3d LocalPoint = Transform.TransformPosition(WorldPoint);
	const FDynamicMesh3* UseMesh = GetMesh();
	if (bLinearSearch)
	{
		return TMeshQueries<FDynamicMesh3>::FindNearestPoint_LinearSearch(*UseMesh, WorldPoint);
	}
	if (bBuildSpatialDataStructure)
	{
		return MeshAABBTree.FindNearestPoint(WorldPoint);
	}
	return WorldPoint;
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


void UPreviewMesh::ReplaceMesh(FDynamicMesh3&& NewMesh)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	*Mesh = MoveTemp(NewMesh);

	DynamicMeshComponent->NotifyMeshUpdated();

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


void UPreviewMesh::DeferredEditMesh(TFunctionRef<void(FDynamicMesh3&)> EditFunc, bool bRebuildSpatial)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	EditFunc(*Mesh);

	if (bBuildSpatialDataStructure && bRebuildSpatial)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}


void UPreviewMesh::ForceRebuildSpatial()
{
	if (bBuildSpatialDataStructure)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}
}


void UPreviewMesh::NotifyDeferredEditCompleted(ERenderUpdateMode UpdateMode, EMeshRenderAttributeFlags ModifiedAttribs, bool bRebuildSpatial)
{
	if (bBuildSpatialDataStructure && bRebuildSpatial)
	{
		MeshAABBTree.SetMesh(DynamicMeshComponent->GetMesh(), true);
	}

	if (UpdateMode == ERenderUpdateMode::FullUpdate)
	{
		DynamicMeshComponent->NotifyMeshUpdated();
	}
	else if (UpdateMode == ERenderUpdateMode::FastUpdate)
	{
		bool bPositions = (ModifiedAttribs & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;
		bool bNormals = (ModifiedAttribs & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None;
		bool bColors = (ModifiedAttribs & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None;
		bool bUVs = (ModifiedAttribs & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None;
		if (bPositions)
		{
			DynamicMeshComponent->FastNotifyPositionsUpdated(bNormals, bColors, bUVs);
		}
		else
		{
			DynamicMeshComponent->FastNotifyVertexAttributesUpdated(bNormals, bColors, bUVs);
		}
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
void UPreviewMesh::ApplyChange(const FMeshReplacementChange* Change, bool bRevert)
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

void UPreviewMesh::SetTriangleColorFunction(TFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFunc, ERenderUpdateMode UpdateMode)
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