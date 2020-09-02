// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshPaintInteractions.h"
#include "InputState.h"
#include "InteractiveToolManager.h"
#include "ToolContextInterfaces.h"
#include "MeshPaintHelpers.h"
#include "Components/MeshComponent.h"
#include "IMeshPaintComponentAdapter.h"
#include "MeshPaintAdapterFactory.h"
#include "EngineUtils.h"
#include "Editor.h"
#if WITH_EDITOR
#include "HitProxies.h"
#endif

#define LOCTEXT_NAMESPACE "MeshSelection"

FInputRayHit UMeshPaintSelectionMechanic::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	IMeshPaintSelectionInterface* Interface = Cast<IMeshPaintSelectionInterface>(GetParentTool());
	if (!bAddToSelectionSet || !Interface->AllowsMultiselect())
	{
		CachedClickedComponents.Empty();
		CachedClickedActors.Empty();
	}
	return FindClickedComponentsAndCacheAdapters(ClickPos) ? FInputRayHit(0.0f) : FInputRayHit();
}

void UMeshPaintSelectionMechanic::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (SharedMeshToolData.IsValid())
	{
		for (UMeshComponent* MeshComponent : CachedClickedComponents)
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = SharedMeshToolData->GetAdapterForComponent(MeshComponent);
			IMeshPaintSelectionInterface* Interface = Cast<IMeshPaintSelectionInterface>(GetParentTool());
			if (MeshComponent && MeshComponent->IsVisible() && MeshAdapter.IsValid() && MeshAdapter->IsValid() && Interface->IsMeshAdapterSupported(MeshAdapter))
			{
				SharedMeshToolData->AddPaintableMeshComponent(MeshComponent);
				MeshAdapter->OnAdded();
			}

			GetParentTool()->GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelection", "Select Mesh"));


			FSelectedOjectsChangeList NewSelection;
			// TODO add CTRL handling
			NewSelection.ModificationType = bAddToSelectionSet ? ESelectedObjectsModificationType::Add : ESelectedObjectsModificationType::Replace;
			NewSelection.Actors.Append(CachedClickedActors);
			GetParentTool()->GetToolManager()->RequestSelectionChange(NewSelection);
			GetParentTool()->GetToolManager()->EndUndoTransaction();
		}
	}
}

bool UMeshPaintSelectionMechanic::FindClickedComponentsAndCacheAdapters(const FInputDeviceRay& ClickPos)
{
	bool bFoundValidComponents = false;
#if WITH_EDITOR
	if (!SharedMeshToolData.IsValid())
	{
		return bFoundValidComponents;
	}

	if (HHitProxy* HitProxy = GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetHitProxy(ClickPos.ScreenPosition.X, ClickPos.ScreenPosition.Y))
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorProxy = (HActor*)HitProxy;
			AActor* Actor = ActorProxy->Actor;
			TArray<UActorComponent*> CandidateComponents = Actor->K2_GetComponentsByClass(UMeshComponent::StaticClass());
			for (UActorComponent* CandidateComponent : CandidateComponents)
			{
				UMeshComponent* MeshComponent = Cast<UMeshComponent>(CandidateComponent);
				TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = FMeshPaintComponentAdapterFactory::CreateAdapterForMesh(MeshComponent, 0);
				IMeshPaintSelectionInterface* Interface = Cast<IMeshPaintSelectionInterface>(GetParentTool());
				if (MeshAdapter.IsValid() && Interface->IsMeshAdapterSupported(MeshAdapter))
				{
					SharedMeshToolData->AddToComponentToAdapterMap(MeshComponent, MeshAdapter);
					CachedClickedComponents.AddUnique(MeshComponent);
					CachedClickedActors.AddUnique(Cast<AActor>(MeshComponent->GetOuter()));
					bFoundValidComponents = true;
				}
			}
		}
	}
#endif
	return bFoundValidComponents;
}

void UMeshPaintSelectionMechanic::SetMeshToolData(TWeakObjectPtr<UMeshToolManager> InMeshToolData)
{
	SharedMeshToolData = InMeshToolData;
}

#undef LOCTEXT_NAMESPACE