// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/FleshComponent.h"

#include "Animation/SkeletalMeshActor.h"
#include "Animation/Skeleton.h"
#include "ChaosStats.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Tetrahedron.h"
#include "ChaosFlesh/FleshCollection.h"
#include "ChaosFlesh/ChaosDeformableSolverActor.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "ChaosFlesh/ChaosDeformableTypes.h"
#include "ChaosFlesh/FleshDynamicAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dataflow/DataflowEngineUtil.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/Facades/CollectionTransformSourceFacade.h"
#include "GeometryCollection/Facades/CollectionTetrahedralSkeletalBindingsFacade.h"
#include "GeometryCollection/TransformCollection.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(FleshComponent)

FChaosEngineDeformableCVarParams CVarParams;
FAutoConsoleVariableRef CVarDeforambleDoDrawSimulationMesh(TEXT("p.Chaos.DebugDraw.Deformable.SimulationMesh"), CVarParams.bDoDrawSimulationMesh, TEXT("Debug draw the deformable simulation resutls on the game thread. [def: true]"));
FAutoConsoleVariableRef CVarDeforambleDoDrawSkeletalMeshBindingPositions(TEXT("p.Chaos.DebugDraw.Deformable.SkeletalMeshBindingPositions"), CVarParams.bDoDrawSkeletalMeshBindingPositions, TEXT("Debug draw the deformable simulation's SkeletalMeshBindingPositions on the game thread. [def: false]"));

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

	UpdateSimSpaceTransformIndex();
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
				// Mesh points are in component space, such that the exterior hull aligns with the
                // surface of the skeletal mesh, which is subject to the transform hierarchy.
				const FTransform& ComponentToWorldXf = GetComponentTransform();
				const FTransform ComponentToSimXf = GetSimSpaceRestTransform();
				return new FFleshThreadingProxy(
					this,
					ComponentToWorldXf, 
					ComponentToSimXf, 
					SimSpace,
					*Rest, 
					*Dynamic);
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

						// Extract animated transforms from all skeletal meshes.
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

												if (SimSpaceTransformGlobalIndex == INDEX_NONE &&
													SimSpaceTransformIndex == Cdx &&
													SkeletalMesh == SimSpaceSkeletalMesh)
												{
													SimSpaceTransformGlobalIndex = Adx;
												}
											}


										}
									}
								}
							}
						}

						FTransform BoneSpaceXf;
						if (AnimationTransforms.IsValidIndex(SimSpaceTransformGlobalIndex))
						{
							BoneSpaceXf = AnimationTransforms[SimSpaceTransformGlobalIndex];
						}
						else
						{
							BoneSpaceXf = FTransform::Identity;
						}

						return FDataMapValue(
							new Chaos::Softs::FFleshThreadingProxy::FFleshInputBuffer(
								this->GetComponentTransform(), 
								BoneSpaceXf, 
								SimSpaceTransformGlobalIndex,
								AnimationTransforms, 
								ComponentPose, 
								bTempEnableGravity, 
								StiffnessMultiplier, 
								DampingMultiplier, 
								MassMultiplier, 
								IncompressibilityMultiplier,
								InflationMultiplier,
								this));
					}
				}
			}
		}
	}
	return FDataMapValue(
		new Chaos::Softs::FFleshThreadingProxy::FFleshInputBuffer(
			this->GetComponentTransform(), 
			GetSimSpaceRestTransform(),
			SimSpaceTransformGlobalIndex,
			bTempEnableGravity,
			StiffnessMultiplier, 
			DampingMultiplier, 
			MassMultiplier, 
			IncompressibilityMultiplier,
			InflationMultiplier,
			this));
}

TArray<FString> UFleshComponent::GetSimSpaceBoneNameOptions() const
{
	TArray<FString> Names;
	if (RestCollection)
	{
		if (RestCollection->SkeletalMesh)
		{
			const FReferenceSkeleton& RefSkeleton = RestCollection->SkeletalMesh->GetSkeleton()->GetReferenceSkeleton();
			Names.SetNum(RefSkeleton.GetNum());
			for (int32 i = 0; i < RefSkeleton.GetNum(); i++)
			{
				Names[i] = RefSkeleton.GetBoneName(i).ToString();
			}
		}
	}
	return Names;
}

bool UFleshComponent::UpdateSimSpaceTransformIndex()
{
	SimSpaceTransformIndex = INDEX_NONE;
	SimSpaceSkeletalMesh = nullptr;

	if (SimSpace != ChaosDeformableSimSpace::Bone)
	{
		return false;
	}

	if (RestCollection && RestCollection->SkeletalMesh)
	{
		const FReferenceSkeleton& RefSkeleton = RestCollection->SkeletalMesh->GetSkeleton()->GetReferenceSkeleton();
		for (int32 i = 0; i < RefSkeleton.GetNum(); i++)
		{
			if (RefSkeleton.GetBoneName(i).ToString() == SimSpaceBoneName.ToString())
			{
				SimSpaceSkeletalMesh = RestCollection->SkeletalMesh;
				SimSpaceTransformIndex = i;
				return true;
			}
		}
	}
	return false;
}

FTransform UFleshComponent::GetSimSpaceRestTransform() const
{
	if (SimSpaceSkeletalMesh == nullptr)
	{
		return FTransform::Identity;
	}
	
	TArray<FTransform> ComponentTransforms;
	ComponentTransforms.SetNum(SimSpaceSkeletalMesh->GetRefSkeleton().GetNum());

	SimSpaceSkeletalMesh->FillComponentSpaceTransforms(
		SimSpaceSkeletalMesh->GetRefSkeleton().GetRefBonePose(),
		SimSpaceSkeletalMesh->GetResourceForRendering()->LODRenderData[0].RequiredBones,
		ComponentTransforms);

	if (!ComponentTransforms.IsValidIndex(SimSpaceTransformIndex))
	{
		return FTransform::Identity;
	}
	const FTransform& ComponentToBone = ComponentTransforms[SimSpaceTransformIndex];
	return ComponentToBone;
}

void UFleshComponent::UpdateFromSimualtion(const FDataMapValue* SimualtionBuffer)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosDeformable_UFleshComponent_UpdateFromSimualtion);
	TRACE_CPUPROFILER_EVENT_SCOPE(ChaosDeformable_UFleshComponent_UpdateFromSimualtion);

	if (const FFleshThreadingProxy::FFleshOutputBuffer* FleshBuffer = (*SimualtionBuffer)->As<FFleshThreadingProxy::FFleshOutputBuffer>())
	{
		if (GetDynamicCollection())
		{
			// @todo(flesh) : reduce conversions
			auto UEVertd = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
			auto UEVertf = [](FVector3d V) { return FVector3f(V.X, V.Y, V.Z); };

			TManagedArray<FVector3f>& DynamicVertex = GetDynamicCollection()->GetPositions();
			const TManagedArray<FVector3f>& SimulationVertex = FleshBuffer->Dynamic.GetAttribute<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);

			// Simulator produces results in component space.
			for (int i = DynamicVertex.Num() - 1; i >= 0; i--)
			{
				DynamicVertex[i] = SimulationVertex[i];
			}
			
			//p.Chaos.DebugDraw.Enabled 1
			//p.Chaos.DebugDraw.Deformable.SkeletalMeshBindingPositions 1
			if (CVarParams.bDoDrawSkeletalMeshBindingPositions)
			{
				DebugDrawSkeletalMeshBindingPositions();
			}
		}
	}
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
		if (IsVisible())
		{
#if WITH_EDITORONLY_DATA
			if (FleshAsset->bRenderInEditor)
#endif
			{
				if (Mesh && CVarParams.bDoDrawSimulationMesh)
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
		}
	}
	if (!bCanRender)
	{
		ResetProceduralMesh();
	}
}


void UFleshComponent::DebugDrawSkeletalMeshBindingPositions() const
{
#if WITH_EDITOR
	auto UEVertd = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };

	if (const UFleshAsset* RestAsset = GetRestCollection())
	{
		const USkeletalMesh* SkeletalMesh = TargetDeformationSkeleton ? TargetDeformationSkeleton : RestAsset->SkeletalMesh;
		if (SkeletalMesh)
		{
			TArray<bool> Influenced;
			TArray<FVector> PosArray = GetSkeletalMeshBindingPositionsInternal(SkeletalMesh, &Influenced);
			for (int i=0;i<PosArray.Num();i++)
			{
				const FVector& Pos = PosArray[i];
				if (Influenced[i])
				{
					Chaos::FDebugDrawQueue::GetInstance().DrawDebugPoint(GetComponentTransform().TransformPosition(Pos), FColor::Red, true, 2.0f, SDPG_Foreground, 10);
				}
			}
		}
	}
#endif
}

TArray<FVector> UFleshComponent::GetSkeletalMeshBindingPositions(const USkeletalMesh* InSkeletalMesh) const
{
	return GetSkeletalMeshBindingPositionsInternal(InSkeletalMesh);
}

TArray<FVector> UFleshComponent::GetSkeletalMeshBindingPositionsInternal(const USkeletalMesh* InSkeletalMesh, TArray<bool>* OutInfluence) const
{
	auto UEVert3d = [](FVector3f V) { return FVector3d(V.X, V.Y, V.Z); };
	auto UEVert4d = [](FVector4f V) { return FVector4d(V.X, V.Y, V.Z, V.W); };

	TArray<FVector> TransformPositions;
	if (InSkeletalMesh)
	{
		FName SkeletalMeshName(InSkeletalMesh->GetName());
		if (const UFleshAsset* RestAsset = GetRestCollection())
		{
			if (const FFleshCollection* Rest = RestAsset->GetCollection())
			{
				GeometryCollection::Facades::FTetrahedralSkeletalBindings TetBindings(*Rest);

				const TManagedArray<int32>* TetrahedronStart = Rest->FindAttribute<int32>(FTetrahedralCollection::TetrahedronStartAttribute, FGeometryCollection::GeometryGroup);
				const TManagedArray<FVector3f>* Verts = GetDynamicCollection() ? GetDynamicCollection()->FindPositions() : Rest->FindAttributeTyped<FVector3f>("Vertex", FGeometryCollection::VerticesGroup);

				if (ensure(Verts != nullptr) && TetrahedronStart)
				{
					TArray<FTransform> ComponentPose;
					Dataflow::Animation::GlobalTransforms(InSkeletalMesh->GetRefSkeleton(), ComponentPose);

					TransformPositions.SetNumUninitialized(ComponentPose.Num());
					for (int32 i = 0; i < ComponentPose.Num(); i++)
					{
						TransformPositions[i] = ComponentPose[i].GetTranslation();
					}

					if (OutInfluence != nullptr) OutInfluence->Init(false, TransformPositions.Num());
					for (int32 TetMeshIdx = 0; TetMeshIdx < TetrahedronStart->Num(); TetMeshIdx++)
					{
						FString MeshBindingsName = GeometryCollection::Facades::FTetrahedralSkeletalBindings::GenerateMeshGroupName(TetMeshIdx, SkeletalMeshName);
						TetBindings.CalculateBindings(MeshBindingsName, Verts->GetConstArray(), TransformPositions, OutInfluence);
					}
				}
			}
		}
	}
	return TransformPositions;
}








