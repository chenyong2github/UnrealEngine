// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionDebugDrawComponent.h"

#if GEOMETRYCOLLECTION_DEBUG_DRAW
#include "GeometryCollection/GeometryCollectionRenderLevelSetActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionDebugDrawActor.h"
#include "GeometryCollection/GeometryCollection.h"
#if INCLUDE_CHAOS
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#endif  // #if INCLUDE_CHAOS
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGeometryCollectionDebugDraw, Log, All);

// Constants
namespace GeometryCollectionDebugDrawComponentConstants
{
	static const FLinearColor DarkerTintFactor(1.0f, 1.0f, 0.7f);  // Darker HSV multiplier
	static const FLinearColor LighterTintFactor(1.0f, 1.0f, 2.0f);  // Lighter HSV multiplier

	static const FLinearColor RigidBodyTint(0.8f, 0.1f, 0.1f);  // Red
	static const FLinearColor ClusteringTint(0.6, 0.4f, 0.2f);  // Orange
	static const FLinearColor GeometryTint(0.4f, 0.2f, 0.6f);  // Purple
	static const FLinearColor SingleFaceTint(0.6, 0.2f, 0.4f);  // Pink
	static const FLinearColor VertexTint(0.2f, 0.4f, 0.6f);  // Blue

	static const FColor RigidBodyIdsColorDefault = (RigidBodyTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyCollisionColorDefault = RigidBodyTint.ToFColor(true);
	static const FColor RigidBodyInertiaColorDefault = (RigidBodyTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyVelocityColorDefault = (RigidBodyTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyForceColorDefault = (RigidBodyTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor RigidBodyInfoColorDefault = (RigidBodyTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor TransformIndexColorDefault = (ClusteringTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor LevelColorDefault = (ClusteringTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor ParentColorDefault = ClusteringTint.ToFColor(true);
	static const FColor GeometryIndexColorDefault = (GeometryTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor BoundingBoxColorDefault = (GeometryTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor FaceColorDefault = GeometryTint.ToFColor(true);
	static const FColor FaceIndexColorDefault = (GeometryTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor FaceNormalColorDefault = (GeometryTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor SingleFaceColorDefault = (SingleFaceTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor VertexColorDefault = VertexTint.ToFColor(true);
	static const FColor VertexIndexColorDefault = (VertexTint.LinearRGBToHSV() * LighterTintFactor).HSVToLinearRGB().ToFColor(true);
	static const FColor VertexNormalColorDefault = (VertexTint.LinearRGBToHSV() * DarkerTintFactor).HSVToLinearRGB().ToFColor(true);
}

// Static member variables
UGeometryCollectionDebugDrawComponent* UGeometryCollectionDebugDrawComponent::RenderLevelSetOwner = nullptr;
int32 UGeometryCollectionDebugDrawComponent::LastRenderedId = INDEX_NONE;

UGeometryCollectionDebugDrawComponent::UGeometryCollectionDebugDrawComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bShowRigidBodyIds(false)
	, bShowRigidBodyTransforms(false)
	, bShowRigidBodyCollisions(false)
	, bShowRigidBodyInertias(false)
	, bShowRigidBodyVelocities(false)
	, bShowRigidBodyForces(false)
	, bShowRigidBodyInfos(false)
	, RigidBodyIdColor(GeometryCollectionDebugDrawComponentConstants::RigidBodyIdsColorDefault)
	, RigidBodyTransformScale(1.0f)
	, RigidBodyCollisionColor(GeometryCollectionDebugDrawComponentConstants::RigidBodyCollisionColorDefault)
	, RigidBodyInertiaColor(GeometryCollectionDebugDrawComponentConstants::RigidBodyInertiaColorDefault)
	, RigidBodyVelocityColor(GeometryCollectionDebugDrawComponentConstants::RigidBodyVelocityColorDefault)
	, RigidBodyForceColor(GeometryCollectionDebugDrawComponentConstants::RigidBodyForceColorDefault)
	, RigidBodyInfoColor(GeometryCollectionDebugDrawComponentConstants::RigidBodyInfoColorDefault)
	, bShowTransformIndices(false)
	, bShowTransforms(false)
	, bShowLevels(false)
	, bShowParents(false)
	, bShowConnectivityEdges(false)
	, TransformIndexColor(GeometryCollectionDebugDrawComponentConstants::TransformIndexColorDefault)
	, TransformScale(1.0f)
	, LevelColor(GeometryCollectionDebugDrawComponentConstants::LevelColorDefault)
	, ParentColor(GeometryCollectionDebugDrawComponentConstants::ParentColorDefault)
	, ConnectivityEdgeThickness(1.0f)
	, bShowGeometryIndices(false)
	, bShowGeometryTransforms(false)
	, bShowBoundingBoxes(false)
	, bShowFaces(false)
	, bShowFaceIndices(false)
	, bShowFaceNormals(false)
	, bShowSingleFace(false)
	, SingleFaceIndex(0)
	, bShowVertices(false)
	, bShowVertexIndices(false)
	, bShowVertexNormals(false)
	, GeometryIndexColor(GeometryCollectionDebugDrawComponentConstants::GeometryIndexColorDefault)
	, GeometryTransformScale(1.0f)
	, BoundingBoxColor(GeometryCollectionDebugDrawComponentConstants::BoundingBoxColorDefault)
	, FaceColor(GeometryCollectionDebugDrawComponentConstants::FaceColorDefault)
	, FaceIndexColor(GeometryCollectionDebugDrawComponentConstants::FaceIndexColorDefault)
	, FaceNormalColor(GeometryCollectionDebugDrawComponentConstants::FaceNormalColorDefault)
	, SingleFaceColor(GeometryCollectionDebugDrawComponentConstants::SingleFaceColorDefault)
	, VertexColor(GeometryCollectionDebugDrawComponentConstants::VertexColorDefault)
	, VertexIndexColor(GeometryCollectionDebugDrawComponentConstants::VertexIndexColorDefault)
	, VertexNormalColor(GeometryCollectionDebugDrawComponentConstants::VertexNormalColorDefault)
	, GeometryCollectionDebugDrawActor(nullptr)
	, GeometryCollectionRenderLevelSetActor(nullptr)
	, GeometryCollectionComponent(nullptr)
#if GEOMETRYCOLLECTION_DEBUG_DRAW
#if INCLUDE_CHAOS
	, ParticlesData()
#endif  // #if INCLUDE_CHAOS
	, ParentCheckSum(0)
	, SelectedRigidBodyId(INDEX_NONE)
	, SelectedTransformIndex(INDEX_NONE)
	, HiddenTransformIndex(INDEX_NONE)
	, bWasVisible(true)
	, bHasIncompleteRigidBodyIdSync(false)
	, SelectedChaosSolver(nullptr)
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
{
	bNavigationRelevant = false;
	bTickInEditor = false;
#if GEOMETRYCOLLECTION_DEBUG_DRAW
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
#else  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
	PrimaryComponentTick.bCanEverTick = false;
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW #else
}

void UGeometryCollectionDebugDrawComponent::BeginPlay()
{
	Super::BeginPlay();

#if GEOMETRYCOLLECTION_DEBUG_DRAW
	// Should always start with ticking disabled
	check(!PrimaryComponentTick.IsTickFunctionEnabled());

	if (GeometryCollectionComponent)
	{
		// Reset all index variables
		SelectedRigidBodyId = INDEX_NONE;
		SelectedTransformIndex = INDEX_NONE;
		HiddenTransformIndex = INDEX_NONE;
		bWasVisible = true;

		// Find or create global debug draw actor
		GeometryCollectionDebugDrawActor = AGeometryCollectionDebugDrawActor::FindOrCreate(GetWorld());
		if (ensure(GeometryCollectionDebugDrawActor))
		{
			// Make sure to tick the debug draw actor first
			// It is required to clear up the persistent lines before drawing a new frame
			AddTickPrerequisiteActor(GeometryCollectionDebugDrawActor);
		}

		// Update the visibility and tick status depending on the debug draw properties currently selected
		OnDebugDrawPropertiesChanged(false);

#if INCLUDE_CHAOS
		// Find or create level set renderer
		GeometryCollectionRenderLevelSetActor = AGeometryCollectionRenderLevelSetActor::FindOrCreate(GetWorld());
#endif  // #if INCLUDE_CHAOS
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
}

void UGeometryCollectionDebugDrawComponent::EndPlay(EEndPlayReason::Type ReasonEnd)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW
	if (GeometryCollectionComponent)
	{
		// Garbage collect debug draw singleton actor (needs this before updating properties to restore visibility)
		GeometryCollectionDebugDrawActor = nullptr;

		// Turn off level set renderer (needs this before updating properties to disable the level set renderer)
		if (RenderLevelSetOwner == this)
		{
			LastRenderedId = INDEX_NONE;
		}

		// Refresh states from end-play properties
		OnDebugDrawPropertiesChanged(false);

		// Garbage collect levelset rendering actor (needs this after updating properties)
		GeometryCollectionRenderLevelSetActor = nullptr;
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
	Super::EndPlay(ReasonEnd);
}

void UGeometryCollectionDebugDrawComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, VeryVerbose, TEXT("Component ticked for actor %s."), *GetOwner()->GetName());

#if GEOMETRYCOLLECTION_DEBUG_DRAW
	if (GeometryCollectionComponent && GeometryCollectionDebugDrawActor)
	{
		if (GeometryCollectionComponent->RestCollection)
		{
			// Debug draw collection
			DebugDrawTick();

#if INCLUDE_CHAOS
			if (GeometryCollectionRenderLevelSetActor)
			{
				// Debug draw solver infos for this collection
				DebugDrawChaosTick();
			}
#endif  // #if INCLUDE_CHAOS

			// Detect breaking by tracking changes in parents so that the geometry visibility can be updated if required
			// Note: The GeometryCollectionComponent breaking callback is unsuitable for this purpose as it notifies changes before any array gets updated.
			const int32 PrevParentCheckSum = ParentCheckSum;
			ParentCheckSum = 0;
			for (int32 ParentIndex : GeometryCollectionComponent->GetParentArray())
			{
				ParentCheckSum += ParentIndex;
			}
			if (ParentCheckSum != PrevParentCheckSum)
			{
				UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Verbose, TEXT("Geometry Collection has broken up for actor %s."), *GetOwner()->GetName());
				UpdateGeometryVisibility(true);
			}
		}
		else
		{
			UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Warning, TEXT("Null Rest Collection for actor %s, skipping Debug Draw Component tick."), *GetOwner()->GetName());
		}
	}
	else
	{
		UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Warning, TEXT("Invalid Debug Draw Component for actor %s, tick is now disabled."), *GetOwner()->GetName());
		SetComponentTickEnabled(false);
	}
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
}

#if WITH_EDITOR
void UGeometryCollectionDebugDrawComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
#if GEOMETRYCOLLECTION_DEBUG_DRAW
	if (GeometryCollectionComponent)
	{
		const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName(): NAME_None;
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UGeometryCollectionDebugDrawComponent, SingleFaceIndex))
		{
			const int32 NumFaces = GeometryCollectionComponent->GetNumElements(FGeometryCollection::FacesGroup);
			SingleFaceIndex = FMath::Clamp(SingleFaceIndex, 0, NumFaces - 1);
		}
	}

	// Update selection and visibility
	OnDebugDrawPropertiesChanged(false);
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UGeometryCollectionDebugDrawComponent::CanEditChange(const UProperty* InProperty) const
{
	if (HasBegunPlay())
	{
		return false;
	}
	return Super::CanEditChange(InProperty);
}
#endif  // #if WITH_EDITOR

#if GEOMETRYCOLLECTION_DEBUG_DRAW
bool UGeometryCollectionDebugDrawComponent::OnDebugDrawPropertiesChanged(bool bForceVisibilityUpdate)
{
	if (HasBegunPlay() && GeometryCollectionComponent)
	{
		UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Verbose, TEXT("OnDebugDrawPropertiesChanged for actor %s"), *GetOwner()->GetName());

		// Make sure to have an up to date selected transform index
		UpdateSelectedTransformIndex();

		// Update geometry visibility
		UpdateGeometryVisibility(bForceVisibilityUpdate);

#if INCLUDE_CHAOS
		// Turn off level set rendering when required
		UpdateLevelSetVisibility();
#endif  // #if INCLUDE_CHAOS

		// Update tick function
		UpdateTickStatus();
	}
	return SelectedTransformIndex != INDEX_NONE;
}

void UGeometryCollectionDebugDrawComponent::OnClusterChanged()
{
	if (HasBegunPlay() && GeometryCollectionComponent && IsComponentTickEnabled())  // Only proceed if the tick is already enabled
	{
		UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Verbose, TEXT("OnClusterChanged for actor %s"), *GetOwner()->GetName());

		// Make sure to have an up to date selected transform index
		UpdateSelectedTransformIndex();

		// Update geometry visibility
		UpdateGeometryVisibility(true);  // Force visibility update

#if INCLUDE_CHAOS
		// Turn off level set rendering when required
		UpdateLevelSetVisibility();
#endif  // #if INCLUDE_CHAOS
	}
}

void UGeometryCollectionDebugDrawComponent::ComputeClusterTransforms(int32 Index, TArray<bool>& IsComputed, TArray<FTransform>& InOutGlobalTransforms)
{
	if (!IsComputed[Index])
	{
		FTransform& Transform = InOutGlobalTransforms[Index];

		// Set to local transform
		const TManagedArray<FTransform>& Transforms = GeometryCollectionComponent->GetTransformArray();
		Transform = Transforms[Index];

		// Recurse through parents and transform this from local space to global space
		const TManagedArray<int32>& Parents = GeometryCollectionComponent->GetParentArray();
		const int32 ParentIndex = Parents[Index];
		if (ParentIndex != FGeometryCollection::Invalid)
		{
			ComputeClusterTransforms(ParentIndex, IsComputed, InOutGlobalTransforms);
			Transform *= InOutGlobalTransforms[ParentIndex];
		}
		IsComputed[Index] = true;
	}
}

void UGeometryCollectionDebugDrawComponent::ComputeTransforms(TArray<FTransform>& OutClusterTransforms, TArray<FTransform>& OutGeometryTransforms)
{
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);

	const TManagedArray<FTransform>& Transforms = GeometryCollectionComponent->GetTransformArray();
	const TManagedArray<FTransform>& RestTransforms = GeometryCollectionComponent->GetTransformArrayRest();
	const TManagedArray<int32>& Parents = GeometryCollectionComponent->GetParentArray();
	const TManagedArray<TSet<int32>>& Children = GeometryCollectionComponent->GetChildrenArray();

	check(Transforms.Num() == RestTransforms.Num());
	const int32 NumTransforms = Transforms.Num();

	const AActor* const Actor = GetOwner();
	check(Actor);
	const FTransform ActorTransform = Actor->GetTransform();

	// Initialize all flip flop array values to false
	TArray<bool> FlipFlopComputeStatus;
	FlipFlopComputeStatus.AddZeroed(NumTransforms);

	// First pass, go up the hierarchy to calculate the leaf/cluster global transforms, not including the actor's tranform
	OutClusterTransforms.SetNumUninitialized(NumTransforms, false);
	for (int32 Index = 0; Index < NumTransforms; ++Index)
	{
		ComputeClusterTransforms(Index, FlipFlopComputeStatus, OutClusterTransforms);
	}

	// Second pass, starts from the leaves and go up the hierarchy to update the parents' transform using the rest transform array
	// Also applies the actor transform to the calculated transforms
	OutGeometryTransforms.SetNumUninitialized(NumTransforms, false);

	for (int32 Index = 0; Index < NumTransforms; ++Index)
	{
		// Start from the leaves, since these have the only up to date geometry transforms
		if (!Children[Index].Num())
		{
			// Apply actor transform to leaves
			OutClusterTransforms[Index] *= ActorTransform;

			// Copy child geometry transform
			OutGeometryTransforms[Index] = OutClusterTransforms[Index];

			// Iterate up the hierarchy to update the parent transform, stop when it finds a parent that has already been computed
			for (int32 ChildIndex = Index, ParentIndex = Parents[Index];
				ParentIndex != FGeometryCollection::Invalid && FlipFlopComputeStatus[ParentIndex];
				ParentIndex = Parents[ChildIndex = ParentIndex])
			{
				// Finalize the remaining cluster nodes' transform by applying the actor transform
				OutClusterTransforms[ParentIndex] *= ActorTransform;

				// Calculate final geometry transform from the current child's transform
				const FTransform& ParentToChildTransform = RestTransforms[ChildIndex];
				OutGeometryTransforms[ParentIndex] = ParentToChildTransform.Inverse() * OutGeometryTransforms[ChildIndex];
				
				// Mark this parent calculation as completed
				FlipFlopComputeStatus[ParentIndex] = false;
			}
		}
	}
}

void UGeometryCollectionDebugDrawComponent::DebugDrawTick()
{
	check(GeometryCollectionComponent);
	check(GeometryCollectionComponent->RestCollection);
	check(GeometryCollectionDebugDrawActor);

	AActor* const Actor = GetOwner();
	check(Actor);

	// Compute world space geometry and cluster transforms
	TArray<FTransform> ClusterTransforms;
	TArray<FTransform> GeometryTransforms;
	ComputeTransforms(ClusterTransforms, GeometryTransforms);

	const bool bIsSelected = (SelectedTransformIndex != INDEX_NONE);
	const bool bIsOneSelected = bIsSelected && !GeometryCollectionDebugDrawActor->bDebugDrawWholeCollection;
	const bool bAreAllSelected = (bIsSelected && GeometryCollectionDebugDrawActor->bDebugDrawWholeCollection) ||
		(GeometryCollectionDebugDrawActor->SelectedRigidBody.Id == INDEX_NONE && 
		 GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver == GeometryCollectionComponent->ChaosSolverActor);

	// Clustering
	if (!bShowTransformIndices && GeometryCollectionDebugDrawActor->bShowTransformIndex && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawTransformIndex(ClusterTransforms, GeometryCollectionComponent, SelectedTransformIndex, TransformIndexColor);
	}
	else if (bShowTransformIndices || (GeometryCollectionDebugDrawActor->bShowTransformIndex && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawTransformIndices(ClusterTransforms, GeometryCollectionComponent, TransformIndexColor);
	}

	if (!bShowTransforms && GeometryCollectionDebugDrawActor->bShowTransform && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawTransform(ClusterTransforms, GeometryCollectionComponent, SelectedTransformIndex, TransformScale);
	}
	else if (bShowTransforms || (GeometryCollectionDebugDrawActor->bShowTransform && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawTransforms(ClusterTransforms, GeometryCollectionComponent, TransformScale);
	}

	if (!bShowParents && GeometryCollectionDebugDrawActor->bShowParent && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawParent(ClusterTransforms, GeometryCollectionComponent, SelectedTransformIndex, ParentColor);
	}
	else if (bShowParents || (GeometryCollectionDebugDrawActor->bShowParent && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawParents(ClusterTransforms, GeometryCollectionComponent, ParentColor);
	}

	if (!bShowLevels && GeometryCollectionDebugDrawActor->bShowLevel && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawLevel(ClusterTransforms, GeometryCollectionComponent, SelectedTransformIndex, LevelColor);
	}
	else if (bShowLevels || (GeometryCollectionDebugDrawActor->bShowLevel && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawLevels(ClusterTransforms, GeometryCollectionComponent, LevelColor);
	}

	// Geometry
	if (!bShowVertices && GeometryCollectionDebugDrawActor->bShowVertices && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawVertices(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, VertexColor);
	}
	else if (bShowVertices || (GeometryCollectionDebugDrawActor->bShowVertices && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawVertices(GeometryTransforms, GeometryCollectionComponent, VertexColor);
	}

	if (!bShowVertexIndices && GeometryCollectionDebugDrawActor->bShowVertexIndices && bIsOneSelected)
	{ 
		GeometryCollectionDebugDrawActor->DrawVertexIndices(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, VertexIndexColor);
	}
	else if (bShowVertexIndices || (GeometryCollectionDebugDrawActor->bShowVertexIndices && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawVertexIndices(GeometryTransforms, GeometryCollectionComponent, VertexIndexColor);
	}

	if (!bShowVertexNormals && GeometryCollectionDebugDrawActor->bShowVertexNormals && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawVertexNormals(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, VertexNormalColor);
	}
	else if (bShowVertexNormals || (GeometryCollectionDebugDrawActor->bShowVertexNormals && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawVertexNormals(GeometryTransforms, GeometryCollectionComponent, VertexNormalColor);
	}

	if (!bShowFaces && GeometryCollectionDebugDrawActor->bShowFaces && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawFaces(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, FaceColor);
	}
	else if (bShowFaces || (GeometryCollectionDebugDrawActor->bShowFaces && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawFaces(GeometryTransforms, GeometryCollectionComponent, FaceColor);
	}

	if (!bShowFaceIndices && GeometryCollectionDebugDrawActor->bShowFaceIndices && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawFaceIndices(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, FaceIndexColor);
	}
	else if (bShowFaceIndices || (GeometryCollectionDebugDrawActor->bShowFaceIndices && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawFaceIndices(GeometryTransforms, GeometryCollectionComponent, FaceIndexColor);
	}

	if (bShowSingleFace)
	{
		GeometryCollectionDebugDrawActor->DrawSingleFace(GeometryTransforms, GeometryCollectionComponent, SingleFaceIndex, SingleFaceColor);
	}
	if (GeometryCollectionDebugDrawActor->bShowSingleFace && (bIsOneSelected || bAreAllSelected))  // No else required here, it should be able to draw the two faces at the same time
	{
		GeometryCollectionDebugDrawActor->DrawSingleFace(GeometryTransforms, GeometryCollectionComponent, GeometryCollectionDebugDrawActor->SingleFaceIndex, SingleFaceColor);
	}

	if (!bShowFaceNormals && GeometryCollectionDebugDrawActor->bShowFaceNormals && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawFaceNormals(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, FaceNormalColor);
	}
	else if (bShowFaceNormals || (GeometryCollectionDebugDrawActor->bShowFaceNormals && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawFaceNormals(GeometryTransforms, GeometryCollectionComponent, FaceNormalColor);
	}

	if (!bShowGeometryIndices && GeometryCollectionDebugDrawActor->bShowGeometryIndex && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawGeometryIndex(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, GeometryIndexColor);
	}
	else if (bShowGeometryIndices || (GeometryCollectionDebugDrawActor->bShowGeometryIndex && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawGeometryIndices(GeometryTransforms, GeometryCollectionComponent, GeometryIndexColor);
	}

	if (!bShowGeometryTransforms && GeometryCollectionDebugDrawActor->bShowGeometryTransform && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawTransform(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, GeometryTransformScale);
	}
	else if (bShowGeometryTransforms || (GeometryCollectionDebugDrawActor->bShowGeometryTransform && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawTransforms(GeometryTransforms, GeometryCollectionComponent, GeometryTransformScale);
	}

	if (!bShowBoundingBoxes && GeometryCollectionDebugDrawActor->bShowBoundingBox && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawBoundingBox(GeometryTransforms, GeometryCollectionComponent, SelectedTransformIndex, BoundingBoxColor);
	}
	else if (bShowBoundingBoxes || (GeometryCollectionDebugDrawActor->bShowBoundingBox && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawBoundingBoxes(GeometryTransforms, GeometryCollectionComponent, BoundingBoxColor);
	}
}

void UGeometryCollectionDebugDrawComponent::UpdateSelectedTransformIndex()
{
	check(GeometryCollectionComponent);

	// No actor, no selection
	if (!GeometryCollectionDebugDrawActor)
	{
		SelectedTransformIndex = SelectedRigidBodyId = INDEX_NONE;
		return;
	}

	// Check whether the selected rigid body id, or solver has changed
	if (!bHasIncompleteRigidBodyIdSync &&
		SelectedRigidBodyId == GeometryCollectionDebugDrawActor->SelectedRigidBody.Id &&
		SelectedChaosSolver == GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver)
	{
		return;
	}

	// Default init selected transform index, in case of premature exit
	SelectedTransformIndex = INDEX_NONE;

	// Simple test to allow for an early exit when nothing has been selected
	if (GeometryCollectionDebugDrawActor->SelectedRigidBody.Id == INDEX_NONE ||
		GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver != GeometryCollectionComponent->ChaosSolverActor)
	{
		SelectedRigidBodyId = GeometryCollectionDebugDrawActor->SelectedRigidBody.Id;
		SelectedChaosSolver = GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver;
		return;
	}

	// Check rigid body id sync
	// Note that this test alone isn't enough to ensure that the rigid body ids are valid.
	const TManagedArray<int32>& RigidBodyIds = GeometryCollectionComponent->RigidBodyIds;
	if (RigidBodyIds.Num() == 0)
	{
		bHasIncompleteRigidBodyIdSync = !!GeometryCollectionComponent->GetTransformArray().Num();
		UE_CLOG(bHasIncompleteRigidBodyIdSync && GetOwner(), LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateSelectedTransformIndex(): Empty RigidBodyIds array for actor %s."), *GetOwner()->GetName());
		return;
	}

	// Find the matching transform if any (and also check the sync completion status)
	bHasIncompleteRigidBodyIdSync = false;

	const TManagedArray<TSet<int32>>& ChildrenRest = GeometryCollectionComponent->GetChildrenArrayRest();
	const TManagedArray<TSet<int32>>& Children = GeometryCollectionComponent->GetChildrenArray();

	for (int32 TransformIndex = 0; TransformIndex < RigidBodyIds.Num(); ++TransformIndex)
	{
		// Is this the selected id?
		if (RigidBodyIds[TransformIndex] == GeometryCollectionDebugDrawActor->SelectedRigidBody.Id)
		{
			SelectedTransformIndex = TransformIndex;
			bHasIncompleteRigidBodyIdSync = false;  // Found it, the wait for a sync can be canceled
			break;
		}
		// Check the reason behind any invalid index
		if (RigidBodyIds[TransformIndex] == INDEX_NONE)
		{
			// Look for detached clusters in order to differentiate un-synced vs empty cluster rigid body ids.
			int32 ChildTransformIndex = TransformIndex;
			while (const TSet<int32>::TConstIterator ChildTransformIterator = Children[ChildTransformIndex].CreateConstIterator())
			{
				// Go down to the cluster's leaf level through the first child
				ChildTransformIndex = *ChildTransformIterator;
			}

			// If this is a leaf bone, it can not be a detached cluster so it should have a valid rigid body
			// In which case the sync has yet to happen and it might be worth trying this again later
			if (!ChildrenRest[ChildTransformIndex].Num())
			{
				bHasIncompleteRigidBodyIdSync = true;
				UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, VeryVerbose, TEXT("UpdateSelectedTransformIndex(): Invalid rigid body id for actor %s, TransformIndex %d."), *GetOwner()->GetName(), TransformIndex);
			}
			else
			{
				// This should match the SimulationType == FST_CLUSTERED or IsClustered(int32 Element)
				ensure(GeometryCollectionComponent->GetSimulationTypeArrayRest()[ChildTransformIndex] == FGeometryCollection::ESimulationTypes::FST_Clustered);
				UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, VeryVerbose, TEXT("UpdateSelectedTransformIndex(): Found empty cluster for actor %s, TransformIndex %d."), *GetOwner()->GetName(), TransformIndex);
			}
		}
	}
	UE_CLOG(bHasIncompleteRigidBodyIdSync && GetOwner(), LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateSelectedTransformIndex(): Invalid RigidBodyIds array elements for actor %s."), *GetOwner()->GetName());

	// Update selected rigid body index and solver
	SelectedRigidBodyId = GeometryCollectionDebugDrawActor->SelectedRigidBody.Id;
	SelectedChaosSolver = GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver;
}

int32 UGeometryCollectionDebugDrawComponent::CountFaces(int32 TransformIndex, bool bDebugDrawClustering) const
{
	check(GeometryCollectionComponent);
	int32 FaceCount = 0;
	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];
	if (GeometryIndex != INDEX_NONE)
	{
		const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();
		FaceCount += FaceCountArray[GeometryIndex];
	}
	const TManagedArray<TSet<int32>>& ChildrenArray = bDebugDrawClustering ?
		GeometryCollectionComponent->GetChildrenArrayRest():
		GeometryCollectionComponent->GetChildrenArray();
	for (int32 HiddenChildIndex : ChildrenArray[TransformIndex])
	{
		FaceCount += CountFaces(HiddenChildIndex, bDebugDrawClustering);
	}
	return FaceCount;
}

void UGeometryCollectionDebugDrawComponent::HideFaces(int32 TransformIndex, bool bDebugDrawClustering)
{
	check(GeometryCollectionComponent);
	const TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollectionComponent->GetTransformToGeometryIndexArray();
	const int32 GeometryIndex = TransformToGeometryIndexArray[TransformIndex];
	if (GeometryIndex != INDEX_NONE)
	{
		TManagedArray<bool>& VisibleArray = GeometryCollectionComponent->GetVisibleArrayCopyOnWrite();
		const TManagedArray<int32>& FaceStartArray = GeometryCollectionComponent->GetFaceStartArray();
		const TManagedArray<int32>& FaceCountArray = GeometryCollectionComponent->GetFaceCountArray();
		const int32 FaceStart = FaceStartArray[GeometryIndex];
		const int32 FaceCount = FaceCountArray[GeometryIndex];
		for (int32 FaceIndex = FaceStart; FaceIndex < FaceStart + FaceCount; ++FaceIndex)
		{
			VisibleArray[FaceIndex] = false;
		}
	}
	const TManagedArray<TSet<int32>>& ChildrenArray = bDebugDrawClustering ?
		GeometryCollectionComponent->GetChildrenArrayRest():
		GeometryCollectionComponent->GetChildrenArray();
	for (int32 HiddenChildIndex : ChildrenArray[TransformIndex])
	{
		HideFaces(HiddenChildIndex, bDebugDrawClustering);
	}
}

void UGeometryCollectionDebugDrawComponent::UpdateGeometryVisibility(bool bForceVisibilityUpdate)
{
	check(GeometryCollectionComponent);
	if (!GeometryCollectionComponent->RestCollection)  // Required for GetTransformIndexArray
	{
		UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Warning, TEXT("UpdateGeometryVisibility(): Empty RestCollection for actor %s"), *GetOwner()->GetName());
		return;
	}

	// Keep old hidden index
	const int32 PrevHiddenIndex = HiddenTransformIndex;

	// Extract this object's visibility arguments from the debug draw actor's hide geometry status
	bool bIsVisible;
	if (!GeometryCollectionDebugDrawActor)
	{
		bIsVisible = true;
		HiddenTransformIndex = INDEX_NONE;
	}
	else
	{
		// Work out partial changes in visiblity
		const bool bIsSelected = (SelectedTransformIndex != INDEX_NONE);
		const bool bAreAllSelected = (bIsSelected && GeometryCollectionDebugDrawActor->bDebugDrawWholeCollection) ||
			(GeometryCollectionDebugDrawActor->SelectedRigidBody.Id == INDEX_NONE && 
			 GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver == GeometryCollectionComponent->ChaosSolverActor);
		const bool bAreAnySelected = bIsSelected || bAreAllSelected;

		switch (GeometryCollectionDebugDrawActor->HideGeometry)
		{
		default:  // Intentional fallthrough
		case EGeometryCollectionDebugDrawActorHideGeometry::HideNone:
			bIsVisible = true;
			HiddenTransformIndex = INDEX_NONE;
			break;
		case EGeometryCollectionDebugDrawActorHideGeometry::HideWithCollision:
			if (bShowRigidBodyCollisions || (bAreAnySelected && GeometryCollectionDebugDrawActor->bShowRigidBodyCollision))
			{
				bIsVisible = !bAreAllSelected;
				HiddenTransformIndex = bIsVisible ? SelectedTransformIndex: INDEX_NONE;
			}
			else
			{
				bIsVisible = true;
				HiddenTransformIndex = INDEX_NONE;
			}
			break;
		case EGeometryCollectionDebugDrawActorHideGeometry::HideSelected:
			bIsVisible = !bAreAllSelected;
			HiddenTransformIndex = bIsVisible ? SelectedTransformIndex: INDEX_NONE;
			break;
		case EGeometryCollectionDebugDrawActorHideGeometry::HideWholeCollection:
			bIsVisible = !bAreAnySelected;
			HiddenTransformIndex = INDEX_NONE;
			break;
		case EGeometryCollectionDebugDrawActorHideGeometry::HideAll:
			bIsVisible = false;
			HiddenTransformIndex = INDEX_NONE;
			break;
		}
	}

	// Update face visibility
	bool bIndexHasChanged = (HiddenTransformIndex != PrevHiddenIndex) || bForceVisibilityUpdate;
	if (bIndexHasChanged)
	{
		// This must happen in both show and hidden geometry mode so that the last 
		// hidden section does not stay hidden when switching between Selected>All>None.

		// todo(ocohen): add ability to use rest collection (i.e. don't use instance copy)
		TManagedArray<bool>& VisibleArray = GeometryCollectionComponent->GetVisibleArrayCopyOnWrite();

		// Reset visibility array to default as the index has changed
		VisibleArray.Init(GeometryCollectionComponent->GetVisibleArrayRest());
		UE_LOG(LogGeometryCollectionDebugDraw, Verbose, TEXT("VisibleArray re-initialized."));

		if (HiddenTransformIndex != INDEX_NONE)
		{
			const int32 NumFaces = CountFaces(HiddenTransformIndex, GeometryCollectionDebugDrawActor->bDebugDrawClustering);
			if (NumFaces < VisibleArray.Num())
			{
				// Hide this geometry's faces
				HideFaces(HiddenTransformIndex, GeometryCollectionDebugDrawActor->bDebugDrawClustering);
				UE_LOG(LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateGeometryVisibility(): Hidding partial object."));
			}
			else
			{
				// Hide entire object
				// Can't send zero vertices to force the vertex buffer to be empty, so hide the component instead
				bIsVisible = false;
				HiddenTransformIndex = INDEX_NONE;
				// Update index has changed
				bIndexHasChanged = (HiddenTransformIndex != PrevHiddenIndex);
				UE_LOG(LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateGeometryVisibility(): Hidding entire object."));
			}
		}
	}
	UE_CLOG(bIndexHasChanged, LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateGeometryVisibility(): Index has changed. Prev index = %d, new index = %d."), PrevHiddenIndex, HiddenTransformIndex);

	// Force component reinit
	if (bIndexHasChanged)
	{
		UE_LOG(LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateGeometryVisibility(): Forcing init render data."));
		GeometryCollectionComponent->ForceRenderUpdateConstantData();
	}

	// Update component visibility, only if the component visibility hasn't changed in between the last call (or unless the change is in sync with the component visibility)
	const bool bIsComponentVisible = GeometryCollectionComponent->IsVisible();
	const bool bAllowVisibilityChange = (bIsComponentVisible || !bWasVisible);
	if (bAllowVisibilityChange)
	{
		const bool bVisibilityHasChanged = (bIsVisible != bIsComponentVisible);
		if (bVisibilityHasChanged)
		{
			UE_LOG(LogGeometryCollectionDebugDraw, Verbose, TEXT("UpdateGeometryVisibility(): Visibility has changed. Old visibility = %d, new visibility = %d."), bWasVisible, bIsVisible);
			GeometryCollectionComponent->SetVisibility(bIsVisible);
		}
		bWasVisible = bIsVisible;  // Only update when changes are allowed so that the component can stay hidden when visibility is out of sync.
	}
}

void UGeometryCollectionDebugDrawComponent::UpdateTickStatus()
{
	bool bIsEnabled;
	if (!GeometryCollectionDebugDrawActor)
	{
		bIsEnabled = false;
	}
	else
	{
		// Check whether anything from this component is selected for debug drawing
		const bool bAreAnySelected = (SelectedTransformIndex != INDEX_NONE ||
			(GeometryCollectionDebugDrawActor->SelectedRigidBody.Id == INDEX_NONE &&
			 GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver == GeometryCollectionComponent->ChaosSolverActor));

		bIsEnabled =
			(bAreAnySelected && (
				   GeometryCollectionDebugDrawActor->bShowRigidBodyId  // Check the debug draw's actor debug options
				|| GeometryCollectionDebugDrawActor->bShowRigidBodyCollision
				|| GeometryCollectionDebugDrawActor->bShowRigidBodyTransform
				|| GeometryCollectionDebugDrawActor->bShowRigidBodyInertia
				|| GeometryCollectionDebugDrawActor->bShowRigidBodyVelocity
				|| GeometryCollectionDebugDrawActor->bShowRigidBodyForce
				|| GeometryCollectionDebugDrawActor->bShowRigidBodyInfos
				|| GeometryCollectionDebugDrawActor->bShowTransformIndex
				|| GeometryCollectionDebugDrawActor->bShowTransform
				|| GeometryCollectionDebugDrawActor->bShowParent
				|| GeometryCollectionDebugDrawActor->bShowLevel
				|| GeometryCollectionDebugDrawActor->bShowConnectivityEdges
				|| GeometryCollectionDebugDrawActor->bShowGeometryIndex
				|| GeometryCollectionDebugDrawActor->bShowGeometryTransform
				|| GeometryCollectionDebugDrawActor->bShowBoundingBox
				|| GeometryCollectionDebugDrawActor->bShowFaces
				|| GeometryCollectionDebugDrawActor->bShowFaceIndices
				|| GeometryCollectionDebugDrawActor->bShowFaceNormals
				|| GeometryCollectionDebugDrawActor->bShowSingleFace
				|| GeometryCollectionDebugDrawActor->SingleFaceIndex
				|| GeometryCollectionDebugDrawActor->bShowVertices
				|| GeometryCollectionDebugDrawActor->bShowVertexIndices
				|| GeometryCollectionDebugDrawActor->bShowVertexNormals))
			|| bShowRigidBodyIds  // Check this component's debug options
			|| bShowRigidBodyTransforms
			|| bShowRigidBodyCollisions
			|| bShowRigidBodyInertias
			|| bShowRigidBodyVelocities
			|| bShowRigidBodyForces
			|| bShowRigidBodyInfos
			|| bShowTransformIndices
			|| bShowTransforms
			|| bShowLevels
			|| bShowParents
			|| bShowConnectivityEdges
			|| bShowGeometryIndices
			|| bShowGeometryTransforms
			|| bShowBoundingBoxes
			|| bShowFaces
			|| bShowFaceIndices
			|| bShowFaceNormals
			|| bShowSingleFace
			|| bShowVertices
			|| bShowVertexIndices
			|| bShowVertexNormals;
	}
	// Update component's ability to tick 
	SetComponentTickEnabled(bIsEnabled);
	UE_CLOG(GetOwner(), LogGeometryCollectionDebugDraw, Verbose, TEXT("Debug Draw Tick Component bIsEnabled = %d for actor %s"), bIsEnabled, *GetOwner()->GetName());
}

#if INCLUDE_CHAOS
void UGeometryCollectionDebugDrawComponent::DebugDrawChaosTick()
{
	check(GeometryCollectionComponent);
	check(GeometryCollectionDebugDrawActor);
	check(GeometryCollectionRenderLevelSetActor);

	AActor* const Actor = GetOwner();
	check(Actor);

	// Retrieve synced particle and clustering data
	const TManagedArray<int32>& RigidBodyIds = GeometryCollectionComponent->RigidBodyIds;
	const FGeometryCollectionPhysicsProxy* const PhysicsProxy = GeometryCollectionComponent->GetPhysicsProxy();
	if (PhysicsProxy)
	{
		ParticlesData.Sync(PhysicsProxy->GetSolver(), RigidBodyIds);
	}

	// Visualize single rigid body
	const bool bIsSelected = (SelectedTransformIndex != INDEX_NONE);
	if (bIsSelected)
	{
		// Visualize the level set collision volume when synced data are available and set to the correct type
		if (GeometryCollectionDebugDrawActor->bShowRigidBodyCollision &&
			ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::GeometryType) &&
			ParticlesData.GetGeometryType(SelectedTransformIndex) == Chaos::ImplicitObjectType::LevelSet)
		{
			// Get the transform for the current piece
			FTransform Transform = FTransform::Identity;

			// Update the transform if we are rendering the level set aligned with the simulated geometry
			bool bSynced;
			bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::X);
			bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::R) && bSynced;
			bSynced = ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::ChildToParentMap) && bSynced;

			if (!GeometryCollectionDebugDrawActor->bCollisionAtOrigin && bSynced)
			{
				// Retrieve particle transform
				Transform = AGeometryCollectionDebugDrawActor::GetParticleTransform(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData);
			}

			// If the level set index has changed at run time, then reload the volume
			// because someone wants to visualize another piece
			const bool bLevelSetTextureDirty = (RenderLevelSetOwner != this || LastRenderedId == INDEX_NONE || LastRenderedId != SelectedRigidBodyId);
			if (!bLevelSetTextureDirty)
			{
				// If we are only updating the transform, or also loading the volume
				GeometryCollectionRenderLevelSetActor->SyncLevelSetTransform(Transform);
			}
			else if (ParticlesData.RequestSyncedData(EGeometryCollectionParticlesData::Geometry))
			{
				// Retrieve level set pointer from sync
				const Chaos::TLevelSet<float, 3>* LevelSet = static_cast<const Chaos::TLevelSet<float, 3>*>(ParticlesData.GetGeometry(SelectedTransformIndex));

				// Build the volume texture
				// @note: we only want to do this once, so we have a state variable on the component to ensure that
				const bool Success = GeometryCollectionRenderLevelSetActor->SetLevelSetToRender(*LevelSet, Transform);
				if (!Success)
				{
					UE_LOG(LogGeometryCollectionDebugDraw, Warning, TEXT("Levelset generation failed: %s"), *GetFullName());
					LastRenderedId = INDEX_NONE;
				}
				else
				{
					// Take ownership
					RenderLevelSetOwner = this;
					// Turn on the volume rendering
					GeometryCollectionRenderLevelSetActor->SetEnabled(true);
					// Update last rendered index for next dirty test
					LastRenderedId = SelectedRigidBodyId;
				}
			}
			else {}  // Wait a game tick for synced data
		}
	}

	// Visualize other rigid body debug draw informations
	const bool bIsOneSelected = bIsSelected && !GeometryCollectionDebugDrawActor->bDebugDrawWholeCollection;
	const bool bAreAllSelected = (bIsSelected && GeometryCollectionDebugDrawActor->bDebugDrawWholeCollection) ||
		(GeometryCollectionDebugDrawActor->SelectedRigidBody.Id == INDEX_NONE && 
		 GeometryCollectionDebugDrawActor->SelectedRigidBody.Solver == GeometryCollectionComponent->ChaosSolverActor);

	if (!bShowRigidBodyIds && GeometryCollectionDebugDrawActor->bShowRigidBodyId && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyId(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyIds, RigidBodyIdColor);
	}
	else if (bShowRigidBodyIds || (GeometryCollectionDebugDrawActor->bShowRigidBodyId && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesId(GeometryCollectionComponent, ParticlesData, RigidBodyIds, RigidBodyIdColor);
	}

	if (!bShowRigidBodyTransforms && GeometryCollectionDebugDrawActor->bShowRigidBodyTransform && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyTransform(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyTransformScale);
	}
	else if (bShowRigidBodyTransforms || (GeometryCollectionDebugDrawActor->bShowRigidBodyTransform && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesTransform(GeometryCollectionComponent, ParticlesData, RigidBodyTransformScale);
	}

	const bool bIsShowingLevelSet = (RenderLevelSetOwner == this && LastRenderedId == SelectedRigidBodyId);  // Only draw single collision whenever there isn't a level set being already rendered
	if (!bIsShowingLevelSet && !bShowRigidBodyCollisions && GeometryCollectionDebugDrawActor->bShowRigidBodyCollision && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyCollision(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyCollisionColor);
	}
	else if (bShowRigidBodyCollisions || (GeometryCollectionDebugDrawActor->bShowRigidBodyCollision && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesCollision(GeometryCollectionComponent, ParticlesData, RigidBodyCollisionColor);
	}

	if (!bShowRigidBodyInertias && GeometryCollectionDebugDrawActor->bShowRigidBodyInertia && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyInertia(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyInertiaColor);
	}
	else if (bShowRigidBodyInertias || (GeometryCollectionDebugDrawActor->bShowRigidBodyInertia && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesInertia(GeometryCollectionComponent, ParticlesData, RigidBodyInertiaColor);
	}

	if (!bShowRigidBodyVelocities && GeometryCollectionDebugDrawActor->bShowRigidBodyVelocity && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyVelocity(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyVelocityColor);
	}
	else if (bShowRigidBodyVelocities || (GeometryCollectionDebugDrawActor->bShowRigidBodyVelocity && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesVelocity(GeometryCollectionComponent, ParticlesData, RigidBodyVelocityColor);
	}

	if (!bShowRigidBodyForces && GeometryCollectionDebugDrawActor->bShowRigidBodyForce && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyForce(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyForceColor);
	}
	else if (bShowRigidBodyForces || (GeometryCollectionDebugDrawActor->bShowRigidBodyForce && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesForce(GeometryCollectionComponent, ParticlesData, RigidBodyForceColor);
	}

	if (!bShowRigidBodyInfos && GeometryCollectionDebugDrawActor->bShowRigidBodyInfos && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodyInfo(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyInfoColor);
	}
	else if (bShowRigidBodyInfos || (GeometryCollectionDebugDrawActor->bShowRigidBodyInfos && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawRigidBodiesInfo(GeometryCollectionComponent, ParticlesData, RigidBodyInfoColor);
	}

	if (!bShowConnectivityEdges && GeometryCollectionDebugDrawActor->bShowConnectivityEdges && bIsOneSelected)
	{
		GeometryCollectionDebugDrawActor->DrawConnectivityEdges(GeometryCollectionComponent, SelectedTransformIndex, ParticlesData, RigidBodyIds, ConnectivityEdgeThickness);
	}
	else if (bShowConnectivityEdges || (GeometryCollectionDebugDrawActor->bShowConnectivityEdges && bAreAllSelected))
	{
		GeometryCollectionDebugDrawActor->DrawConnectivityEdges(GeometryCollectionComponent, ParticlesData, RigidBodyIds, ConnectivityEdgeThickness);
	}
}

void UGeometryCollectionDebugDrawComponent::UpdateLevelSetVisibility()
{
	const bool bIsSelected = (SelectedTransformIndex != INDEX_NONE);
	const bool bShowCollision = bShowRigidBodyCollisions || 
		(bIsSelected && GeometryCollectionDebugDrawActor && GeometryCollectionDebugDrawActor->bShowRigidBodyCollision);
	
	if (RenderLevelSetOwner == this && (LastRenderedId == INDEX_NONE || !bShowCollision))
	{
		// Disable rendering
		GeometryCollectionRenderLevelSetActor->SetEnabled(false);

		// Disown renderer
		RenderLevelSetOwner = nullptr;
		LastRenderedId = INDEX_NONE;
	}
}
#endif  // #if INCLUDE_CHAOS
#endif  // #if GEOMETRYCOLLECTION_DEBUG_DRAW
