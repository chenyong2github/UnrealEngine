// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/FleshComponent.h"

#include "Animation/SkeletalMeshActor.h"
#include "Animation/Skeleton.h"
#include "Chaos/DebugDrawQueue.h"
#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/ChaosDeformableSolverActor.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "ChaosStats.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dataflow/DataflowEngineUtil.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/Facades/CollectionTransformSourceFacade.h"
#include "GeometryCollection/TransformCollection.h"
#include "ProceduralMeshComponent.h"
#include "ChaosFlesh/ChaosDeformableTypes.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(FleshComponent)

FChaosEngineDeformableCVarParams CVarParams;
FAutoConsoleVariableRef CVarDeforambleDoDrawSimulationMesh(TEXT("p.Chaos.DebugDraw.Deformable.SimulationMesh"), CVarParams.bDoDrawSimulationMesh, TEXT("Debug draw the deformable simulation resutls on the game thread. [def: true]"));

DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.UFleshComponent.TickComponent"), STAT_ChaosDeformable_UFleshComponent_TickComponent, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.UFleshComponent.NewProxy"), STAT_ChaosDeformable_UFleshComponent_NewProxy, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.UFleshComponent.UpdateFromSimualtion"), STAT_ChaosDeformable_UFleshComponent_NewDeformableData, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.UFleshComponent.UpdateFromSimualtion"), STAT_ChaosDeformable_UFleshComponent_UpdateFromSimualtion, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.UFleshComponent.RenderProceduralMesh"), STAT_ChaosDeformable_UFleshComponent_RenderProceduralMesh, STATGROUP_Chaos);

DEFINE_LOG_CATEGORY_STATIC(LogFleshComponentInternal, Log, All);

UFleshComponent::UFleshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_LastDemotable;

	bTickInEditor = true;
	DynamicCollection = ObjectInitializer.CreateDefaultSubobject<UFleshDynamicAsset>(this, TEXT("Flesh Dynamic Asset"));
	Mesh = ObjectInitializer.CreateDefaultSubobject<UProceduralMeshComponent>(this, TEXT("Flesh Visualization Component"));
}

UFleshComponent::~UFleshComponent()
{
	if (RenderMesh) delete RenderMesh;
}

void UFleshComponent::Invalidate()
{
	bBoundsNeedsUpdate = true;
}


void UFleshComponent::OnRegister()
{
	if (bBoundsNeedsUpdate)
	{
		UpdateLocalBounds();
	}
	Super::OnRegister();
}


void UFleshComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ASkeletalMeshActor* SkeletalMeshActor = Cast<ASkeletalMeshActor>(GetOwner()))
	{
		if (USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent())
		{
			PrimaryComponentTick.AddPrerequisite(SkeletalMeshComponent, SkeletalMeshComponent->PrimaryComponentTick);
		}
	}
	if (PrimarySolverComponent)
	{
		PrimaryComponentTick.AddPrerequisite(PrimarySolverComponent, PrimarySolverComponent->PrimaryComponentTick);
	}
}

void UFleshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosDeformable_UFleshComponent_NewDeformableData);
	TRACE_CPUPROFILER_EVENT_SCOPE(ChaosDeformable_UFleshComponent_NewDeformableData);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CVarParams.bDoDrawSimulationMesh)
	{
		RenderProceduralMesh();
	}
	else 
	{
		ResetProceduralMesh();
	}
	UpdateLocalBounds();
}

void UFleshComponent::EndPlay(const EEndPlayReason::Type ReasonEnd)
{
	if (GetDynamicCollection())
	{
		GetDynamicCollection()->Reset();
	}
}

void UFleshComponent::SetRestCollection(const UFleshAsset* InRestCollection)
{
	RestCollection = InRestCollection;
	Invalidate();
	UpdateLocalBounds();
	ResetProceduralMesh();
}


UDeformablePhysicsComponent::FThreadingProxy* UFleshComponent::NewProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosDeformable_UFleshComponent_NewProxy);
	TRACE_CPUPROFILER_EVENT_SCOPE(ChaosDeformable_UFleshComponent_NewProxy);

	if (const UFleshAsset* RestAsset = GetRestCollection())
	{
		if (const FFleshCollection* Rest = RestAsset->GetCollection())
		{
			if (!GetDynamicCollection())
			{
				DynamicCollection = NewObject<UFleshDynamicAsset>(this, TEXT("Flesh Dynamic Asset"));
			}

			GetDynamicCollection()->Reset(Rest);
			if (const FManagedArrayCollection* Dynamic = GetDynamicCollection()->GetCollection())
			{
				return new FFleshThreadingProxy(this, GetComponentTransform(), *Rest, *Dynamic);
			}
		}
	}
	return nullptr;
}

UDeformablePhysicsComponent::FDataMapValue UFleshComponent::NewDeformableData()
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosDeformable_UFleshComponent_NewDeformableData);
	TRACE_CPUPROFILER_EVENT_SCOPE(ChaosDeformable_UFleshComponent_NewDeformableData);

	using namespace GeometryCollection::Facades;
	if (GetOwner())
	{
		if (const UFleshAsset* FleshAsset = GetRestCollection())
		{
			if (const FFleshCollection* Rest = FleshAsset->GetCollection())
			{
				FTransformSource TransformSource(*Rest);
				if (TransformSource.IsValid())
				{
					TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
					GetOwner()->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);

					if (const TManagedArray<FTransform>* RestTransforms = Rest->FindAttribute<FTransform>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup))
					{
						TArray<FTransform> AnimationTransforms = RestTransforms->GetConstArray();
						TArray<FTransform> ComponentPose = RestTransforms->GetConstArray();

						for (const USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
						{
							if (const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset())
							{
								if (const USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
								{
									TSet<int32> Roots = TransformSource.GetTransformSource(Skeleton->GetName(), Skeleton->GetGuid().ToString());
									if (!Roots.IsEmpty() && ensureMsgf(Roots.Num() == 1, TEXT("Error: Only supports a single root per skeleton.(%s)"), *Skeleton->GetName()))
									{
										TArray<FTransform> ComponentLocalPose;
										Dataflow::Animation::GlobalTransforms(SkeletalMesh->GetRefSkeleton(), ComponentLocalPose);

										const TArray<FTransform>& ComponentTransforms = SkeletalMeshComponent->GetComponentSpaceTransforms();
										if (ComponentLocalPose.Num() == ComponentTransforms.Num())
										{
											for (int Adx = Roots.Array()[0], Cdx = 0; Adx <= AnimationTransforms.Num() && Cdx < ComponentTransforms.Num(); Adx++, Cdx++)
											{
												// @todo(flesh) : Can we just use one array?
												AnimationTransforms[Adx] = ComponentTransforms[Cdx];
												ComponentPose[Adx] = ComponentLocalPose[Cdx]; 
											}
										}
									}
								}
							}
						}
						return FDataMapValue(new Chaos::Softs::FFleshThreadingProxy::FFleshInputBuffer(this->GetComponentTransform(), AnimationTransforms, ComponentPose, bTempEnableGravity, this));
					}
				}
			}
		}
	}
	return FDataMapValue(new Chaos::Softs::FFleshThreadingProxy::FFleshInputBuffer(this->GetComponentTransform(), bTempEnableGravity, this));
}


void UFleshComponent::UpdateFromSimualtion(const FDataMapValue* SimualtionBuffer)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosDeformable_UFleshComponent_UpdateFromSimualtion);
	TRACE_CPUPROFILER_EVENT_SCOPE(ChaosDeformable_UFleshComponent_UpdateFromSimualtion);

	FTransform CurrTransform = GetComponentTransform();
	if (const FFleshThreadingProxy::FFleshOutputBuffer* FleshBuffer = (*SimualtionBuffer)->As<FFleshThreadingProxy::FFleshOutputBuffer>())
	{
		if (GetDynamicCollection())
		{
			// @todo(flesh) : reduce conversions
			auto UEVertd = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
			auto UEVertf = [](FVector3d V) { return FVector3f(V.X, V.Y, V.Z); };

			TManagedArray<FVector3f>& DynamicVertex = GetDynamicCollection()->GetPositions();
			const TManagedArray<FVector3f>& SimulationVertex = FleshBuffer->Dynamic.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);

			for (int i = DynamicVertex.Num() - 1; i >= 0; i--)
			{
				DynamicVertex[i] = UEVertf(GetComponentTransform().InverseTransformPosition(UEVertd(SimulationVertex[i])));
				//DynamicVertex[i] = UEVertf(PrevTransform.InverseTransformPosition(UEVertd(SimulationVertex[i])));
				//Chaos::FDebugDrawQueue::GetInstance().DrawDebugPoint(UEVertd(SimulationVertex[i]), FColor::Red, false, -1.0f, 0, 5);
			}
		}
	}
	//PrevTransform = this->GetComponentTransform();
}

void UFleshComponent::UpdateLocalBounds()
{
	if (bBoundsNeedsUpdate && RestCollection)
	{
		{
			FFleshAssetEdit EditObject = RestCollection->EditCollection();
			if (FFleshCollection* Collection = EditObject.GetFleshCollection())
			{
				Collection->UpdateBoundingBox();
			}
		}
		BoundingBox = RestCollection->GetCollection()->GetBoundingBox();
		bBoundsNeedsUpdate = false;
	}

}

FBoxSphereBounds UFleshComponent::CalcBounds(const FTransform& LocalToWorldIn) const
{
	return BoundingBox.TransformBy(GetComponentTransform()); // todo(chaos:flesh) use LocalToWorldIn
}

void UFleshComponent::ResetDynamicCollection()
{
	if (const UFleshAsset* RestAsset = GetRestCollection())
	{
		if (!GetDynamicCollection())
		{
			DynamicCollection = NewObject<UFleshDynamicAsset>(this, TEXT("Flesh Dynamic Asset"));
		}

		if (!GetDynamicCollection()->GetCollection() || 
			!GetDynamicCollection()->GetCollection()->NumElements(FGeometryCollection::VerticesGroup))
		{
			GetDynamicCollection()->Reset(RestAsset->GetCollection());
			ResetProceduralMesh();
		}
		else
		{
			GetDynamicCollection()->ResetAttributesFrom(RestAsset->GetCollection());
		}
	}
}

void UFleshComponent::ResetProceduralMesh()
{
	if (Mesh )
	{
		Mesh->ClearAllMeshSections();
	}
	if (RenderMesh)
	{
		delete RenderMesh;
		RenderMesh = nullptr;
	}
}

void UFleshComponent::RenderProceduralMesh()
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosDeformable_UFleshComponent_RenderProceduralMesh);
	TRACE_CPUPROFILER_EVENT_SCOPE(ChaosDeformable_UFleshComponent_RenderProceduralMesh);

	bool bCanRender = false;
	if (const UFleshAsset* FleshAsset = GetRestCollection())
	{
#if WITH_EDITORONLY_DATA
		if (Mesh && FleshAsset->bRenderInEditor && CVarParams.bDoDrawSimulationMesh)
#endif
		{
			if (const FFleshCollection* Flesh = FleshAsset->GetCollection())
			{
				int32 NumVertices = Flesh->NumElements(FGeometryCollection::VerticesGroup);
				int32 NumFaces = Flesh->NumElements(FGeometryCollection::FacesGroup);
				if (NumFaces && NumVertices)
				{
					if (RenderMesh && RenderMesh->Vertices.Num() != NumFaces * 3)
					{
						ResetProceduralMesh();
					}

					if (!RenderMesh)
					{
						RenderMesh = new FFleshRenderMesh;

						for (int i = 0; i < NumFaces; ++i)
						{
							const auto& P1 = Flesh->Vertex[Flesh->Indices[i][0]];
							const auto& P2 = Flesh->Vertex[Flesh->Indices[i][1]];
							const auto& P3 = Flesh->Vertex[Flesh->Indices[i][2]];

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

							auto Normal = -Chaos::FVec3::CrossProduct(P3 - P1, P2 - P1);
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

						Mesh->SetRelativeTransform(GetComponentTransform());
						Mesh->CreateMeshSection_LinearColor(0, RenderMesh->Vertices, RenderMesh->Triangles, RenderMesh->Normals, RenderMesh->UVs, RenderMesh->Colors, RenderMesh->Tangents, false);
					}
					else
					{

						const TManagedArray<FVector3f>* RenderVertex = &Flesh->Vertex;
						if (GetDynamicCollection())
						{
							const TManagedArray<FVector3f>& DynamicVertex = GetDynamicCollection()->GetPositions();
							if (DynamicVertex.Num()) RenderVertex = &DynamicVertex;
						}
						auto InRange = [](int32 Size, int32 Val) { return 0 <= Val && Val < Size; };

						// Display only
						for (int i = 0; i < NumFaces; ++i)
						{
							const auto& P1 = (*RenderVertex)[Flesh->Indices[i][0]];
							const auto& P2 = (*RenderVertex)[Flesh->Indices[i][1]];
							const auto& P3 = (*RenderVertex)[Flesh->Indices[i][2]];

							RenderMesh->Vertices[3 * i] = FVector(P1);
							RenderMesh->Vertices[3 * i + 1] = FVector(P2);
							RenderMesh->Vertices[3 * i + 2] = FVector(P3);

							auto Normal = Chaos::FVec3::CrossProduct(P3 - P1, P2 - P1);
							RenderMesh->Normals[3 * i] = Normal;
							RenderMesh->Normals[3 * i + 1] = Normal;
							RenderMesh->Normals[3 * i + 2] = Normal;

							auto Tangent = (P2 - P1).GetSafeNormal();
							RenderMesh->Tangents[3 * i] = FProcMeshTangent(Tangent[0], Tangent[1], Tangent[2]);
							Tangent = (P3 - P2).GetSafeNormal();
							RenderMesh->Tangents[3 * i + 1] = FProcMeshTangent(Tangent[0], Tangent[1], Tangent[2]);
							Tangent = (P1 - P3).GetSafeNormal();
							RenderMesh->Tangents[3 * i + 2] = FProcMeshTangent(Tangent[0], Tangent[1], Tangent[2]);
						}

						if (!Mesh->GetComponentTransform().Equals(GetComponentTransform())) {
							Mesh->SetRelativeTransform(GetComponentTransform());
						}
						Mesh->UpdateMeshSection_LinearColor(0, RenderMesh->Vertices, RenderMesh->Normals, RenderMesh->UVs, RenderMesh->Colors, RenderMesh->Tangents);
					}

					bCanRender = true;
				}
			}
		}
	}
	if (!bCanRender)
	{
		ResetProceduralMesh();
	}
}








