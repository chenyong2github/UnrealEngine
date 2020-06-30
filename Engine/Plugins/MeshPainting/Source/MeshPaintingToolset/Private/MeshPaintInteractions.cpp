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
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetParentTool()->GetToolManager()))
	{
		IMeshPaintSelectionInterface* Interface = Cast<IMeshPaintSelectionInterface>(GetParentTool());
		if (!bAddToSelectionSet || !Interface->AllowsMultiselect())
		{
			CachedClickedComponents.Empty();
			CachedClickedActors.Empty();
		}
		return FindClickedComponentsAndCacheAdapters(ClickPos, MeshToolManager) ? FInputRayHit(0.0f) : FInputRayHit();
	}
	return FInputRayHit();
}

void UMeshPaintSelectionMechanic::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetParentTool()->GetToolManager()))
	{
		for (UMeshComponent* MeshComponent : CachedClickedComponents)
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = MeshToolManager->GetAdapterForComponent(MeshComponent);
			IMeshPaintSelectionInterface* Interface = Cast<IMeshPaintSelectionInterface>(GetParentTool());
			if (MeshComponent && MeshComponent->IsVisible() && MeshAdapter.IsValid() && MeshAdapter->IsValid() && Interface->IsMeshAdapterSupported(MeshAdapter))
			{
				MeshToolManager->AddPaintableMeshComponent(MeshComponent);
				MeshAdapter->OnAdded();
			}

			MeshToolManager->BeginUndoTransaction(LOCTEXT("MeshSelection", "Select Mesh"));


			FSelectedOjectsChangeList NewSelection;
			// TODO add CTRL handling
			NewSelection.ModificationType = bAddToSelectionSet ? ESelectedObjectsModificationType::Add : ESelectedObjectsModificationType::Replace;
			NewSelection.Actors.Append(CachedClickedActors);
			MeshToolManager->RequestSelectionChange(NewSelection);
			MeshToolManager->EndUndoTransaction();
		}
	}
}

bool UMeshPaintSelectionMechanic::FindClickedComponentsAndCacheAdapters(const FInputDeviceRay& ClickPos, class UMeshToolManager* MeshToolManager)
{
	bool bFoundValidComponents = false;
#if WITH_EDITOR
	if (HHitProxy* HitProxy = MeshToolManager->GetContextQueriesAPI()->GetHitProxy(ClickPos.ScreenPosition.X, ClickPos.ScreenPosition.Y))
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
					MeshToolManager->AddToComponentToAdapterMap(MeshComponent, MeshAdapter);
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
#undef LOCTEXT_NAMESPACE