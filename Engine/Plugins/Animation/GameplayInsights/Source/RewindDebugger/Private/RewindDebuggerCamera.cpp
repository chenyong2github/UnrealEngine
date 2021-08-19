// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerCamera.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "IGameplayProvider.h"
#include "Insights/IUnrealInsightsModule.h"
#include "IRewindDebugger.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "SLevelViewport.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "TraceServices/Model/Frames.h"

#define LOCTEXT_NAMESPACE "RewindDebuggerCamera"

FRewindDebuggerCamera::FRewindDebuggerCamera()
	: LastPositionValid(false)
{
}

void FRewindDebuggerCamera::Initialize()
{
	UToolMenu* Menu = UToolMenus::Get()->FindMenu("RewindDebugger.MainMenu");

	Menu->AddSection("Camera Mode", LOCTEXT("Camera Mode","Camera Mode"));
	Menu->AddMenuEntry("Camera Mode",
			FToolMenuEntry::InitMenuEntry("CameraModeDisabled",
				LOCTEXT("Camera Mode Dosan;ed", "Disabled"),
				FText(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FRewindDebuggerCamera::SetCameraMode, ECameraMode::Disabled),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([this] { return Mode == ECameraMode::Disabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )),
				EUserInterfaceActionType::Check
				)
			);

	Menu->AddMenuEntry("Camera Mode",
			FToolMenuEntry::InitMenuEntry("CameraModeFollow",
				LOCTEXT("Camera Mode Follow", "Follow Target Actor"),
				FText(),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateRaw(this, &FRewindDebuggerCamera::SetCameraMode, ECameraMode::FollowTargetActor),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([this] { return Mode == ECameraMode::FollowTargetActor ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )),
				EUserInterfaceActionType::Check
				)
			);

	Menu->AddMenuEntry("Camera Mode",
			FToolMenuEntry::InitMenuEntry("CameraModeReplay",
				LOCTEXT("Camera Mode Recorded", "Replay Recorded Camera"),
				FText(),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateRaw(this, &FRewindDebuggerCamera::SetCameraMode, ECameraMode::Replay),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([this] { return Mode == ECameraMode::Replay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )),
				EUserInterfaceActionType::Check
				)
			);
}

void FRewindDebuggerCamera::SetCameraMode(ECameraMode InMode)
{
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	SLevelViewport* LevelViewport = LevelEditor.GetFirstActiveLevelViewport().Get();
	FLevelEditorViewportClient& LevelViewportClient = LevelViewport->GetLevelViewportClient();

	if (Mode == ECameraMode::Replay && InMode != ECameraMode::Replay)
	{
		LevelViewportClient.SetActorLock(nullptr);
	}
	else if (Mode == ECameraMode::Replay)
	{
		if (CameraActor.IsValid())
		{
			LevelViewportClient.SetActorLock(CameraActor.Get());
		}
	}

	Mode = InMode;
}

void FRewindDebuggerCamera::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
	if (RewindDebugger->IsPIESimulating() || RewindDebugger->GetRecordingDuration() == 0.0)
	{
		return;
	}

	if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		double CurrentTraceTime = RewindDebugger->CurrentTraceTime();
		
		static double LastCameraScrubTime = 0.0f;
		if (CurrentTraceTime != LastCameraScrubTime)
		{
			FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
			SLevelViewport* LevelViewport = LevelEditor.GetFirstActiveLevelViewport().Get();
			FLevelEditorViewportClient& LevelViewportClient = LevelViewport->GetLevelViewportClient();

			FVector TargetActorPosition;
			bool bTargetActorPositionValid = RewindDebugger->GetTargetActorPosition(TargetActorPosition);

			// only update camera in playback or scrubbing when the time has changed (allow free movement when paused)
			LastCameraScrubTime = CurrentTraceTime;

			if (Mode == ECameraMode::FollowTargetActor)
			{
				// Follow Actor mode: apply position changes from the target actor to the camera
				if (bTargetActorPositionValid)
				{
					if(LastPositionValid)
					{
						LevelViewportClient.SetViewLocation(LevelViewportClient.GetViewLocation() + TargetActorPosition - LastPosition);
					}
				}
			}

			// always update the camera actor to the replay values even if it isn't locked
			if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
			{
				GameplayProvider->ReadViewTimeline([this, RewindDebugger, CurrentTraceTime, Session](const IGameplayProvider::ViewTimeline& TimelineData)
				{
					const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
					TraceServices::FFrame Frame;
					if(FrameProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, CurrentTraceTime, Frame))
					{
						TimelineData.EnumerateEvents(Frame.StartTime, Frame.EndTime,
							[this, RewindDebugger](double InStartTime, double InEndTime, uint32 InDepth, const FViewMessage& ViewMessage)
							{
								if (!CameraActor.IsValid())
								{
									FActorSpawnParameters SpawnParameters;
									SpawnParameters.ObjectFlags |= RF_Transient;
									CameraActor = RewindDebugger->GetWorldToVisualize()->SpawnActor<ACameraActor>(ViewMessage.Position, ViewMessage.Rotation, SpawnParameters);
									UCameraComponent* Camera = CameraActor->GetCameraComponent();
									CameraActor->SetActorLabel("RewindDebuggerCamera"); 
								}

								UCameraComponent* Camera = CameraActor->GetCameraComponent();
								Camera->SetWorldLocationAndRotation(ViewMessage.Position, ViewMessage.Rotation);
								Camera->SetFieldOfView(ViewMessage.Fov);
								Camera->SetAspectRatio(ViewMessage.AspectRatio);

								return TraceServices::EEventEnumerate::Stop;
							});
					}
				});
			}

			if (Mode == ECameraMode::Replay)
			{
				if (CameraActor.IsValid())
				{
					LevelViewportClient.SetActorLock(CameraActor.Get());
				}
			}

			LastPosition = TargetActorPosition;
			LastPositionValid = bTargetActorPositionValid;
		}
	}
}

#undef LOCTEXT_NAMESPACE