// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelViewportUtils.h"

#include "EVCamTargetViewportID.h"
#include "GameFramework/PlayerController.h"
#include "Output/VCamOutputProviderBase.h"
#include "Util/VCamViewportLocker.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#endif

namespace UE::VCamCore::LevelViewportUtils::Private
{
	namespace Locking
	{
		enum class EViewportFlags
		{
			None,
			IsNotUsed   = 1 << 0,
			ForceUse	= 1 << 1,
		};
		ENUM_CLASS_FLAGS(EViewportFlags)
		
#if WITH_EDITOR
		static void UpdateLockStateForEditor(FVCamViewportLockState& ViewportLockState, EVCamTargetViewportID ViewportID, bool bNewLockState, AActor* ActorToLockWith = nullptr)
		{
			TSharedPtr<SLevelViewport> Viewport = GetLevelViewport(ViewportID);
			FLevelEditorViewportClient* LevelViewportClient = Viewport.IsValid()
				? &Viewport->GetLevelViewportClient()
				: nullptr;
			const bool bNeedsLock = !ViewportLockState.bIsLockedToViewport && bNewLockState;
			if (LevelViewportClient)
			{
				if (bNeedsLock)
				{
					ViewportLockState.Backup_ActorLock = LevelViewportClient->GetActiveActorLock();
					LevelViewportClient->SetActorLock(ActorToLockWith);
					// If bLockedCameraView is not true then the viewport is locked to the actor's transform and not the camera component
					LevelViewportClient->bLockedCameraView = true;
					ViewportLockState.bIsLockedToViewport = true;
				}
				else if (ViewportLockState.bIsLockedToViewport && !bNewLockState
					&& ViewportLockState.Backup_ActorLock.IsValid())
				{
					LevelViewportClient->SetActorLock(ViewportLockState.Backup_ActorLock.Get());
					ViewportLockState.Backup_ActorLock = nullptr;
					// If bLockedCameraView is not true then the viewport is locked to the actor's transform and not the camera component
					LevelViewportClient->bLockedCameraView = true;
					ViewportLockState.bIsLockedToViewport = false;
				}
				else if (ViewportLockState.bIsLockedToViewport && !bNewLockState)
				{
					LevelViewportClient->SetActorLock(nullptr);
					ViewportLockState.bIsLockedToViewport = false;
				}
			}
		}
#endif

		static void UpdateLockStateForGame(FVCamViewportLockState& ViewportLockState, const FWorldContext& Context, bool bNewLockState, AActor* ActorToLockWith = nullptr)
		{
			UWorld* ActorWorld = Context.World();
			if (ActorWorld && ActorWorld->GetGameInstance())
			{
				APlayerController* PlayerController = ActorWorld->GetGameInstance()->GetFirstLocalPlayerController(ActorWorld);
				if (PlayerController)
				{
					const bool bNeedsLock = !ViewportLockState.bIsLockedToViewport && bNewLockState;
					if (bNeedsLock)
					{
						ViewportLockState.Backup_ViewTarget = PlayerController->GetViewTarget();
						PlayerController->SetViewTarget(ActorToLockWith);
						ViewportLockState.bIsLockedToViewport = true;
					}
					else if (ViewportLockState.bIsLockedToViewport && !bNewLockState
						&& ViewportLockState.Backup_ViewTarget.IsValid())
					{
						PlayerController->SetViewTarget(ViewportLockState.Backup_ViewTarget.Get());
						ViewportLockState.Backup_ViewTarget = nullptr;
						ViewportLockState.bIsLockedToViewport = false;
					}
					else if (ViewportLockState.bIsLockedToViewport && !bNewLockState)
					{
						PlayerController->SetViewTarget(nullptr);
						ViewportLockState.bIsLockedToViewport = false;
					}
				}
			}
		}
		
		static void UpdateLockState(FVCamViewportLockState& ViewportLockState, EVCamTargetViewportID ViewportID, EViewportFlags ViewportFlags, AActor* ActorToLockWith = nullptr)
		{
			const bool bForceUse = (ViewportFlags & EViewportFlags::ForceUse) != EViewportFlags::None;
			const bool bIsUsed = (ViewportFlags & EViewportFlags::IsNotUsed) == EViewportFlags::None;
			const bool bNewLockState = bForceUse || (ViewportLockState.bLockViewportToCamera && bIsUsed);

#if WITH_EDITOR
			ViewportLockState.bIsForceLocked = bForceUse;
#endif
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
#if WITH_EDITOR
				if (Context.WorldType == EWorldType::Editor)
				{
					UpdateLockStateForEditor(ViewportLockState, ViewportID, bNewLockState, ActorToLockWith);
				}
				else
#endif
				{
					UpdateLockStateForGame(ViewportLockState, Context, bNewLockState, ActorToLockWith);
				}
			}
		}
	}

	void UpdateViewportLocksFromOutputs(TArray<TObjectPtr<UVCamOutputProviderBase>> OutputProviders, FVCamViewportLocker& LockData, AActor* ActorToLockWith)
	{
		TSet<EVCamTargetViewportID> Viewports;
		TSet<EVCamTargetViewportID> ForcefullyLockedViewports;
		
		Algo::TransformIf(OutputProviders, Viewports,
			[](TObjectPtr<UVCamOutputProviderBase> Output){ return Output && Output->IsActive(); },
			[](TObjectPtr<UVCamOutputProviderBase> Output) { return Output->GetTargetViewport(); }
			);
		Algo::TransformIf(OutputProviders, ForcefullyLockedViewports,
			[](TObjectPtr<UVCamOutputProviderBase> Output){ return Output && Output->IsActive() && Output->NeedsForceLockToViewport(); },
			[](TObjectPtr<UVCamOutputProviderBase> Output) { return Output->GetTargetViewport(); }
			);
		
		check(LockData.Locks.Num() == 4);
		for (TPair<EVCamTargetViewportID, FVCamViewportLockState>& ViewportData : LockData.Locks)
		{
			const bool bIsForceLocked = ForcefullyLockedViewports.Contains(ViewportData.Key);
			const bool bIsNotUsed = !Viewports.Contains(ViewportData.Key);
			
			Locking::EViewportFlags Flags = Locking::EViewportFlags::None;
			Flags |= bIsForceLocked ? Locking::EViewportFlags::ForceUse : Locking::EViewportFlags::None;
			Flags |= bIsNotUsed ? Locking::EViewportFlags::IsNotUsed : Locking::EViewportFlags::None;
			
			Locking::UpdateLockState(ViewportData.Value, ViewportData.Key, Flags, ActorToLockWith);
		}
	}

	void UnlockAllViewports(FVCamViewportLocker& LockData)
	{
		for (TPair<EVCamTargetViewportID, FVCamViewportLockState>& ViewportData : LockData.Locks)
		{
			Locking::UpdateLockState(ViewportData.Value, ViewportData.Key, Locking::EViewportFlags::IsNotUsed);
		}
	}

#if WITH_EDITOR
	TSharedPtr<SLevelViewport> GetLevelViewport(EVCamTargetViewportID TargetViewport)
	{
		TSharedPtr<SLevelViewport> OutLevelViewport = nullptr;

		if (TargetViewport == EVCamTargetViewportID::CurrentlySelected)
		{
			FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
			return LevelEditorModule
				? LevelEditorModule->GetFirstActiveLevelViewport()
				: nullptr;;
		}

		if (!GEditor)
		{
			return nullptr;
		}
	
		for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			// We only care about the fully rendered 3D viewport...seems like there should be a better way to check for this
			if (Client->IsOrtho())
			{
				continue;
			}

			TSharedPtr<SLevelViewport> LevelViewport = StaticCastSharedPtr<SLevelViewport>(Client->GetEditorViewportWidget());
			if (!LevelViewport.IsValid())
			{
				continue;
			}
		
			const FString WantedViewportString = GetConfigKeyFor(TargetViewport);
			const FString ViewportConfigKey = LevelViewport->GetConfigKey().ToString();
			if (ViewportConfigKey.Contains(*WantedViewportString, ESearchCase::CaseSensitive, ESearchDir::FromStart))
			{
				return LevelViewport;
			}
		}

		return OutLevelViewport;
	}
	
	FString GetConfigKeyFor(EVCamTargetViewportID TargetViewport)
	{
		return TargetViewport == EVCamTargetViewportID::CurrentlySelected
			? FName(EName::None).ToString()
			: FString::Printf(TEXT("Viewport %d.Viewport"), (int32)TargetViewport);
	}
#endif
}
