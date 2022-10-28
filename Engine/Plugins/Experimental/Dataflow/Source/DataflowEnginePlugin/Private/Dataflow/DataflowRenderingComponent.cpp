// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowRenderingComponent.h"

#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "GeometryCollection/Facades/CollectionRenderingFacade.h"
#include "GeometryCollection/Facades/CollectionBoundsFacade.h"
#include "GeometryCollection/GeometryCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowRenderingComponent)

DEFINE_LOG_CATEGORY_STATIC(LogDataflowRenderComponentInternal, Log, All);

UDataflowRenderingComponent::UDataflowRenderingComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	Mesh = ObjectInitializer.CreateDefaultSubobject<UProceduralMeshComponent>(this, TEXT("Dataflow Visualization Component"));
}

UDataflowRenderingComponent::~UDataflowRenderingComponent()
{
	if (RenderMesh) delete RenderMesh;
}

void UDataflowRenderingComponent::Invalidate()
{
	bUpdateRender = true;
	bBoundsNeedsUpdate = true;
}


void UDataflowRenderingComponent::ResetRenderTargets() 
{ 
	RenderTargets.Reset();
	ResetRenderingCollection();
}

void UDataflowRenderingComponent::AddRenderTarget(const UDataflowEdNode* InTarget) 
{ 
	RenderTargets.AddUnique(InTarget); 
	ResetRenderingCollection();
}

void UDataflowRenderingComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RenderProceduralMesh();
}

void UDataflowRenderingComponent::UpdateLocalBounds()
{
	if (bBoundsNeedsUpdate)
	{
		GeometryCollection::Facades::FBoundsFacade::UpdateBoundingBox(RenderCollection);
		bBoundsNeedsUpdate = false;
	}
}

FBoxSphereBounds UDataflowRenderingComponent::CalcBounds(const FTransform& LocalToWorldIn) const
{
	return BoundingBox.TransformBy(GetComponentTransform());
}

void UDataflowRenderingComponent::ResetRenderingCollection()
{
	Invalidate();
	RenderCollection = FManagedArrayCollection();
	ResetProceduralMesh();
}

void UDataflowRenderingComponent::SetRenderingCollection(FManagedArrayCollection&& InCollection)
{
	Invalidate();
	RenderCollection = InCollection;
	UpdateLocalBounds();
	ResetProceduralMesh();
}


void UDataflowRenderingComponent::ResetProceduralMesh()
{
	if (Mesh && RenderMesh)
	{
		Mesh->ClearAllMeshSections();
		delete RenderMesh;
		RenderMesh = nullptr;
	}
}

void UDataflowRenderingComponent::RenderProceduralMesh()
{
	if (bUpdateRender)
	{
		RenderCollection = FManagedArrayCollection();
		GeometryCollection::Facades::FRenderingFacade Facade(&RenderCollection);

		bool bNeedsRefresh = false;
		if (Context && Dataflow)
		{
			for (const UDataflowEdNode* Target : RenderTargets)
			{
				bNeedsRefresh |= Target->Render(Facade, Context);
			}
		}
		bUpdateRender = false;
	}
		
	bool bCanRender = false;	
	if (GeometryCollection::Facades::FRenderingFacade::IsValid(&RenderCollection))
	{
		int32 NumVertices = RenderCollection.NumElements(FGeometryCollection::VerticesGroup);
		int32 NumFaces = RenderCollection.NumElements(FGeometryCollection::FacesGroup);
		if (NumFaces && NumVertices)
		{

			const TManagedArray<FIntVector>& Indices = RenderCollection.GetAttribute<FIntVector>("Indices", FGeometryCollection::FacesGroup);
			const TManagedArray<FVector3f>& Vertex = RenderCollection.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);

			if (RenderMesh && RenderMesh->Vertices.Num() != NumFaces * 3)
			{
				ResetProceduralMesh();
			}

			if (!RenderMesh)
			{
				RenderMesh = new FRenderMesh;

				for (int i = 0; i < NumFaces; ++i)
				{
					const auto& P1 = Vertex[Indices[i][0]];
					const auto& P2 = Vertex[Indices[i][1]];
					const auto& P3 = Vertex[Indices[i][2]];

					RenderMesh->Vertices.Add(FVector(P1));
					RenderMesh->Vertices.Add(FVector(P2));
					RenderMesh->Vertices.Add(FVector(P3));

					RenderMesh->Colors.Add(FLinearColor::White);
					RenderMesh->Colors.Add(FLinearColor::White);
					RenderMesh->Colors.Add(FLinearColor::White);

					RenderMesh->UVs.Add(FVector2D(0, 0));
					RenderMesh->UVs.Add(FVector2D(0, 0));
					RenderMesh->UVs.Add(FVector2D(0, 0));

					RenderMesh->Triangles.Add(3 * i);
					RenderMesh->Triangles.Add(3 * i + 1);
					RenderMesh->Triangles.Add(3 * i + 2);

					auto Normal = Chaos::FVec3::CrossProduct(P3 - P1, P2 - P1);
					RenderMesh->Normals.Add(Normal);
					RenderMesh->Normals.Add(Normal);
					RenderMesh->Normals.Add(Normal);

					auto Tangent = (P2 - P1).GetSafeNormal();
					RenderMesh->Tangents.Add(FProcMeshTangent(Tangent[0], Tangent[1], Tangent[2]));
					Tangent = (P3 - P2).GetSafeNormal();
					RenderMesh->Tangents.Add(FProcMeshTangent(Tangent[0], Tangent[1], Tangent[2]));
					Tangent = (P1 - P3).GetSafeNormal();
					RenderMesh->Tangents.Add(FProcMeshTangent(Tangent[0], Tangent[1], Tangent[2]));
				}
				Mesh->CreateMeshSection_LinearColor(0, RenderMesh->Vertices, RenderMesh->Triangles, RenderMesh->Normals, RenderMesh->UVs, RenderMesh->Colors, RenderMesh->Tangents, false);
			}
			else
			{
				Mesh->UpdateMeshSection_LinearColor(0, RenderMesh->Vertices, RenderMesh->Normals, RenderMesh->UVs, RenderMesh->Colors, RenderMesh->Tangents);
			}
			bCanRender = true;
		}
	}

	if (!bCanRender)
	{
		ResetProceduralMesh();
	}
}



