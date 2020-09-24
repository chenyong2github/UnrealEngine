// Copyright Epic Games, Inc. All Rights Reserved.
#include "LuminAROrigin.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "AugmentedReality/Public/ARSessionConfig.h"
#include "HeadMountedDisplayFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogMagicLeap, Log, All);

ALuminAROrigin::ALuminAROrigin(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MRMeshComponent(CreateDefaultSubobject<UMRMeshComponent>(TEXT("MRMesh")))
	, PlaneSurfaceMaterial(nullptr)
	, WireframeMaterial(nullptr)
	, VertexColorMapping( {
		{ EMagicLeapPlaneQueryFlags::Vertical, FColor(0, 255, 255, 255) },
		{ EMagicLeapPlaneQueryFlags::Horizontal, FColor(0, 255, 0, 255) },
		{ EMagicLeapPlaneQueryFlags::Arbitrary, FColor(255, 255, 255, 255) },
		{ EMagicLeapPlaneQueryFlags::Ceiling, FColor(255, 0, 255, 255) },
		{ EMagicLeapPlaneQueryFlags::Floor, FColor(255, 102, 0, 255) },
		{ EMagicLeapPlaneQueryFlags::Wall, FColor(255, 0, 0, 255) }
	})
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> 
		PlaneSurfaceMaterialObj(
			TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface>
		WireframeMaterialObj(
			TEXT("/Engine/EngineDebugMaterials/WireframeMaterial"));

	if (PlaneSurfaceMaterialObj.Succeeded())
	{
		PlaneSurfaceMaterial = PlaneSurfaceMaterialObj.Object;
	}
	if(WireframeMaterialObj.Succeeded())
	{
		WireframeMaterial = WireframeMaterialObj.Object;
	}

	RootComponent = MRMeshComponent;

	PrimaryActorTick.bCanEverTick = true;
}

ALuminAROrigin* ALuminAROrigin::GetOriginActor(
									   const UARSessionConfig& ARSessionConfig,
									   UWorld* World)
{
	UWorld*const GameWorld = World ? World : FindWorld();
	if (!GameWorld)
	{
		return nullptr;
	}
	ALuminAROrigin* FoundActor = nullptr;
	for (TActorIterator<ALuminAROrigin> Iter(GameWorld); Iter; ++Iter)
	{
		if (!(*Iter)->IsPendingKill())
		{
			FoundActor = *Iter;
			break;
		}
	}
	// If we didn't find an instance in the world, create one here //
	if (FoundActor == nullptr)
	{
		FoundActor = GameWorld->SpawnActor<ALuminAROrigin>(
								   ALuminAROrigin::StaticClass(), 
								   FVector::ZeroVector, FRotator::ZeroRotator);
	}
	// There doesn't seem to be a reason to have to set the MRMeshComponent's
	//	options every single time we add a new plane, so we can just do that
	//	one time here and just forget about it //
	// The new MRMeshComponent must have the material set manually here because
	//	for some reason because the engine's default material for MD_Surface is
	//	not working correctly.  Using UMRMeshComponent::SetUseWireframe here
	//	also does not work correctly.
	// Note: the first parameter to SetMaterial is unused inside
	//	UMRMeshComponent, so the value is irrelevant.

	// MRMesh setting doesnt work, so set the material itself
	// FoundActor->MRMeshComponent->SetUseWireframe(ARSessionConfig.bRenderMeshDataInWireframe);
	if(ARSessionConfig.bRenderMeshDataInWireframe && FoundActor->WireframeMaterial != nullptr)
	{
		FoundActor->MRMeshComponent->SetMaterial(0,
												 FoundActor->WireframeMaterial);
	}
	else if (FoundActor->PlaneSurfaceMaterial != nullptr)
	{
		FoundActor->MRMeshComponent->SetMaterial(0,
											  FoundActor->PlaneSurfaceMaterial);
	}
	// Additional mesh options configured via the ARSessionConfig //
	FoundActor->MRMeshComponent->SetEnableMeshOcclusion     ( ARSessionConfig.bUseMeshDataForOcclusion);
	FoundActor->MRMeshComponent->SetNeverCreateCollisionMesh(!ARSessionConfig.bGenerateCollisionForMeshData);
	FoundActor->MRMeshComponent->SetEnableNavMesh           ( ARSessionConfig.bGenerateNavMeshForMeshData);
	// Override some ARSession options due to compatibility issues //
	///TODO: remove this block once these options regain functionality!
	{
		// Enabling occlusion on the MRMesh prevents flickering due to an
		//	unknown UE4 issue.
		FoundActor->MRMeshComponent->SetEnableMeshOcclusion(true);
	}
	return FoundActor;
}

// Temporary storage container to hold the vertex data of each plane mesh for 
//	the duration of its corresponding MRMeshComponent's lifetime //
struct FPlaneMeshHolder : public IMRMesh::FBrickDataReceipt
{
public:
	FPlaneMeshHolder(const TArray<FVector>& VerticesLocalSpace, 
					 const FTransform& LocalToTracking,
					 const FColor& VertexColor)
	{
		UVData = TArray<FVector2D>(
			{ FVector2D(0.f, 0.f)
			, FVector2D(1.f, 0.f)
			, FVector2D(1.f, 1.f)
			, FVector2D(0.f, 1.f) });
		ColorData           .SetNumUninitialized(VerticesLocalSpace.Num());
		TangentXZData       .SetNumUninitialized(VerticesLocalSpace.Num() * 2);
		VertexTrackingPositions.SetNumUninitialized(VerticesLocalSpace.Num());

		// LocalToTracking transform for planes uses the PlaneResult's ContentOrientation which makes Z axis the normal.
		const FVector PlaneNormal = LocalToTracking.GetScaledAxis(EAxis::Z).GetSafeNormal();
		const FVector Perp = PlaneNormal.X < PlaneNormal.Z ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 1.0f, 0.0f);
		const FVector Tang = FVector::CrossProduct(PlaneNormal, Perp).GetSafeNormal();

		for (int32 c = 0; c < VerticesLocalSpace.Num(); c++)
		{
			ColorData[c] = VertexColor;
			TangentXZData[2 * c    ] = Tang;
			TangentXZData[2 * c + 1] = PlaneNormal;
			VertexTrackingPositions[c] =
							   LocalToTracking.TransformPosition(VerticesLocalSpace[c]);
		}
		WorldBoundingBox = FBox(VertexTrackingPositions);
		// Indices for two triangles representing the plane assuming a CCW 
		//	winding order:
		Indices = TArray<MRMESH_INDEX_TYPE>({ 0, 2, 1, 0, 3, 2 });

	}
public:
	TArray<FVector> VertexTrackingPositions;
	TArray<FVector> PositionData;
	TArray<FVector2D> UVData;
	TArray<FPackedNormal> TangentXZData;
	TArray<FColor> ColorData;
	TArray<MRMESH_INDEX_TYPE> Indices;
	FBox WorldBoundingBox;
};

void ALuminAROrigin::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SetActorTransform(UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr));
	// Since MRMeshComponent is the root component, setting the actor transform in the previous line sets the MRMesh transform itself.
	// MRMeshComponent->SendRelativeTransform(FTransform::Identity);
}

void ALuminAROrigin::CreatePlane(const FGuid& PlaneId,
								 const TArray<FVector>& VerticesLocalSpace,
								 const FTransform& LocalToTracking,
								 EMagicLeapPlaneQueryFlags FlagForVertexColor)
{
	check(!PlaneIdToBrickId.Contains(PlaneId));
	const IMRMesh::FBrickId BrickId = GetBrickId(PlaneId);
	// Now, actually populate the new mesh component w/ the
	//	appropriate geometry... //
	TSharedPtr<FPlaneMeshHolder, ESPMode::ThreadSafe> MeshHolder(
		new FPlaneMeshHolder(VerticesLocalSpace, LocalToTracking, VertexColorMapping[FlagForVertexColor]));

	static_cast<IMRMesh*>(MRMeshComponent)->SendBrickData(
		IMRMesh::FSendBrickDataArgs{
			MeshHolder,
			BrickId,
			MeshHolder->VertexTrackingPositions,
			MeshHolder->UVData,
			MeshHolder->TangentXZData,
			MeshHolder->ColorData,
			MeshHolder->Indices,
			MeshHolder->WorldBoundingBox
		});
}

void ALuminAROrigin::DestroyPlane(const FGuid& PlaneId)
{
	const IMRMesh::FBrickId * const MappedBrickId =
												PlaneIdToBrickId.Find(PlaneId);
	if(MappedBrickId)
	{
		// All we have to do to destroy an MRMesh block is to call SendBrickData
		//	using an empty index array.  However, to inline the argument struct
		//	with the brick data call, we have to provide empty versions of the
		//	struct members prior to Indices.
		static const TArray<FVector>           EmptyPositionData;
		static const TArray<FVector2D>         EmptyUVData;
		static const TArray<FPackedNormal>     EmptyTangentXZData;
		static const TArray<FColor>            EmptyColorData;
		static const TArray<MRMESH_INDEX_TYPE> EmptyIndices;
		static_cast<IMRMesh*>(MRMeshComponent)->SendBrickData(
			IMRMesh::FSendBrickDataArgs{
				nullptr,
				*MappedBrickId,
				EmptyPositionData,
				EmptyUVData,
				EmptyTangentXZData,
				EmptyColorData,
				EmptyIndices
			});
		// Now that the block has been removed from the MRMesh, we can safely
		//	re-use the same BrickId //
		const int32 NumRemoved = PlaneIdToBrickId.Remove(PlaneId);
		verify(NumRemoved == 1);
	}
	else
	{
		UE_LOG(LogMagicLeap, Warning,
			   TEXT("Attempting to destroy unmapped PlaneId! Ignoring."));
	}
}

IMRMesh::FBrickId ALuminAROrigin::GetBrickId(const FGuid& PlaneId)
{
	IMRMesh::FBrickId*const MappedBrickId = PlaneIdToBrickId.Find(PlaneId);
	if(MappedBrickId)
	{
		return *MappedBrickId;
	}
	const IMRMesh::FBrickId NewBrickId = NextBrickId++;
	PlaneIdToBrickId.Add(PlaneId, NewBrickId);
	return NewBrickId;
}

UWorld* ALuminAROrigin::FindWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game || 
			Context.WorldType == EWorldType::PIE)
		{
			return Context.World();
		}
	}
	return nullptr;
}
