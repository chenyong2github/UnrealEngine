// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaSelectionComponent.h"
#include "Materials/Material.h"

IMPLEMENT_HIT_PROXY(HPersonaSelectionHitProxy, HHitProxy)

const FPersonaSelectionCapsule& HPersonaSelectionHitProxy::GetCapsule() const
{
	return Component->operator[](CapsuleIndex);
}

void HPersonaSelectionHitProxy::BroadcastClicked() const
{
	Component->OnClicked().ExecuteIfBound(Component, CapsuleIndex, GetCapsule());
}

////////////////////////////////////////////////////////////////////////////////////////

UPersonaSelectionComponent::UPersonaSelectionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bVisibleInMainPass(false)
{
	PrimaryComponentTick.TickGroup = TG_LastDemotable;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bTickInEditor = true;
	bAutoActivate = true;
}

void UPersonaSelectionComponent::Reset()
{
	Capsules.Reset();
	CapsuleIndices.Reset();
	MarkCapsulesChanged();
}

void UPersonaSelectionComponent::SetNum(int32 InCount)
{
	Capsules.SetNum(InCount);
	SetCapsulesIndicesToFullArray();
	MarkCapsulesChanged();
}

int32 UPersonaSelectionComponent::Add(int32 InCount)
{
	int32 FirstIndex = INDEX_NONE;
	for(int32 Index=0;Index<InCount;Index++)
	{
		const int32 CapsuleIndex = Capsules.Add(FPersonaSelectionCapsule());
		if(FirstIndex == INDEX_NONE)
		{
			FirstIndex = CapsuleIndex;
		}
		CapsuleIndices.Add(CapsuleIndex);
	}
	return FirstIndex;
}

void UPersonaSelectionComponent::SetCapsulesIndicesToFullArray()
{
	CapsuleIndices.SetNum(Capsules.Num());
	for(int32 Index=0;Index<CapsuleIndices.Num();Index++)
	{
		CapsuleIndices[Index] = Index;
	}
}

void UPersonaSelectionComponent::MarkCapsulesChanged()
{
	while(HitProxies.Num() > Capsules.Num())
	{
		HitProxies.Pop();
	}
	while(HitProxies.Num() < Capsules.Num())
	{
		HitProxies.Add(new HPersonaSelectionHitProxy(HitProxies.Num(), this));
	}
	
	UpdateBounds();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UPersonaSelectionComponent::CreateSceneProxy()
{
	return new FPersonaSelectionComponentProxy(this);
}

FBoxSphereBounds UPersonaSelectionComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BBox(ForceInit);

	if(CapsuleIndices.Num() > 0)
	{
		const FTransform ComponentTransform = GetComponentToWorld();
		
		for(int32 CapsuleIndex : CapsuleIndices)
		{
			const FPersonaSelectionCapsule& Capsule = Capsules[CapsuleIndex];
			const FTransform& Transform = Capsule.Transform * ComponentTransform;
			BBox += Transform.TransformPosition(FVector(0.f, 0.f, Capsule.HalfHeight + Capsule.Radius));
			BBox += Transform.TransformPosition(-FVector(0.f, 0.f, Capsule.HalfHeight + Capsule.Radius));
		}
	}

	if (!BBox.IsValid)
	{
		const FVector BoxExtent(1.f);
		return FBoxSphereBounds(LocalToWorld.GetLocation(), BoxExtent, 1.f);
	}

	// Points are in world space, so no need to transform.
	return FBoxSphereBounds(BBox);
}

void UPersonaSelectionComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
	bool bGetDebugMaterials) const
{
	OutMaterials.Add(GEngine->DefaultFlattenMaterial);
}

void UPersonaSelectionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if(Capsules.Num() > 0 && CapsuleIndices.Num() > 0)
	{
		if(OnPersonaSelectionUpdateCapsules.IsBound())
		{
			OnPersonaSelectionUpdateCapsules.Execute(this, CapsuleIndices, Capsules);
			MarkCapsulesChanged();
		}
	}
}

void UPersonaSelectionComponent::ComputeCapsuleFromBonePositions(
	const FVector& InWorldStart,
	const FVector& InWorldEnd,
	float InBounds,
	float InRadiusMultiplier,
	FPersonaSelectionCapsule& OutCapsule
	)
{
	static const float MaxDrawRadius = InBounds * 0.02f;
	const FVector Direction = InWorldEnd - InWorldStart;
	const float BoneLength = Direction.Size();
	OutCapsule.Radius = FMath::Clamp(BoneLength * 0.05f, 0.1f, MaxDrawRadius) * InRadiusMultiplier;
	OutCapsule.HalfHeight = BoneLength * 0.5f;

	FVector Location = (InWorldStart + InWorldEnd) * 0.5f;
	FQuat AlignmentQuat = FQuat::Identity;
	if(!Direction.IsNearlyZero())
	{
		Location -= Direction.GetSafeNormal() * OutCapsule.Radius;
		AlignmentQuat = FQuat::FindBetween(FVector(0.f, 0.f, 1.f), Direction);
	}

	OutCapsule.Transform.SetLocation(Location);
	OutCapsule.Transform.SetRotation(AlignmentQuat);
	OutCapsule.Transform.SetScale3D(FVector::OneVector);
}

////////////////////////////////////////////////////////////////////////////////////////

FPersonaSelectionComponentProxy::FPersonaSelectionComponentProxy(const UPersonaSelectionComponent* InComponent)
: FPrimitiveSceneProxy(InComponent)
, SelectionComponentPtr(InComponent)
, bVisibleInMainPass(false)
{
	bWillEverBeLit = false;

	if(InComponent)
	{
		bVisibleInMainPass = InComponent->bVisibleInMainPass;
	}
}

SIZE_T FPersonaSelectionComponentProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FPersonaSelectionComponentProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if(!SelectionComponentPtr.IsValid())
	{
		return;
	}

	const UPersonaSelectionComponent* SelectionComponent = SelectionComponentPtr.Get();
	if(SelectionComponent->Capsules.IsEmpty())
	{
		return;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

			FMaterialRenderProxy* MaterialRenderProxy = GEngine->DefaultFlattenMaterial->GetRenderProxy();
			const FTransform ComponentToWorld = SelectionComponent->GetComponentToWorld();

			for(int32 CapsuleIndex : SelectionComponent->CapsuleIndices)
            {
				const FPersonaSelectionCapsule& Capsule = SelectionComponent->Capsules[CapsuleIndex];
				const FLinearColor LineColor = FLinearColor::Yellow;
				const FTransform& Transform = Capsule.Transform * ComponentToWorld;

				if(!SelectionComponent->HitProxies.IsValidIndex(CapsuleIndex))
				{
					break;
				}
				
				HPersonaSelectionHitProxy* HitProxy = SelectionComponent->HitProxies[CapsuleIndex].GetReference();

				GetBoxMesh(
					Transform.ToMatrixWithScale(),
					FVector(Capsule.Radius,
					Capsule.Radius,
					Capsule.HalfHeight),
					MaterialRenderProxy,
					SDPG_Foreground,
					ViewIndex,
					Collector,
					HitProxy
				);
			}
		}
	}
}

/**
*  Returns a struct that describes to the renderer when to draw this proxy.
*	@param		Scene view to use to determine our relevence.
*  @return		View relevance struct
*/
FPrimitiveViewRelevance FPersonaSelectionComponentProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance ViewRelevance;
	ViewRelevance.bDrawRelevance = IsShown(View);
	ViewRelevance.bOpaque = !bVisibleInMainPass;
	ViewRelevance.bDynamicRelevance = true;
	ViewRelevance.bShadowRelevance = false;
	ViewRelevance.bEditorPrimitiveRelevance = !bVisibleInMainPass;
	ViewRelevance.bSeparateTranslucency = ViewRelevance.bNormalTranslucency = true;
	return ViewRelevance;
}

uint32 FPersonaSelectionComponentProxy::GetMemoryFootprint(void) const
{
	return(sizeof(*this) + GetAllocatedSize());
}

uint32 FPersonaSelectionComponentProxy::GetAllocatedSize(void) const
{
	return FPrimitiveSceneProxy::GetAllocatedSize();
}
