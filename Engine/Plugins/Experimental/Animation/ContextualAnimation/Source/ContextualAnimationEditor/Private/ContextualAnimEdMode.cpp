// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEdMode.h"
#include "ContextualAnimEdModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "ContextualAnimEdModeSettings.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "ContextualAnimComponent.h"
#include "MotionWarpingComponent.h"
#include "Animation/AnimMontage.h"

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
		if (!TestCharacter->IsPlayingRootMotion())
		{
			//@TODO: Cache the list of interactable actors
			float BestDistance = MAX_FLT;
			UContextualAnimComponent* ContextualAnimComp = nullptr;
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				AActor* Actor = *It;
				UContextualAnimComponent* Comp = Actor ? Actor->FindComponentByClass<UContextualAnimComponent>() : nullptr;
				if(Comp)
				{
					const float Distance = FVector::DistSquared(Actor->GetActorLocation(), TestCharacter->GetActorLocation());
					if(Distance < BestDistance)
					{
						BestDistance = Distance;
						ContextualAnimComp = Comp;
					}
				}
			}

			if (ContextualAnimComp)
			{
				FContextualAnimEntryPoint EntryPoint;
				if (ContextualAnimComp->FindBestEntryPointForActor(TestCharacter.Get(), EntryPoint))
				{
					if (UAnimMontage* Montage = EntryPoint.Animation.LoadSynchronous())
					{
						if (UMotionWarpingComponent* MotionWarpingComp = TestCharacter->FindComponentByClass<UMotionWarpingComponent>())
						{
							const FName SyncPointName = GetContextualAnimEdModeToolkit()->GetSettings()->MotionWarpSyncPointName;
							MotionWarpingComp->AddOrUpdateSyncPoint(SyncPointName, EntryPoint.SyncTransform);
							TestCharacter->PlayAnimMontage(Montage);
						}
					}
				}
				else
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString::Printf(TEXT("Found nearest interactable actor (%s) but the Test Character is not close enough to any of the entry points"), 
					*GetNameSafe(ContextualAnimComp->GetOwner())));
				}
			}
			else
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("There are no interactable actors in the level")));
			}
		}
		else
		{
			TestCharacter->StopAnimMontage();
		}

		return true;
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FContextualAnimEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (Click.IsControlDown() && GEditor->IsSimulatingInEditor())
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(InViewportClient->Viewport, InViewportClient->GetScene(), InViewportClient->EngineShowFlags).SetRealtimeUpdate(InViewportClient->IsRealtime()));
		FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
		FViewportCursorLocation Cursor(View, InViewportClient, Click.GetClickPos().X, Click.GetClickPos().Y);
		const auto ViewportType = InViewportClient->GetViewportType();
		
		const FVector RayStart = Cursor.GetOrigin();
		const FVector RayEnd = RayStart + Cursor.GetDirection() * HALF_WORLD_MAX;
				
		FHitResult HitResult;
		InViewportClient->GetWorld()->LineTraceSingleByChannel(HitResult, RayStart, RayEnd, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam);
		
		if (HitResult.IsValidBlockingHit())
		{
			if(!TestCharacter.IsValid())
			{
				TSubclassOf<ACharacter> TestActorClass = GetContextualAnimEdModeToolkit()->GetSettings()->TestCharacterClass;
				if(TestActorClass)
				{
					TestCharacter = GetWorld()->SpawnActor<ACharacter>(TestActorClass, FTransform(HitResult.ImpactPoint));
					
					if(TestCharacter.IsValid())
					{
						if (!TestCharacter->AIControllerClass || !TestCharacter->AIControllerClass->IsChildOf<AAIController>())
						{
							TestCharacter->AIControllerClass = AAIController::StaticClass();
						}

						TestCharacter->SpawnDefaultController();
					}
				}
				else
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString(TEXT("Select a valid character class for your test actor")));
				}
			}
			else
			{
				if(AAIController* Controller = Cast<AAIController>(TestCharacter->GetController()))
				{
					UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
					const ANavigationData* NavData = NavSys ? NavSys->GetNavDataForProps(Controller->GetNavAgentPropertiesRef(), Controller->GetNavAgentLocation()) : nullptr;

					const bool bUsePathfinding = (NavData != nullptr);
					Controller->MoveToLocation(HitResult.ImpactPoint, 10.f, true, bUsePathfinding);
				}
			}
		}

		return true;
	}

	return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
}

bool FContextualAnimEdMode::UsesToolkits() const
{
	return true;
}




