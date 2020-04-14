// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshSelect.h"
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


bool UVertexAdapterClickToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UVertexAdapterClickToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVertexAdapterClickTool* NewTool = NewObject<UVertexAdapterClickTool>(SceneState.ToolManager);

	return NewTool;
}

bool UTextureAdapterClickToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UTextureAdapterClickToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UTextureAdapterClickTool* NewTool = NewObject<UTextureAdapterClickTool>(SceneState.ToolManager);

	return NewTool;
}


UMeshClickTool::UMeshClickTool()
{
}

void UMeshClickTool::Setup()
{
	UInteractiveTool::Setup();

	// add default button input behaviors for devices
	USingleClickInputBehavior* MouseBehavior = NewObject<USingleClickInputBehavior>();
	MouseBehavior->Initialize(this);
	MouseBehavior->Modifiers.RegisterModifier(AdditiveSelectionModifier, FInputDeviceState::IsShiftKeyDown);
	AddInputBehavior(MouseBehavior);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartMeshSelectTool", "Select a mesh. Switch tools to paint vertex colors, blend between textures, or paint directly onto a texture file."),
		EToolMessageLevel::UserNotification);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelection", "Select Mesh"));


	FSelectedOjectsChangeList NewSelection;
	// TODO add CTRL handling
	NewSelection.ModificationType = ESelectedObjectsModificationType::Clear;
	GetToolManager()->RequestSelectionChange(NewSelection);
	GetToolManager()->EndUndoTransaction();
}

void UMeshClickTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == AdditiveSelectionModifier)
	{
		bAddToSelectionSet = bIsOn;
	}
}

FInputRayHit UMeshClickTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		if (!bAddToSelectionSet || !AllowsMultiselect())
		{
			CachedClickedComponents.Empty();
			CachedClickedActors.Empty();
		}
		return FindClickedComponentsAndCacheAdapters(ClickPos, MeshToolManager) ? FInputRayHit(0.0f) : FInputRayHit();
	}
	return FInputRayHit();
}

void UMeshClickTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		for (UMeshComponent* MeshComponent : CachedClickedComponents)
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = MeshToolManager->GetAdapterForComponent(MeshComponent);
			if (MeshComponent->IsVisible() && MeshAdapter.IsValid() && MeshAdapter->IsValid() && IsMeshAdapterSupported(MeshAdapter))
			{
				MeshToolManager->AddPaintableMeshComponent(MeshComponent);
				MeshAdapter->OnAdded();
			}

			GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelection", "Select Mesh"));


			FSelectedOjectsChangeList NewSelection;
			// TODO add CTRL handling
			NewSelection.ModificationType = bAddToSelectionSet ? ESelectedObjectsModificationType::Add : ESelectedObjectsModificationType::Replace;
			NewSelection.Actors.Append(CachedClickedActors);
			GetToolManager()->RequestSelectionChange(NewSelection);
			GetToolManager()->EndUndoTransaction();
		}
	}
}



bool UMeshClickTool::FindClickedComponentsAndCacheAdapters(const FInputDeviceRay& ClickPos, class UMeshToolManager* MeshToolManager)
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
				if (MeshAdapter.IsValid() && IsMeshAdapterSupported(MeshAdapter))
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

UVertexAdapterClickTool::UVertexAdapterClickTool()
	: UMeshClickTool()
{

}

bool UVertexAdapterClickTool::IsMeshAdapterSupported(TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter)
{
	return MeshAdapter.IsValid() ? MeshAdapter->SupportsVertexPaint() : false;
}

UTextureAdapterClickTool::UTextureAdapterClickTool()
	: UMeshClickTool()
{

}

bool UTextureAdapterClickTool::IsMeshAdapterSupported(TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter)
{
	return MeshAdapter.IsValid() ? MeshAdapter->SupportsTexturePaint() : false;
}

#undef LOCTEXT_NAMESPACE