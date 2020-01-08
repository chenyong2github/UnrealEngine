// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTools.h"
#include "SequencerScriptingEditor.h"
#include "MovieSceneCapture.h"
#include "MovieSceneCaptureDialogModule.h"
#include "AutomatedLevelSequenceCapture.h"
#include "MovieSceneTimeHelpers.h"
#include "UObject/Stack.h"
#include "UObject/Package.h"
#include "LevelSequencePlayer.h"
#include "FbxExporter.h"
#include "FbxImporter.h"
#include "MovieSceneToolsUserSettings.h"
#include "MovieSceneToolHelpers.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "MovieSceneCommonHelpers.h"

#define LOCTEXT_NAMESPACE "SequencerTools"

bool USequencerToolsFunctionLibrary::RenderMovie(UMovieSceneCapture* InCaptureSettings, FOnRenderMovieStopped OnFinishedCallback)
{
	IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
	
	// Because this comes from the Python/BP layer we need to soft-validate the state before we pass it onto functions that do a assert-based validation.
	if (!InCaptureSettings)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot start Render Sequence to Movie with null capture settings."), ELogVerbosity::Error);
		return false;
	}

	if (IsRenderingMovie())
	{
		FFrame::KismetExecutionMessage(TEXT("Capture already in progress."), ELogVerbosity::Error);
		return false;
	}

	// If they're capturing a level sequence we'll do some additional checking as there are more parameters on the Automated Level Sequence capture.
	UAutomatedLevelSequenceCapture* LevelSequenceCapture = Cast<UAutomatedLevelSequenceCapture>(InCaptureSettings);
	if (LevelSequenceCapture)
	{
		if (!LevelSequenceCapture->LevelSequenceAsset.IsValid())
		{
			// UE_LOG(LogTemp, Warning, TEXT("No Level Sequence Asset specified in UAutomatedLevelSequenceCapture."));
			FFrame::KismetExecutionMessage(TEXT("No Level Sequence Asset specified in UAutomatedLevelSequenceCapture."), ELogVerbosity::Error);
			return false;
		}

		if (!LevelSequenceCapture->bUseCustomStartFrame && !LevelSequenceCapture->bUseCustomEndFrame)
		{
			// If they don't want to use a custom start/end frame we override the default values to be the length of the sequence, as the default is [0,1)
			ULevelSequence* LevelSequence = Cast<ULevelSequence>(LevelSequenceCapture->LevelSequenceAsset.TryLoad());
			if (!LevelSequence)
			{
				const FString ErrorMessage = FString::Printf(TEXT("Specified Level Sequence Asset failed to load. Specified Asset Path: %s"), *LevelSequenceCapture->LevelSequenceAsset.GetAssetPathString());
				FFrame::KismetExecutionMessage(*ErrorMessage, ELogVerbosity::Error);
				return false;
			}

			FFrameRate DisplayRate = LevelSequence->GetMovieScene()->GetDisplayRate();
			FFrameRate TickResolution = LevelSequence->GetMovieScene()->GetTickResolution();
			
			LevelSequenceCapture->Settings.CustomFrameRate = DisplayRate;
			LevelSequenceCapture->Settings.bUseCustomFrameRate = true;
			LevelSequenceCapture->Settings.bUseRelativeFrameNumbers = false;
			TRange<FFrameNumber> Range = LevelSequence->GetMovieScene()->GetPlaybackRange();

			FFrameNumber StartFrame = MovieScene::DiscreteInclusiveLower(Range);
			FFrameNumber EndFrame = MovieScene::DiscreteExclusiveUpper(Range);

			FFrameNumber RoundedStartFrame = FFrameRate::TransformTime(StartFrame, TickResolution, DisplayRate).CeilToFrame();
			FFrameNumber RoundedEndFrame = FFrameRate::TransformTime(EndFrame, TickResolution, DisplayRate).CeilToFrame();

			LevelSequenceCapture->CustomStartFrame = RoundedStartFrame;
			LevelSequenceCapture->CustomEndFrame = RoundedEndFrame;
		}
	}

	auto LocalCaptureStoppedCallback = [OnFinishedCallback](bool bSuccess)
	{
		OnFinishedCallback.ExecuteIfBound(bSuccess);
	};

	MovieSceneCaptureModule.StartCapture(InCaptureSettings);
	MovieSceneCaptureModule.GetCurrentCapture()->CaptureStoppedDelegate.AddLambda(LocalCaptureStoppedCallback);
	return true;
}

void USequencerToolsFunctionLibrary::CancelMovieRender()
{
	IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
	TSharedPtr<FMovieSceneCaptureBase> CurrentCapture = MovieSceneCaptureModule.GetCurrentCapture();
	if (CurrentCapture.IsValid())
	{
		// We just invoke the capture's Cancel function. This will cause a shut-down of the capture (the same as the UI)
		// which will invoke all of the necessary callbacks as well. We don't null out CurrentCapture because that is done
		// as the result of its shutdown callbacks.
		CurrentCapture->Cancel();
	}
}


bool USequencerToolsFunctionLibrary::ExportFBX(UWorld* World, ULevelSequence* Sequence, const TArray<FSequencerBindingProxy>& InBindings, UFbxExportOption* OverrideOptions, const FString& InFBXFileName)
{
	
	
	UnFbx::FFbxExporter* Exporter = UnFbx::FFbxExporter::GetInstance();
	//Show the fbx export dialog options
	Exporter->SetExportOptionsOverride(OverrideOptions);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	TArray<FGuid> Bindings;
	for (const FSequencerBindingProxy& Proxy : InBindings)
	{
		if (Proxy.Sequence == Sequence)
		{
			Bindings.Add(Proxy.BindingID);
		}
	}

	INodeNameAdapter NodeNameAdapter;
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);
	Player->Initialize(Sequence, World->PersistentLevel, Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *Sequence, *Player);
	FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
	FMovieSceneSequenceTransform RootToLocalTransform;
	bool bDidExport = MovieSceneToolHelpers::ExportFBX(World, MovieScene, Player, Bindings, NodeNameAdapter, Template, InFBXFileName, RootToLocalTransform);
	Exporter->SetExportOptionsOverride(nullptr);
	return bDidExport;
}



TArray<FGuid> AddActors(UWorld* World, UMovieSceneSequence* InSequence, UMovieScene* InMovieScene, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID,const TArray<TWeakObjectPtr<AActor> >& InActors)
{
	TArray<FGuid> PossessableGuids;

	if (InMovieScene->IsReadOnly())
	{
		return PossessableGuids;
	}

	for (TWeakObjectPtr<AActor> WeakActor : InActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			FGuid ExistingGuid = Player->FindObjectId(*Actor, TemplateID);
			if (!ExistingGuid.IsValid())
			{
				InMovieScene->Modify();
				const FGuid PossessableGuid = InMovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
				PossessableGuids.Add(PossessableGuid);
				InSequence->BindPossessableObject(PossessableGuid, *Actor, World);

				//TODO New to figure way to call void FLevelSequenceEditorToolkit::AddDefaultTracksForActor(AActor& Actor, const FGuid Binding)

				if (Actor->IsA<ACameraActor>())
				{
					MovieSceneToolHelpers::CameraAdded(InMovieScene, PossessableGuid, 0);
				}
			}
		}
	}
	return PossessableGuids;
}


void ImportFBXCamera(UnFbx::FFbxImporter* FbxImporter, UWorld* World, UMovieSceneSequence* Sequence, UMovieScene* InMovieScene, IMovieScenePlayer* Player, FMovieSceneSequenceIDRef TemplateID, TMap<FGuid, FString>& InObjectBindingMap, bool bMatchByNameOnly, bool bCreateCameras)
{
	if (bCreateCameras)
	{
		TArray<FbxCamera*> AllCameras;
		MovieSceneToolHelpers::GetCameras(FbxImporter->Scene->GetRootNode(), AllCameras);

		// Find unmatched cameras
		TArray<FbxCamera*> UnmatchedCameras;
		for (auto Camera : AllCameras)
		{
			FString NodeName = MovieSceneToolHelpers::GetCameraName(Camera);

			bool bMatched = false;
			for (auto InObjectBinding : InObjectBindingMap)
			{
				FString ObjectName = InObjectBinding.Value;
				if (ObjectName == NodeName)
				{
					// Look for a valid bound object, otherwise need to create a new camera and assign this binding to it
					bool bFoundBoundObject = false;
					TArrayView<TWeakObjectPtr<>> BoundObjects = Player->FindBoundObjects(InObjectBinding.Key, TemplateID);
					for (auto BoundObject : BoundObjects)
					{
						if (BoundObject.IsValid())
						{
							bFoundBoundObject = true;
							break;
						}
					}
				}
			}

			if (!bMatched)
			{
				UnmatchedCameras.Add(Camera);
			}
		}

		// If there are new cameras, clear the object binding map so that we're only assigning values to the newly created cameras
		if (UnmatchedCameras.Num() != 0)
		{
			InObjectBindingMap.Reset();
			bMatchByNameOnly = true;
		}

		// Add any unmatched cameras
		for (auto UnmatchedCamera : UnmatchedCameras)
		{
			FString CameraName = MovieSceneToolHelpers::GetCameraName(UnmatchedCamera);

			AActor* NewCamera = nullptr;
			if (UnmatchedCamera->GetApertureMode() == FbxCamera::eFocalLength)
			{
				FActorSpawnParameters SpawnParams;
				NewCamera = World->SpawnActor<ACineCameraActor>(SpawnParams);
				NewCamera->SetActorLabel(*CameraName);
			}
			else
			{
				FActorSpawnParameters SpawnParams;
				NewCamera = World->SpawnActor<ACameraActor>(SpawnParams);
				NewCamera->SetActorLabel(*CameraName);
			}

			// Copy camera properties before adding default tracks so that initial camera properties match and can be restored after sequencer finishes
			MovieSceneToolHelpers::CopyCameraProperties(UnmatchedCamera, NewCamera);

			TArray<TWeakObjectPtr<AActor> > NewCameras;
			NewCameras.Add(NewCamera);
			TArray<FGuid> NewCameraGuids = AddActors(World, Sequence,InMovieScene, Player, TemplateID,NewCameras);

			if (NewCameraGuids.Num())
			{
				InObjectBindingMap.Add(NewCameraGuids[0]);
				InObjectBindingMap[NewCameraGuids[0]] = CameraName;
			}
		}
	}
	//everything created now import it in.
	MovieSceneToolHelpers::ImportFBXCameraToExisting(FbxImporter,  InMovieScene, Player, TemplateID, InObjectBindingMap, bMatchByNameOnly, true);
}

bool USequencerToolsFunctionLibrary::ImportFBX(UWorld* World, ULevelSequence* Sequence, const TArray<FSequencerBindingProxy>& InBindings, UMovieSceneUserImportFBXSettings* ImportFBXSettings, const FString&  ImportFilename)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return false;
	}

	TMap<FGuid, FString> ObjectBindingMap;
	for (const FSequencerBindingProxy& Binding : InBindings)
	{
		FString Name = MovieScene->GetObjectDisplayName(Binding.BindingID).ToString();
		ObjectBindingMap.Add(Binding.BindingID, Name);
	}

	FFBXInOutParameters InOutParams;
	if (!MovieSceneToolHelpers::ReadyFBXForImport(ImportFilename, ImportFBXSettings, InOutParams))
	{
		return false;
	}

	const bool bMatchByNameOnly = ImportFBXSettings->bMatchByNameOnly;
	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);
	Player->Initialize(Sequence, World->GetLevel(0), Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *Sequence, *Player);

	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();

	ImportFBXCamera(FbxImporter, World, Sequence, MovieScene, Player, MovieSceneSequenceID::Root, ObjectBindingMap, bMatchByNameOnly,
		ImportFBXSettings->bCreateCameras);

	bool bValid = MovieSceneToolHelpers::ImportFBXIfReady(World, MovieScene, Player, ObjectBindingMap, ImportFBXSettings, InOutParams);

	return bValid;
}




#undef LOCTEXT_NAMESPACE // "SequencerTools"