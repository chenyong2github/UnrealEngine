// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatineeToLevelSequenceConverter.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineAnalytics.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Matinee/InterpData.h"
#include "Matinee/InterpGroup.h"
#include "Matinee/InterpGroupDirector.h"
#include "Matinee/InterpGroupInst.h"
#include "Matinee/InterpTrackAudioMaster.h"
#include "Matinee/InterpTrackColorScale.h"
#include "Matinee/InterpTrackDirector.h"
#include "Matinee/InterpTrackFade.h"
#include "Matinee/InterpTrackSlomo.h"
#include "Matinee/MatineeActor.h"
#include "MatineeConverter.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneSlomoTrack.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MatineeToLevelSequenceConverter"

FMatineeToLevelSequenceConverter::FMatineeToLevelSequenceConverter(const FMatineeConverter* InMatineeConverter)
	: MatineeConverter(InMatineeConverter)
{
}

void FMatineeToLevelSequenceConverter::ConvertMatineeToLevelSequence(TArray<TWeakObjectPtr<AActor> > ActorsToConvert)
{
	// Keep track of how many people actually used the tool to convert assets over.
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Matinee.ConversionTool.MatineeActorConverted"));
	}

	TArray<TWeakObjectPtr<ALevelSequenceActor> > NewActors;

	int32 NumWarnings = 0;
	for (TWeakObjectPtr<AActor> Actor : ActorsToConvert)
	{
		TWeakObjectPtr<ALevelSequenceActor> NewActor = ConvertSingleMatineeToLevelSequence(Actor, NumWarnings);
		if (NewActor.IsValid())
		{
			NewActors.Add(NewActor);
		}
	}

	// Select the newly created level sequence actors
	const bool bNotifySelectionChanged = true;
	const bool bDeselectBSP = true;
	const bool bWarnAboutTooManyActors = false;
	const bool bSelectEvenIfHidden = false;

	GEditor->GetSelectedActors()->Modify();
	GEditor->GetSelectedActors()->BeginBatchSelectOperation();

	GEditor->SelectNone( bNotifySelectionChanged, bDeselectBSP, bWarnAboutTooManyActors );

	for (TWeakObjectPtr<AActor> NewActor : NewActors )
	{
		GEditor->SelectActor(NewActor.Get(), true, bNotifySelectionChanged, bSelectEvenIfHidden );
	}

	GEditor->GetSelectedActors()->EndBatchSelectOperation();
	GEditor->NoteSelectionChange();

	// Edit the first asset
	if (NewActors.Num())
	{
		UObject* NewAsset = NewActors[0]->LevelSequence.TryLoad();
		if (NewAsset)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
		}

		FText NotificationText;
		if (NewActors.Num() == 1)
		{
			NotificationText = FText::Format(LOCTEXT("MatineeToLevelSequence_SingularResult", "Conversion to {0} complete with {1} warnings"), FText::FromString(NewActors[0]->GetActorLabel()), FText::AsNumber(NumWarnings));
		}
		else
		{
			NotificationText = FText::Format(LOCTEXT("MatineeToLevelSequence_Result", "Converted {0} with {1} warnings"), FText::AsNumber(NewActors.Num()), FText::AsNumber(NumWarnings));
		}

		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.f;
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); });
		NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
}

/** Convert a single matinee to a level sequence asset */
TWeakObjectPtr<ALevelSequenceActor> FMatineeToLevelSequenceConverter::ConvertSingleMatineeToLevelSequence(TWeakObjectPtr<AActor> ActorToConvert, int32& NumWarnings)
{
	using namespace UE::MovieScene;

	UObject* AssetOuter = ActorToConvert->GetOuter();
	UPackage* AssetPackage = AssetOuter->GetOutermost();

	FString NewLevelSequenceAssetName = ActorToConvert->GetActorLabel() + FString("LevelSequence");
	FString NewLevelSequenceAssetPath = AssetPackage->GetName();
	int LastSlashPos = NewLevelSequenceAssetPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	NewLevelSequenceAssetPath = NewLevelSequenceAssetPath.Left(LastSlashPos);

	// Create a new level sequence asset with the appropriate name
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UObject* NewAsset = nullptr;
	UFactory* Factory = FindFactoryForClass(ULevelSequence::StaticClass());
	if (Factory != nullptr)
	{
		NewAsset = AssetTools.CreateAssetWithDialog(NewLevelSequenceAssetName, NewLevelSequenceAssetPath, ULevelSequence::StaticClass(), Factory);
	}
	if (!NewAsset)
	{
		return nullptr;
	}

	UMovieSceneSequence* NewSequence = Cast<UMovieSceneSequence>(NewAsset);
	UMovieScene* NewMovieScene = NewSequence->GetMovieScene();

	// Add a level sequence actor for this new sequence
	UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(ALevelSequenceActor::StaticClass());
	if (!ensure(ActorFactory))
	{
		return nullptr;
	}

	ALevelSequenceActor* NewActor = CastChecked<ALevelSequenceActor>(GEditor->UseActorFactory(ActorFactory, FAssetData(NewAsset), &FTransform::Identity));

	struct FTemporaryPlayer : IMovieScenePlayer
	{
		FTemporaryPlayer(UMovieSceneSequence& InSequence, UObject* InContext)
			: Context(InContext)
		{
			RootInstance.Initialize(InSequence, *this, nullptr);
		}

		virtual FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() { return RootInstance; }
		virtual void UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams) {}
		virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) {}
		virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const {}
		virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const { return EMovieScenePlayerStatus::Stopped; }
		virtual void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) {}
		virtual UObject* GetPlaybackContext() const { return Context; }
		FMovieSceneRootEvaluationTemplateInstance RootInstance;
		UObject* Context;

	} TemporaryPlayer(*NewSequence, NewActor->GetWorld());

	// Walk through all the interp group data and create corresponding tracks on the new level sequence asset
	if (ActorToConvert->IsA(AMatineeActor::StaticClass()))
	{
		AMatineeActor* MatineeActor = StaticCast<AMatineeActor*>(ActorToConvert.Get());
		MatineeActor->InitInterp();

		// Set the length
		const int32 LengthInFrames = (MatineeActor->MatineeData->InterpLength * NewMovieScene->GetTickResolution()).FrameNumber.Value;
		NewMovieScene->SetPlaybackRange(FFrameNumber(0), LengthInFrames + 1);

		// Convert the groups
		for (int32 i=0; i<MatineeActor->GroupInst.Num(); ++i)
		{
			UInterpGroupInst* GrInst = MatineeActor->GroupInst[i];
			UInterpGroup* Group = GrInst->Group;
			AActor* GroupActor = GrInst->GetGroupActor();
			MatineeConverter->ConvertInterpGroup(Group, GroupActor, NewSequence, NewMovieScene, NumWarnings);
		}

		// Force an evaluation so that bound objects will 
		FMovieSceneContext Context = FMovieSceneContext(FMovieSceneEvaluationRange(FFrameTime(), FFrameRate()), EMovieScenePlayerStatus::Jumping);
		TemporaryPlayer.GetEvaluationTemplate().Evaluate(Context, TemporaryPlayer);

		// Director group - convert this after the regular groups to ensure that the camera cut bindings are there
		UInterpGroupDirector* DirGroup = MatineeActor->MatineeData->FindDirectorGroup();
		if (DirGroup)
		{
			UInterpTrackDirector* MatineeDirectorTrack = DirGroup->GetDirectorTrack();
			if (MatineeDirectorTrack && MatineeDirectorTrack->GetNumKeyframes() != 0)
			{
				UMovieSceneCameraCutTrack* CameraCutTrack = NewMovieScene->AddMasterTrack<UMovieSceneCameraCutTrack>();
				FMatineeImportTools::CopyInterpDirectorTrack(MatineeDirectorTrack, CameraCutTrack, MatineeActor, TemporaryPlayer);
			}

			UInterpTrackFade* MatineeFadeTrack = DirGroup->GetFadeTrack();
			if (MatineeFadeTrack && MatineeFadeTrack->GetNumKeyframes() != 0)
			{						
				UMovieSceneFadeTrack* FadeTrack = NewMovieScene->AddMasterTrack<UMovieSceneFadeTrack>();
				FMatineeImportTools::CopyInterpFadeTrack(MatineeFadeTrack, FadeTrack);
			}

			UInterpTrackSlomo* MatineeSlomoTrack = DirGroup->GetSlomoTrack();
			if (MatineeSlomoTrack && MatineeSlomoTrack->GetNumKeyframes() != 0)
			{
				UMovieSceneSlomoTrack* SlomoTrack = NewMovieScene->AddMasterTrack<UMovieSceneSlomoTrack>();
				FMatineeImportTools::CopyInterpSlomoTrack(MatineeSlomoTrack, SlomoTrack);
			}
			
			UInterpTrackColorScale* MatineeColorScaleTrack = DirGroup->GetColorScaleTrack();
			if (MatineeColorScaleTrack && MatineeColorScaleTrack->GetNumKeyframes() != 0)
			{
				UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s'."), *MatineeColorScaleTrack->TrackTitle);
				++NumWarnings;
			}

			UInterpTrackAudioMaster* MatineeAudioMasterTrack = DirGroup->GetAudioMasterTrack();
			if (MatineeAudioMasterTrack && MatineeAudioMasterTrack->GetNumKeyframes() != 0)
			{
				UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s'."), *MatineeAudioMasterTrack->TrackTitle);
				++NumWarnings;
			}
		}

		MatineeActor->TermInterp();
	}
	return NewActor;
}

#undef LOCTEXT_NAMESPACE
