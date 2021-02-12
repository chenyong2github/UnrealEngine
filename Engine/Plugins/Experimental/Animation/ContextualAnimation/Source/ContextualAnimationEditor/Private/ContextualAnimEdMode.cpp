// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEdMode.h"
#include "ContextualAnimEdModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "ContextualAnimEdModeSettings.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "ContextualAnimComponent.h"

const FEditorModeID FContextualAnimEdMode::EM_ContextualAnimEdModeId = TEXT("EM_ContextualAnimEdMode");

FContextualAnimEdMode::FContextualAnimEdMode()
{
}

FContextualAnimEdMode::~FContextualAnimEdMode()
{
}

void FContextualAnimEdMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FContextualAnimEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FContextualAnimEdMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

TSharedPtr<FContextualAnimEdModeToolkit> FContextualAnimEdMode::GetContextualAnimEdModeToolkit()
{
	return StaticCastSharedPtr<FContextualAnimEdModeToolkit>(Toolkit);
}

void FContextualAnimEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if(TestCharacter.IsValid() && GEditor->IsSimulatingInEditor())
	{
		if (ViewportClient->Viewport->KeyState(EKeys::W) || ViewportClient->Viewport->KeyState(EKeys::S))
		{
			const FVector WorldDirection = FRotationMatrix(TestCharacter->GetActorRotation()).GetScaledAxis(EAxis::X);
			TestCharacter->AddMovementInput(WorldDirection, ViewportClient->Viewport->KeyState(EKeys::W) ? 1.f : -1.f);
		}

		if (ViewportClient->Viewport->KeyState(EKeys::A) || ViewportClient->Viewport->KeyState(EKeys::D))
		{
			const FVector WorldDirection = FRotationMatrix(TestCharacter->GetActorRotation()).GetScaledAxis(EAxis::Y);
			TestCharacter->AddMovementInput(WorldDirection, ViewportClient->Viewport->KeyState(EKeys::D) ? 1.f : -1.f);
		}
	}
}

bool FContextualAnimEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if(Key == EKeys::Enter && Event == IE_Released && TestCharacter.IsValid() && GEditor->IsSimulatingInEditor())
	{
		UContextualAnimComponent* ContextualAnimComp = nullptr;

		TArray<UPrimitiveComponent*> OverlappingComps;
		TestCharacter->GetOverlappingComponents(OverlappingComps);

		for (UPrimitiveComponent* Comp : OverlappingComps)
		{
			if (Comp && Comp->GetClass()->IsChildOf<UContextualAnimComponent>())
			{
				ContextualAnimComp = Cast<UContextualAnimComponent>(Comp);
				break;
			}
		}

		if(ContextualAnimComp == nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING: The preview actor is not overlapping any interactable")));
			return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
		}

		// Check if the preview actor is already playing a contextual animation
		if (!ContextualAnimComp->IsActorPlayingContextualAnimation(TestCharacter.Get()))
		{
			// Try to find best entry point
			FContextualAnimQueryResult Result;
			const bool bQueryResult = ContextualAnimComp->QueryData(FContextualAnimQueryParams(TestCharacter.Get(), true, true), Result);

			// Early out of no valid entry point is found
			if(bQueryResult == false)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString::Printf(TEXT("WARNING: The preview actor is overlapping an interactable (%s) but there is no valid entry point for his current position"),
					*GetNameSafe(ContextualAnimComp->GetOwner())));
				return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
			}

			// QueryData does not load the animation and TryStartContextualAnimation expects the animation to be loaded
			// @TODO: We may want to provide functions to query the data and load the animation asynchronous
			Result.Animation.LoadSynchronous();

			// Attempt to start the interaction
			ContextualAnimComp->TryStartContextualAnimation(TestCharacter.Get(), Result);
		}
		else
		{
			//@TODO: For now we don't care where in the animation the preview actor is
			ContextualAnimComp->TryEndContextualAnimation(TestCharacter.Get());
		}

		return true;
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FContextualAnimEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!Click.IsControlDown())
	{
		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}

	if (!GEditor->IsSimulatingInEditor())
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING. You are not in Simulating Mode")));
		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(InViewportClient->Viewport, InViewportClient->GetScene(), InViewportClient->EngineShowFlags).SetRealtimeUpdate(InViewportClient->IsRealtime()));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation Cursor(View, InViewportClient, Click.GetClickPos().X, Click.GetClickPos().Y);
	const auto ViewportType = InViewportClient->GetViewportType();

	const FVector RayStart = Cursor.GetOrigin();
	const FVector RayEnd = RayStart + Cursor.GetDirection() * HALF_WORLD_MAX;

	FHitResult HitResult;
	InViewportClient->GetWorld()->LineTraceSingleByChannel(HitResult, RayStart, RayEnd, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam);

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::White, FString::Printf(TEXT("INFO: HandleClick: IsValidHit: %d Actor: %s Location: %s"),
		HitResult.IsValidBlockingHit(), *GetNameSafe(HitResult.GetActor()), *HitResult.ImpactPoint.ToString()));

	if (!HitResult.IsValidBlockingHit())
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING. Can't spawn preview actor. Reason: HitResult from click event is not a valid blocking hit.")));
		return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
	}

	if (!TestCharacter.IsValid())
	{
		TSubclassOf<ACharacter> TestActorClass = GetContextualAnimEdModeToolkit()->GetSettings()->TestCharacterClass;
		if (!TestActorClass)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("WARNING. Can't spawn preview actor. Reason: TestCharacterClass is invalid")));
			return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
		}

		TestCharacter = GetWorld()->SpawnActor<ACharacter>(TestActorClass, FTransform(HitResult.ImpactPoint));
		if (ensureAlways(TestCharacter.IsValid()))
		{
			TestCharacter->bUseControllerRotationYaw = false;
			if (UCharacterMovementComponent* CharacterMovementComp = TestCharacter->GetCharacterMovement())
			{
				CharacterMovementComp->bOrientRotationToMovement = true;
				CharacterMovementComp->bUseControllerDesiredRotation = false;
				CharacterMovementComp->RotationRate = FRotator(0.f, 540.0, 0.f);
			}

			if (!TestCharacter->AIControllerClass || !TestCharacter->AIControllerClass->IsChildOf<AAIController>())
			{
				TestCharacter->AIControllerClass = AAIController::StaticClass();
			}

			TestCharacter->SpawnDefaultController();
		}
	}
	else
	{
		if (AAIController* Controller = Cast<AAIController>(TestCharacter->GetController()))
		{
			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
			const ANavigationData* NavData = NavSys ? NavSys->GetNavDataForProps(Controller->GetNavAgentPropertiesRef(), Controller->GetNavAgentLocation()) : nullptr;

			const bool bUsePathfinding = (NavData != nullptr);
			Controller->MoveToLocation(HitResult.ImpactPoint, 10.f, true, bUsePathfinding);
		}
	}

	return true;
}

bool FContextualAnimEdMode::UsesToolkits() const
{
	return true;
}




