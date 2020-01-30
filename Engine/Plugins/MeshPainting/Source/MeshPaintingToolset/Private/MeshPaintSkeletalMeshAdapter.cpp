// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshPaintSkeletalMeshAdapter.h"
#include "Engine/SkeletalMesh.h"
#include "MeshPaintHelpers.h"
#include "MeshPaintingToolsetTypes.h"
#include "ComponentReregisterContext.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "IndexTypes.h"

//////////////////////////////////////////////////////////////////////////
// FMeshPaintGeometryAdapterForSkeletalMeshes

bool FMeshPaintSkeletalMeshComponentAdapter::Construct(UMeshComponent* InComponent, int32 InMeshLODIndex)
{
	SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InComponent);
	if (SkeletalMeshComponent != nullptr)
	{
		SkeletalMeshChangedHandle = SkeletalMeshComponent->RegisterOnSkeletalMeshPropertyChanged(USkeletalMeshComponent::FOnSkeletalMeshPropertyChanged::CreateRaw(this, &FMeshPaintSkeletalMeshComponentAdapter::OnSkeletalMeshChanged));

		if (SkeletalMeshComponent->SkeletalMesh != nullptr)
		{
			ReferencedSkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
			MeshLODIndex = InMeshLODIndex;
			const bool bSuccess = Initialize();
			return bSuccess;
		}
	}

	return false;
}

FMeshPaintSkeletalMeshComponentAdapter::~FMeshPaintSkeletalMeshComponentAdapter()
{
	if (SkeletalMeshComponent != nullptr)
	{
		SkeletalMeshComponent->UnregisterOnSkeletalMeshPropertyChanged(SkeletalMeshChangedHandle);
	}
}

void FMeshPaintSkeletalMeshComponentAdapter::OnSkeletalMeshChanged()
{
	OnRemoved();
	ReferencedSkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	if (SkeletalMeshComponent->SkeletalMesh != nullptr)
	{	
		Initialize();
		OnAdded();
	}	
}

void FMeshPaintSkeletalMeshComponentAdapter::OnPostMeshCached(USkeletalMesh* SkeletalMesh)
{
	if (ReferencedSkeletalMesh == SkeletalMesh)
	{
		OnSkeletalMeshChanged();
	}
}

bool FMeshPaintSkeletalMeshComponentAdapter::Initialize()
{
	check(ReferencedSkeletalMesh == SkeletalMeshComponent->SkeletalMesh);

	bool bInitializationResult = false;

	MeshResource = ReferencedSkeletalMesh->GetResourceForRendering();
	if (MeshResource != nullptr)
	{
		LODData = &MeshResource->LODRenderData[MeshLODIndex];
		checkf(ReferencedSkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(MeshLODIndex), TEXT("Invalid Imported Model index for vertex painting"));
		LODModel = &ReferencedSkeletalMesh->GetImportedModel()->LODModels[MeshLODIndex];

		bInitializationResult = FBaseMeshPaintComponentAdapter::Initialize();
	}

	
	return bInitializationResult;
}

bool FMeshPaintSkeletalMeshComponentAdapter::InitializeVertexData()
{
	// Retrieve mesh vertex and index data 
	const int32 NumVertices = LODData->GetNumVertices();
	MeshVertices.Reset();
	MeshVertices.AddDefaulted(NumVertices);
	for (int32 Index = 0; Index < NumVertices; Index++)
	{
		const FVector& Position = LODData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index);
		MeshVertices[Index] = Position;
	}

	MeshIndices.Reserve(LODData->MultiSizeIndexContainer.GetIndexBuffer()->Num());
	LODData->MultiSizeIndexContainer.GetIndexBuffer(MeshIndices);

	return (MeshVertices.Num() >= 0 && MeshIndices.Num() > 0);
}

void FMeshPaintSkeletalMeshComponentAdapter::InitializeAdapterGlobals()
{
	static bool bInitialized = false;
	if (!bInitialized)
	{
		bInitialized = true;
	}
}

void FMeshPaintSkeletalMeshComponentAdapter::AddReferencedObjectsGlobals(FReferenceCollector& Collector)
{
}

void FMeshPaintSkeletalMeshComponentAdapter::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ReferencedSkeletalMesh);
	Collector.AddReferencedObject(SkeletalMeshComponent);
}

void FMeshPaintSkeletalMeshComponentAdapter::CleanupGlobals()
{
}

void FMeshPaintSkeletalMeshComponentAdapter::OnAdded()
{
	checkf(SkeletalMeshComponent, TEXT("Invalid SkeletalMesh Component"));
	checkf(ReferencedSkeletalMesh, TEXT("Invalid reference to Skeletal Mesh"));
	checkf(ReferencedSkeletalMesh == SkeletalMeshComponent->SkeletalMesh, TEXT("Referenced Skeletal Mesh does not match one in Component"));

	SkeletalMeshComponent->bUseRefPoseOnInitAnim = true;
	SkeletalMeshComponent->InitAnim(true);

	// Register callback for when the skeletal mesh is cached underneath us
	ReferencedSkeletalMesh->OnPostMeshCached().AddRaw(this, &FMeshPaintSkeletalMeshComponentAdapter::OnPostMeshCached);
}

void FMeshPaintSkeletalMeshComponentAdapter::OnRemoved()
{
	checkf(SkeletalMeshComponent, TEXT("Invalid SkeletalMesh Component"));
	
	// If the referenced skeletal mesh has been destroyed (and nulled by GC), don't try to do anything more.
	// It should be in the process of removing all global geometry adapters if it gets here in this situation.
	if (!ReferencedSkeletalMesh)
	{
		return;
	}

	SkeletalMeshComponent->bUseRefPoseOnInitAnim = false;
	SkeletalMeshComponent->InitAnim(true);

	ReferencedSkeletalMesh->OnPostMeshCached().RemoveAll(this);
}

bool FMeshPaintSkeletalMeshComponentAdapter::LineTraceComponent(struct FHitResult& OutHit, const FVector Start, const FVector End, const struct FCollisionQueryParams& Params) const
{
	const bool bHitBounds = FMath::LineSphereIntersection(Start, End.GetSafeNormal(), (End - Start).SizeSquared(), SkeletalMeshComponent->Bounds.Origin, SkeletalMeshComponent->Bounds.SphereRadius);
	const float SqrRadius = FMath::Square(SkeletalMeshComponent->Bounds.SphereRadius);
	const bool bInsideBounds = (SkeletalMeshComponent->Bounds.ComputeSquaredDistanceFromBoxToPoint(Start) <= SqrRadius) || (SkeletalMeshComponent->Bounds.ComputeSquaredDistanceFromBoxToPoint(End) <= SqrRadius);

	bool bHitTriangle = false;
	if (bHitBounds || bInsideBounds)
	{
		const FTransform& ComponentTransform = SkeletalMeshComponent->GetComponentTransform();
		const FTransform InverseComponentTransform = ComponentTransform.Inverse();
		const FVector LocalStart = InverseComponentTransform.TransformPosition(Start);
		const FVector LocalEnd = InverseComponentTransform.TransformPosition(End);
		float MinDistance = FLT_MAX;
		FVector Intersect;
		FVector Normal;
		FIndex3i FoundTriangle;
		FVector HitPosition;
		if (!RayIntersectAdapter(FoundTriangle, HitPosition, LocalStart, LocalEnd))
		{
			return false;
		}

		// Compute the normal of the triangle
		const FVector& P0 = MeshVertices[FoundTriangle.A];
		const FVector& P1 = MeshVertices[FoundTriangle.B];
		const FVector& P2 = MeshVertices[FoundTriangle.C];

		const FVector TriNorm = (P1 - P0) ^ (P2 - P0);

		//check collinearity of A,B,C
		if (TriNorm.SizeSquared() > SMALL_NUMBER)
		{
			FVector IntersectPoint;
			FVector HitNormal;
		
			bool bHit = FMath::SegmentTriangleIntersection(LocalStart, LocalEnd, P0, P1, P2, IntersectPoint, HitNormal);

			if (bHit)
			{
				const float Distance = (LocalStart - IntersectPoint).SizeSquared();
				if (Distance < MinDistance)
				{
					MinDistance = Distance;
					Intersect = IntersectPoint;
					Normal = HitNormal;
				}
			}
		}
		

		if (MinDistance != FLT_MAX)
		{
			OutHit.Component = SkeletalMeshComponent;
			OutHit.Normal = Normal.GetSafeNormal();
			OutHit.ImpactNormal = OutHit.Normal;
			OutHit.ImpactPoint = ComponentTransform.TransformPosition(Intersect);
			OutHit.Location = OutHit.ImpactPoint;
			OutHit.bBlockingHit = true;
			OutHit.Distance = MinDistance;
			bHitTriangle = true;
		}
	}	

	return bHitTriangle;
}

void FMeshPaintSkeletalMeshComponentAdapter::QueryPaintableTextures(int32 MaterialIndex, int32& OutDefaultIndex, TArray<struct FPaintableTexture>& InOutTextureList)
{
	DefaultQueryPaintableTextures(MaterialIndex, SkeletalMeshComponent, OutDefaultIndex, InOutTextureList);
}

void FMeshPaintSkeletalMeshComponentAdapter::ApplyOrRemoveTextureOverride(UTexture* SourceTexture, UTexture* OverrideTexture) const
{
	DefaultApplyOrRemoveTextureOverride(SkeletalMeshComponent, SourceTexture, OverrideTexture);
}

void FMeshPaintSkeletalMeshComponentAdapter::GetTextureCoordinate(int32 VertexIndex, int32 ChannelIndex, FVector2D& OutTextureCoordinate) const
{
	OutTextureCoordinate = LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, ChannelIndex);
}

void FMeshPaintSkeletalMeshComponentAdapter::PreEdit()
{
	FlushRenderingCommands();

	SkeletalMeshComponent->Modify();

	ReferencedSkeletalMesh->SetFlags(RF_Transactional);
	ReferencedSkeletalMesh->Modify();

	ReferencedSkeletalMesh->bHasVertexColors = true;
	ReferencedSkeletalMesh->VertexColorGuid = FGuid::NewGuid();

	// Release the static mesh's resources.
	ReferencedSkeletalMesh->ReleaseResources();

	// Flush the resource release commands to the rendering thread to ensure that the build doesn't occur while a resource is still
	// allocated, and potentially accessing the UStaticMesh.
	ReferencedSkeletalMesh->ReleaseResourcesFence.Wait();

	if (LODData->StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() == 0)
	{
		// Mesh doesn't have a color vertex buffer yet!  We'll create one now.
		LODData->StaticVertexBuffers.ColorVertexBuffer.InitFromSingleColor(FColor(255, 255, 255, 255), LODData->GetNumVertices());
		ReferencedSkeletalMesh->bHasVertexColors = true;
		ReferencedSkeletalMesh->VertexColorGuid = FGuid::NewGuid();
		BeginInitResource(&LODData->StaticVertexBuffers.ColorVertexBuffer);
	}
	//Make sure we change the import data so the re-import do not replace the new data
	if (ReferencedSkeletalMesh->AssetImportData)
	{
		UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(ReferencedSkeletalMesh->AssetImportData);
		if (ImportData && ImportData->VertexColorImportOption != EVertexColorImportOption::Ignore)
		{
			ImportData->SetFlags(RF_Transactional);
			ImportData->Modify();
			ImportData->VertexColorImportOption = EVertexColorImportOption::Ignore;
		}
	}
}

void FMeshPaintSkeletalMeshComponentAdapter::PostEdit()
{
	TUniquePtr< FSkinnedMeshComponentRecreateRenderStateContext > RecreateRenderStateContext = MakeUnique<FSkinnedMeshComponentRecreateRenderStateContext>(ReferencedSkeletalMesh);
	ReferencedSkeletalMesh->InitResources();
}

void FMeshPaintSkeletalMeshComponentAdapter::GetVertexColor(int32 VertexIndex, FColor& OutColor, bool bInstance /*= true*/) const
{
	if (LODData->StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > 0)
	{
		check((int32)LODData->StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > VertexIndex);
		OutColor = LODData->StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIndex);
	}
}

void FMeshPaintSkeletalMeshComponentAdapter::SetVertexColor(int32 VertexIndex, FColor Color, bool bInstance /*= true*/)
{
	if (LODData->StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > 0)
	{
		LODData->StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIndex) = Color;

		int32 SectionIndex = INDEX_NONE;
		int32 SectionVertexIndex = INDEX_NONE;		
		LODModel->GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
		LODModel->Sections[SectionIndex].SoftVertices[SectionVertexIndex].Color = Color;

		if (!ReferencedSkeletalMesh->GetLODInfo(MeshLODIndex)->bHasPerLODVertexColors)
		{
			ReferencedSkeletalMesh->GetLODInfo(MeshLODIndex)->bHasPerLODVertexColors = true;
		}
	}
}

FMatrix FMeshPaintSkeletalMeshComponentAdapter::GetComponentToWorldMatrix() const
{
	return SkeletalMeshComponent->GetComponentToWorld().ToMatrixWithScale();
}

//////////////////////////////////////////////////////////////////////////
// FMeshPaintGeometryAdapterForSkeletalMeshesFactory

TSharedPtr<IMeshPaintComponentAdapter> FMeshPaintSkeletalMeshComponentAdapterFactory::Construct(class UMeshComponent* InComponent, int32 InMeshLODIndex) const
{
	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InComponent))
	{
		if (SkeletalMeshComponent->SkeletalMesh != nullptr)
		{
			TSharedRef<FMeshPaintSkeletalMeshComponentAdapter> Result = MakeShareable(new FMeshPaintSkeletalMeshComponentAdapter());
			if (Result->Construct(InComponent, InMeshLODIndex))
			{
				return Result;
			}
		}
	}

	return nullptr;
}
