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

#define LOCTEXT_NAMESPACE "MeshSelection"


bool UMeshClickToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UMeshClickToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshClickTool* NewTool = NewObject<UMeshClickTool>(SceneState.ToolManager);

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
	return FInputRayHit(0.0f);
}

void UMeshClickTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		const FVector& RayOrigin = ClickPos.WorldRay.Origin;
		const FVector& RayDirection = ClickPos.WorldRay.Direction;
		FRay Ray = FRay(RayOrigin, RayDirection);
		const FVector TraceStart(RayOrigin);
		const FVector TraceEnd(RayOrigin + RayDirection * HALF_WORLD_MAX);
		MeshToolManager->ResetState();
		//Default iterator only iterates over active levels.
		const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;
		// TODO: The tool needs to know the world its acting on
		for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext(false).World(), AActor::StaticClass(), Flags); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor->IsEditable() &&
				Actor->IsListedInSceneOutliner() &&					// Only add actors that are allowed to be selected and drawn in editor
				!Actor->IsTemplate() &&								// Should never happen, but we never want CDOs
				!Actor->HasAnyFlags(RF_Transient))				// Don't add transient actors in non-play worlds
			{
				TArray<UActorComponent*> CandidateComponents = Actor->K2_GetComponentsByClass(UMeshComponent::StaticClass());
				for (UActorComponent* CandidateComponent : CandidateComponents)
				{
					UMeshComponent* MeshComponent = Cast<UMeshComponent>(CandidateComponent);
					TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = FMeshPaintComponentAdapterFactory::CreateAdapterForMesh(MeshComponent, 0);
					MeshToolManager->AddPaintableMeshComponent(MeshComponent);
					MeshToolManager->AddToComponentToAdapterMap(MeshComponent, MeshAdapter);
				}
					
			}
		}
		FHitResult TraceHitResult(1.0f);
		MeshToolManager->FindHitResult(Ray, TraceHitResult);
		MeshToolManager->ResetState();
		TArray<AActor*> SelectedActors;
		if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TraceHitResult.GetComponent()))
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = FMeshPaintComponentAdapterFactory::CreateAdapterForMesh(MeshComponent, 0);
			if (MeshComponent->IsVisible() && MeshAdapter.IsValid() && MeshAdapter->IsValid())
			{
				MeshToolManager->AddPaintableMeshComponent(MeshComponent);
				MeshToolManager->AddToComponentToAdapterMap(MeshComponent, MeshAdapter);
				SelectedActors.Add(Cast<AActor>(MeshComponent->GetOuter()));
				MeshAdapter->OnAdded();
			}
		}

		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelection", "Select Mesh"));


		FSelectedOjectsChangeList NewSelection;
		// TODO add CTRL handling
		NewSelection.ModificationType = bAddToSelectionSet ? ESelectedObjectsModificationType::Add : ESelectedObjectsModificationType::Replace;
		NewSelection.Actors.Append(SelectedActors);
		GetToolManager()->RequestSelectionChange(NewSelection);
		GetToolManager()->EndUndoTransaction();
	}
}

#undef LOCTEXT_NAMESPACE
