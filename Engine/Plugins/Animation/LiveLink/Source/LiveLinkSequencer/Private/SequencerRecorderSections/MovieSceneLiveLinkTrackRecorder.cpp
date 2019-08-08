// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneLiveLinkTrackRecorder.h"

#include "Engine/Engine.h"
#include "Engine/TimecodeProvider.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSequencerPrivate.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneFolder.h"
#include "MovieScene/MovieSceneLiveLinkSection.h"
#include "MovieScene/MovieSceneLiveLinkTrack.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "SequenceRecorderUtils.h"
#include "TakeRecorderSource/TakeRecorderLiveLinkSource.h"


static TAutoConsoleVariable<int32> CVarSequencerAlwaysUseRecordLiveLinkTimecode(
	TEXT("Sequencer.AlwayRecordLiveLinkTimecode"),
	0, TEXT("If nonzero we use the LiveLink Timecode for time, even if Subject isn't Synchronized."),
	ECVF_Default);


void UMovieSceneLiveLinkTrackRecorder::CreateTrack(UMovieScene* InMovieScene, const FName& InSubjectName, bool bInSaveSubjectSettings, UMovieSceneTrackRecorderSettings* InSettingsObject)
{
	MovieScene = InMovieScene;
	SubjectName = InSubjectName;
	bSaveSubjectSettings = bInSaveSubjectSettings;
	CreateTracks();
}

UMovieSceneLiveLinkTrack* UMovieSceneLiveLinkTrackRecorder::DoesLiveLinkMasterTrackExist(const FName& MasterTrackName, const TSubclassOf<ULiveLinkRole>& InTrackRole)
{
	for (UMovieSceneTrack* MasterTrack : MovieScene->GetMasterTracks())
	{
		if (MasterTrack->IsA(UMovieSceneLiveLinkTrack::StaticClass()))
		{
			UMovieSceneLiveLinkTrack* TestLiveLinkTrack = CastChecked<UMovieSceneLiveLinkTrack>(MasterTrack);
			if (TestLiveLinkTrack && TestLiveLinkTrack->GetPropertyName() == MasterTrackName && TestLiveLinkTrack->GetTrackRole() == InTrackRole)
			{
				return TestLiveLinkTrack;
			}
		}
	}
	return nullptr;
}

void UMovieSceneLiveLinkTrackRecorder::CreateTracks()
{
	LiveLinkTrack = nullptr;
	MovieSceneSection.Reset();

	FramesToProcess.Empty();

	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	FText Error;
	
	if (LiveLinkClient == nullptr)
	{
		UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Error: Could not create live link track. LiveLink module is not available."));
		return;
	}

	if (SubjectName == NAME_None)
	{
		UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Error: Could not create live link track. Desired subject name is empty."));
		return;
	}

	//Find the subject key associated with the desired subject name. Only one subject with the same name can be enabled.
	const bool bIncludeDisabledSubjects = false;
	const bool bIncludeVirtualSubjects = false;
	TArray<FLiveLinkSubjectKey> EnabledSubjects = LiveLinkClient->GetSubjects(bIncludeDisabledSubjects, bIncludeVirtualSubjects);
	const FLiveLinkSubjectKey* DesiredSubjectKey = EnabledSubjects.FindByPredicate([=](const FLiveLinkSubjectKey& InOther) { return SubjectName == InOther.SubjectName; });
	if (DesiredSubjectKey == nullptr)
	{
		UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Error: Could not create live link track. Could not find an enabled subject with subject name '%s'."), *SubjectName.ToString());
		return;
	}

	TSharedPtr<FLiveLinkStaticDataStruct> StaticData = MakeShared<FLiveLinkStaticDataStruct>();
	const bool bRegistered = LiveLinkClient->RegisterForSubjectFrames(SubjectName
																	, FOnLiveLinkSubjectStaticDataReceived::FDelegate::CreateUObject(this, &UMovieSceneLiveLinkTrackRecorder::OnStaticDataReceived)
																	, FOnLiveLinkSubjectFrameDataReceived::FDelegate::CreateUObject(this, &UMovieSceneLiveLinkTrackRecorder::OnFrameDataReceived)
																	, OnStaticDataReceivedHandle
																	, OnFrameDataReceivedHandle
																	, SubjectRole
																	, StaticData.Get());

	if(bRegistered)
	{
		LiveLinkTrack = DoesLiveLinkMasterTrackExist(SubjectName, SubjectRole);
		if (!LiveLinkTrack.IsValid())
		{
			LiveLinkTrack = MovieScene->AddMasterTrack<UMovieSceneLiveLinkTrack>();
			LiveLinkTrack->SetTrackRole(SubjectRole);
		}
		else
		{
			LiveLinkTrack->RemoveAllAnimationData();
		}

		LiveLinkTrack->SetPropertyNameAndPath(SubjectName, SubjectName.ToString());

		MovieSceneSection = Cast<UMovieSceneLiveLinkSection>(LiveLinkTrack->CreateNewSection());
		if (MovieSceneSection != nullptr)
		{
			MovieSceneSection->SetIsActive(false);
			LiveLinkTrack->AddSection(*MovieSceneSection);

			FLiveLinkSubjectPreset SubjectPreset;
			if (bSaveSubjectSettings)
			{
				SubjectPreset = LiveLinkClient->GetSubjectPreset(*DesiredSubjectKey, MovieSceneSection.Get());
			}
			else
			{
				//When we don't save defaults, fill in a preset to match the subject. SourceGuid is left out voluntarily. It will be filled when the sequencer is playing back the track.
				SubjectPreset.Key.Source.Invalidate();
				SubjectPreset.Key.SubjectName = SubjectName;
				SubjectPreset.Role = SubjectRole;
				SubjectPreset.bEnabled = true;
			}

			//Initialize the LiveLink Section. This will spawn required sub sections to manage data for this role
			MovieSceneSection->Initialize(SubjectPreset, StaticData);

			MovieSceneSection->CreateChannelProxy();
		}
		else
		{
			UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Error Creating LiveLink MovieScene Section for subject '%s' with role '%s"), *SubjectName.ToString(), *SubjectRole->GetFName().ToString());
		}
	}
	else
	{
		UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Error: Could not register to SubjectName '%s' from LiveLink client."), *(SubjectName.ToString()));
	}
}

void UMovieSceneLiveLinkTrackRecorder::SetSectionStartTimecodeImpl(const FTimecode& InSectionStartTimecode, const FFrameNumber& InSectionFirstFrame) 
{
	float Time = 0.0f;
	SecondsDiff = FPlatformTime::Seconds() - Time;

	if (MovieSceneSection.IsValid())
	{
		MovieSceneSection->TimecodeSource = FMovieSceneTimecodeSource(InSectionStartTimecode);
	}
}

void UMovieSceneLiveLinkTrackRecorder::StopRecordingImpl()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	if (LiveLinkClient && MovieSceneSection.IsValid())
	{
		LiveLinkClient->UnregisterSubjectFramesHandle(SubjectName, OnStaticDataReceivedHandle, OnFrameDataReceivedHandle);
	}
}

void UMovieSceneLiveLinkTrackRecorder::FinalizeTrackImpl()
{
	if (MovieSceneSection.IsValid())
	{
		FKeyDataOptimizationParams Params;
		Params.bAutoSetInterpolation = true;
		MovieSceneSection->FinalizeSection(bReduceKeys, Params);
		
		MovieSceneSection->SetIsActive(true);
	}
}

void UMovieSceneLiveLinkTrackRecorder::RecordSampleImpl(const FQualifiedFrameTime& CurrentTime)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	if (LiveLinkClient && MovieSceneSection.IsValid())
	{
		//we know all section have same tick resolution
		const FFrameRate TickResolution = MovieSceneSection->GetTypedOuter<UMovieScene>()->GetTickResolution();
		const FFrameNumber CurrentFrame = CurrentTime.ConvertTo(TickResolution).FloorToFrame();

		const bool bAlwaysUseTimecode = CVarSequencerAlwaysUseRecordLiveLinkTimecode->GetInt() == 0 ? false : true;
		bool bSyncedOrForced = bAlwaysUseTimecode || LiveLinkClient->IsSubjectTimeSynchronized(SubjectName);

		if (FramesToProcess.Num() > 0)
		{
			for (const FLiveLinkFrameDataStruct& Frame : FramesToProcess)
			{
				FFrameNumber FrameNumber;

				if (bSyncedOrForced && GEngine && GEngine->GetTimecodeProvider() && GEngine->GetTimecodeProvider()->GetSynchronizationState() == ETimecodeProviderSynchronizationState::Synchronized)
				{
					//Get StartTime on Section in TimeCode FrameRate
					//Convert that to LiveLink FrameRate and subtract out from LiveLink Frame to get section starting from zero.
					//Finally convert that to the actual MovieScene Section FrameRate(TickResolution).
					const FQualifiedFrameTime TimeProviderStartFrameTime = FQualifiedFrameTime(MovieSceneSection->TimecodeSource.Timecode, FApp::GetTimecodeFrameRate());
					FQualifiedFrameTime LiveLinkFrameTime = Frame.GetBaseData()->MetaData.SceneTime;
					const FFrameNumber FrameNumberStart = TimeProviderStartFrameTime.ConvertTo(LiveLinkFrameTime.Rate).FrameNumber;
					LiveLinkFrameTime.Time.FrameNumber -= FrameNumberStart;
					FFrameTime FrameTime = LiveLinkFrameTime.ConvertTo(TickResolution);
					FrameNumber = FrameTime.FrameNumber;
				}
				else
				{
					const double Second = Frame.GetBaseData()->WorldTime.GetOffsettedTime() - SecondsDiff;
					FrameNumber = (Second * TickResolution).FloorToFrame();
				}

				MovieSceneSection->RecordFrame(FrameNumber, Frame);
			}

			//Empty out frames that were processed
			FramesToProcess.Reset();
		} 
	}
}

void UMovieSceneLiveLinkTrackRecorder::AddContentsToFolder(UMovieSceneFolder* InFolder)
{
	if (LiveLinkTrack.IsValid())
	{
		InFolder->AddChildMasterTrack(LiveLinkTrack.Get());
	}
}

void UMovieSceneLiveLinkTrackRecorder::OnStaticDataReceived(FLiveLinkSubjectKey InSubjectKey, TSubclassOf<ULiveLinkRole> InSubjectRole, const FLiveLinkStaticDataStruct& InStaticData)
{
	UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Static data changed for subject '%s' while recording. This is not supported and could cause problems with associated frame data"), *(SubjectName.ToString()));
}

void UMovieSceneLiveLinkTrackRecorder::OnFrameDataReceived(FLiveLinkSubjectKey InSubjectKey, TSubclassOf<ULiveLinkRole> InSubjectRole, const FLiveLinkFrameDataStruct& InFrameData)
{
	if (InSubjectKey.SubjectName.Name != SubjectName)
	{
		UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Received frame for Subject '%s' but was expecting subject '%s'"), *(InSubjectKey.SubjectName.Name.ToString()), *(SubjectName.ToString()));
		return;
	}

	if (InSubjectRole != SubjectRole)
	{
		UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Received frame for Subject '%s' for role '%s' but was expecting role '%s'")
			, *InSubjectKey.SubjectName.ToString()
			, *InSubjectRole.GetDefaultObject()->GetDisplayName().ToString()
			, *SubjectRole.GetDefaultObject()->GetDisplayName().ToString());

		return;
	}

	//We need to make our own copy of the incoming frame to process it when record is called
	FLiveLinkFrameDataStruct CopiedFrame;
	CopiedFrame.InitializeWith(InFrameData);

	FramesToProcess.Emplace(MoveTemp(CopiedFrame));
}

bool UMovieSceneLiveLinkTrackRecorder::LoadRecordedFile(const FString& FileName, UMovieScene *InMovieScene, TMap<FGuid, AActor*>& ActorGuidToActorMap, TFunction<void()> InCompletionCallback) 
{
	UE_LOG(LogLiveLinkSequencer, Warning, TEXT("Loading recorded file for live link tracks is not supported."));
	return false;
}
