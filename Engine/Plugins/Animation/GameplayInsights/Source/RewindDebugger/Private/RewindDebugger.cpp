// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebugger.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "IAnimationProvider.h"
#include "IGameplayProvider.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "TraceServices/ITraceServicesModule.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "IRewindDebuggerExtension.h"


FRewindDebugger* FRewindDebugger::InternalInstance = nullptr;
const FName IRewindDebuggerExtension::ModularFeatureName = "RewindDebuggerExtension";

static void IterateExtensions(TFunction<void(IRewindDebuggerExtension* Extension)> IteratorFunction)
{
	// update extensions
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(IRewindDebuggerExtension::ModularFeatureName);
	for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
	{
		IRewindDebuggerExtension* Extension = static_cast<IRewindDebuggerExtension*>(ModularFeatures.GetModularFeatureImplementation(IRewindDebuggerExtension::ModularFeatureName, ExtensionIndex));
		IteratorFunction(Extension);
	}
}

FRewindDebugger::FRewindDebugger()  :
	ControlState(FRewindDebugger::EControlState::Pause),
	bPIEStarted(false),
	bPIESimulating(false),
	bAutoRecord(false),
	bRecording(false),
	PlaybackRate(1),
	CurrentScrubTime(0),
	RecordingIndex(0),
	bTargetActorPositionValid(false)
{
	RecordingDuration.Set(0);

	if (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld)
	{
		OnPIEStarted(true);
	}

	FEditorDelegates::PreBeginPIE.AddRaw(this, &FRewindDebugger::OnPIEStarted);
	FEditorDelegates::PausePIE.AddRaw(this, &FRewindDebugger::OnPIEPaused);
	FEditorDelegates::ResumePIE.AddRaw(this, &FRewindDebugger::OnPIEResumed);
	FEditorDelegates::EndPIE.AddRaw(this, &FRewindDebugger::OnPIEStopped);
	FEditorDelegates::SingleStepPIE.AddRaw(this, &FRewindDebugger::OnPIESingleStepped);

	DebugTargetActor.OnPropertyChanged = DebugTargetActor.OnPropertyChanged.CreateLambda([this](FString Target) { RefreshDebugComponents(); });

	UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("RewindDebugger"), 0.0f, [this](float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRewindDebuggerModule_Tick);

		Tick(DeltaTime);

		return true;
	});
}

FRewindDebugger::~FRewindDebugger() 
{
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
	FEditorDelegates::PausePIE.RemoveAll(this);
	FEditorDelegates::ResumePIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::SingleStepPIE.RemoveAll(this);

	FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

}

void FRewindDebugger::Initialize() 
{
	InternalInstance = new FRewindDebugger;
}

void FRewindDebugger::Shutdown() 
{
	delete InternalInstance;
}

void FRewindDebugger::OnComponentListChanged(const FOnComponentListChanged& InComponentListChangedDelegate)
{
	ComponentListChangedDelegate = InComponentListChangedDelegate;
}

void FRewindDebugger::OnTrackCursor(const FOnTrackCursor& InTrackCursorDelegate)
{
	TrackCursorDelegate = InTrackCursorDelegate;
}

void FRewindDebugger::OnPIEStarted(bool bSimulating)
{
	bPIEStarted = true;
	bPIESimulating = true;

	UE::Trace::ToggleChannel(TEXT("Object"), true);

	if (bAutoRecord)
	{
		StartRecording();
	}
}

void FRewindDebugger::OnPIEPaused(bool bSimulating)
{
	bPIESimulating = false;
	ControlState = EControlState::Pause;

	if (bRecording)
	{
		UWorld* World = GetWorldToVisualize();
		RecordingDuration.Set(FObjectTrace::GetWorldElapsedTime(World));
		SetCurrentScrubTime(RecordingDuration.Get());
	}
}

void FRewindDebugger::OnPIEResumed(bool bSimulating)
{
	bPIESimulating = true;

	// restore all relative transforms of any meshes that may have been moved while scrubbing
	for (TTuple<uint64, FMeshComponentResetData>& MeshData : MeshComponentsToReset)
	{
		if (USkeletalMeshComponent* MeshComponent = MeshData.Value.Component.Get())
		{
			MeshComponent->SetRelativeTransform(MeshData.Value.RelativeTransform);
		}
	}

	MeshComponentsToReset.Empty();
}

void FRewindDebugger::OnPIESingleStepped(bool bSimulating)
{
	// restore all relative transforms of any meshes that may have been moved while scrubbing
	for (TTuple<uint64, FMeshComponentResetData>& MeshData : MeshComponentsToReset)
	{
		if (USkeletalMeshComponent* MeshComponent = MeshData.Value.Component.Get())
		{
			MeshComponent->SetRelativeTransform(MeshData.Value.RelativeTransform);
		}
	}

	MeshComponentsToReset.Empty();

	if (bRecording)
	{
		UWorld* World = GetWorldToVisualize();
		RecordingDuration.Set(FObjectTrace::GetWorldElapsedTime(World));
		SetCurrentScrubTime(RecordingDuration.Get());
	}
}


void FRewindDebugger::OnPIEStopped(bool bSimulating)
{
	bPIEStarted = false;
	bPIESimulating = false;
	MeshComponentsToReset.Empty();

	UE::Trace::ToggleChannel(TEXT("Object"), false);

	StopRecording();
	// clear the current recording (until we support playback in the Editor world on spawned actors)
	RecordingDuration.Set(0);
	SetCurrentScrubTime(0);
}

bool FRewindDebugger::UpdateComponentList(uint64 ParentId, TArray<TSharedPtr<FDebugObjectInfo>>& ComponentList)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = UnrealInsightsModule->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
	UWorld* World = GetWorldToVisualize();

	const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");

	bool bChanged = false;

	GameplayProvider->EnumerateObjects([this, &bChanged, ParentId, GameplayProvider, &ComponentList](const FObjectInfo& InObjectInfo)
	{
		// todo: filter components based on creation/destruction frame, and the current scrubbing time (so dynamically created and destroyed components won't add up)

		const FObjectInfo* ObjectInfo = &InObjectInfo;
		if (ObjectInfo->OuterId == ParentId)
		{
			int32 FoundIndex = ComponentList.FindLastByPredicate([ObjectInfo](const TSharedPtr<FDebugObjectInfo>& Info) { return Info->ObjectName == ObjectInfo->Name; });

			if (FoundIndex >= 0 && ComponentList[FoundIndex]->ObjectName == InObjectInfo.Name)
			{
				ComponentList[FoundIndex]->ObjectId = ObjectInfo->Id; // there is an issue with skeletal mesh components changing ids
				bChanged = bChanged || UpdateComponentList(ObjectInfo->Id, ComponentList[FoundIndex]->Children);
			}
			else
			{
				bChanged = true;
				ComponentList.Add(MakeShared<FDebugObjectInfo>(ObjectInfo->Id,InObjectInfo.Name));
				UpdateComponentList(ObjectInfo->Id, ComponentList.Last()->Children);
			}
		}
	});

	return bChanged;
}

bool FRewindDebugger::GetTargetActorPosition(FVector& OutPosition) const
{
	OutPosition = TargetActorPosition;
	return bTargetActorPositionValid;
}

uint64 FRewindDebugger::GetTargetActorId() const
{
	if (DebugTargetActor.Get() == "")
	{
		return 0;
	}

	uint64 TargetActorId = 0;

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			GameplayProvider->EnumerateObjects([this,&TargetActorId](const FObjectInfo& InObjectInfo)
			{
				if (DebugTargetActor.Get() == InObjectInfo.Name)
				{
					TargetActorId = InObjectInfo.Id;
				}
			});
		}
	}

	return TargetActorId;
}

void FRewindDebugger::RefreshDebugComponents()
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		UWorld* World = GetWorldToVisualize();

		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			uint64 TargetActorId = GetTargetActorId();

			if (TargetActorId == 0)
			{
				return;
			}

			bool bChanged = false;

			// add actor (even if it isn't found in the gameplay provider)
			if (DebugComponents.Num() == 0)
			{
				bChanged = true;
				DebugComponents.Add(MakeShared<FDebugObjectInfo>(TargetActorId, DebugTargetActor.Get()));
			}
			else
			{
				if (DebugComponents[0]->ObjectName != DebugTargetActor.Get() || DebugComponents[0]->ObjectId != TargetActorId)
				{
					bChanged = true;
					DebugComponents[0] = MakeShared<FDebugObjectInfo>(TargetActorId, DebugTargetActor.Get());
				}
			}

			if (TargetActorId != 0 && DebugComponents.Num() > 0)
			{
				bChanged = bChanged || UpdateComponentList(TargetActorId, DebugComponents[0]->Children);
			}

			if (bChanged)
			{
				ComponentListChangedDelegate.ExecuteIfBound();
			}
		}
	}
}

void FRewindDebugger::StartRecording()
{
	if (!CanStartRecording())
	{
		return;
	}

	// Enable Object and Animation Trace filters
	UE::Trace::ToggleChannel(TEXT("ObjectProperties"), true);
	UE::Trace::ToggleChannel(TEXT("Animation"), true);
	UE::Trace::ToggleChannel(TEXT("Frame"), true);

	// update extensions
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->RecordingStarted(this);
		}
	);

	RecordingDuration.Set(0);
	RecordingIndex++;
	bRecording = true;

	// setup FObjectTrace to start tracking tracing times from 0
	// and increment the RecordingIndex so we can use it to distinguish between the latest recording and older ones
	UWorld* World = GetWorldToVisualize();
	FObjectTrace::ResetWorldElapsedTime(World);
	FObjectTrace::SetWorldRecordingIndex(World, RecordingIndex);
}

void FRewindDebugger::StopRecording()
{
	if (bRecording)
	{
		// Enable Object and Animation Trace filters
		UE::Trace::ToggleChannel(TEXT("ObjectProperties"), false);
		UE::Trace::ToggleChannel(TEXT("Animation"), false);
		UE::Trace::ToggleChannel(TEXT("Frame"), false);

		// update extensions
		IterateExtensions([this](IRewindDebuggerExtension* Extension)
			{
				Extension->RecordingStopped(this);
			}
		);

		bRecording = false;
	}
}

bool FRewindDebugger::CanPause() const
{
	return ControlState != EControlState::Pause;
}

void FRewindDebugger::Pause()
{
	if (CanPause())
	{
		if (bPIESimulating)
		{
			// pause PIE
		}

		ControlState = EControlState::Pause;
	}
}

bool FRewindDebugger::IsPlaying() const
{
	return ControlState==EControlState::Play && !bPIESimulating;
}

bool FRewindDebugger::CanPlay() const
{
	return ControlState!=EControlState::Play && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::Play()
{
	if (CanPlay())
	{
		if (CurrentScrubTime >= RecordingDuration.Get())
		{
			SetCurrentScrubTime(0);
		}

		ControlState = EControlState::Play;
	}
}

bool FRewindDebugger::CanPlayReverse() const
{
	return ControlState!=EControlState::PlayReverse && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::PlayReverse()
{
	if (CanPlayReverse())
	{
		if (CurrentScrubTime <= 0)
		{
			SetCurrentScrubTime(RecordingDuration.Get());
		}

		ControlState = EControlState::PlayReverse;
	}
}

bool FRewindDebugger::CanScrub() const
{
	return !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::ScrubToStart()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(0);
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::ScrubToEnd()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(RecordingDuration.Get());
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::StepForward()
{
	if (CanScrub())
	{
		Pause();

		SetCurrentScrubTime(FMath::Min(CurrentScrubTime + 1.0f/30.0f, RecordingDuration.Get()));
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::StepBackward()
{
	if (CanScrub())
	{
		Pause();
		// todo: snap to actual frames from AnimationProvider
		SetCurrentScrubTime(FMath::Max(CurrentScrubTime - 1.0f/30.0f, 0.0f));
		TrackCursorDelegate.ExecuteIfBound(true);
	}
}


void FRewindDebugger::ScrubToTime(float ScrubTime, bool bIsScrubbing)
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(ScrubTime);
	}
}

UWorld* FRewindDebugger::GetWorldToVisualize() const
{
	// we probably want to replace this with a world selector widget, if we are going to support tracing from anything other thn the PIE world

	UWorld* World = nullptr;

#if WITH_EDITOR
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && EditorEngine != nullptr && World == nullptr)
	{
		// lets use PlayWorld during PIE/Simulate and regular world from editor otherwise, to draw debug information
		World = EditorEngine->PlayWorld != nullptr ? ToRawPtr(EditorEngine->PlayWorld) : EditorEngine->GetEditorWorldContext().World();
	}

#endif
	if (!GIsEditor && World == nullptr)
	{
		World = GEngine->GetWorld();
	}

	return World;
}

void  FRewindDebugger::SetCurrentScrubTime(float Time)
{
	CurrentScrubTime = Time;
	UpdateTraceTime();
}

void FRewindDebugger::UpdateTraceTime()
{
	// find the current Insights trace time for the current scrub time.
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		UWorld* World = GetWorldToVisualize();

		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(RecordingIndex))
			{
				uint64 EventCount = Recording->GetEventCount();

				if (EventCount > 0)
				{
					uint64 EventIndex;

					for(EventIndex = 0; EventIndex < EventCount-1; EventIndex++)
					{
						const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
						// todo: optimize this linear search (cache current index, and pass it in as a hint)
						//		 and do interpolation between nearest two frames
						if (Event.ElapsedTime >= CurrentScrubTime)
						{
							break;
						}
					}
					TraceTime.Set(Recording->GetEvent(EventIndex).ProfileTime);
				}
			}
		}
	}
}

const TraceServices::IAnalysisSession* FRewindDebugger::GetAnalysisSession() const
{
	if (UnrealInsightsModule == nullptr)
	{
		UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	}

	return UnrealInsightsModule->GetAnalysisSession().Get();
}

void FRewindDebugger::Tick(float DeltaTime)
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (bRecording)
		{
			// if you select a debug target before you start recording, update component list when it becomes valid
			RefreshDebugComponents();
		}

		if (const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider"))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			UWorld* World = GetWorldToVisualize();

			if (bPIESimulating)
			{
				if (bRecording)
				{
					RecordingDuration.Set(FObjectTrace::GetWorldElapsedTime(World));
					SetCurrentScrubTime(RecordingDuration.Get());
					TrackCursorDelegate.ExecuteIfBound(false);
				}
			}
			else
			{
				if (RecordingDuration.Get() > 0)
				{
					UpdateTraceTime(); 

					if (ControlState == EControlState::Play || ControlState == EControlState::PlayReverse)
					{
						float Rate = PlaybackRate * (ControlState == EControlState::Play ? 1 : -1);
						SetCurrentScrubTime(FMath::Clamp(CurrentScrubTime + Rate * DeltaTime, 0.0f, RecordingDuration.Get()));
						TrackCursorDelegate.ExecuteIfBound(Rate<0);

						if (CurrentScrubTime == 0 || CurrentScrubTime == RecordingDuration.Get())
						{
							// pause at end.
							ControlState = EControlState::Pause;
						}
					}

					double CurrentTraceTime = TraceTime.Get();
					uint64 TargetActorId = GetTargetActorId();

					// update pose on all SkeletalMeshComponents (todo: move pose updating into an extension)
					ULevel* CurLevel = World->GetCurrentLevel();
					for( AActor* LevelActor : CurLevel->Actors )
					{
						if (LevelActor)
						{
							bool bIsTargetActor = TargetActorId == FObjectTrace::GetObjectId(LevelActor);

							if (bIsTargetActor)
							{
								bTargetActorPositionValid = false;
							}

							TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents;
							LevelActor->GetComponents(SkeletalMeshComponents);

							for (USkeletalMeshComponent* MeshComponent : SkeletalMeshComponents)
							{
								int64 ObjectId = FObjectTrace::GetObjectId(MeshComponent);

								AnimationProvider->ReadSkeletalMeshPoseTimeline(ObjectId, [this, bIsTargetActor, ObjectId, CurrentTraceTime, MeshComponent, AnimationProvider](const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData, bool bHasCurves)
								{
									double PrecedingPoseTime;
									double FollowingPoseTime;
									const FSkeletalMeshPoseMessage* PrecedingPose;
									const FSkeletalMeshPoseMessage* FollowingPose;

									TimelineData.FindNearestEvents(CurrentTraceTime, PrecedingPose, PrecedingPoseTime, FollowingPose, FollowingPoseTime);

									if (FollowingPose || PrecedingPose)
									{	
										// Ideally we should iterpolate between the two poses here if both are valid (we would need to transform the component space transforms to local space though)
										const FSkeletalMeshPoseMessage& PoseMessage = FollowingPose ? *FollowingPose : *PrecedingPose;

										FTransform ComponentWorldTransform;
										const FSkeletalMeshInfo* SkeletalMeshInfo = AnimationProvider->FindSkeletalMeshInfo(PoseMessage.MeshId);
										AnimationProvider->GetSkeletalMeshComponentSpacePose(PoseMessage, *SkeletalMeshInfo, ComponentWorldTransform, MeshComponent->GetEditableComponentSpaceTransforms());

										if (MeshComponentsToReset.Find(ObjectId) == nullptr)
										{
											FMeshComponentResetData ResetData;
											ResetData.Component = MeshComponent;
											ResetData.RelativeTransform = MeshComponent->GetRelativeTransform();
											MeshComponentsToReset.Add(ObjectId, ResetData);
										}

										// todo: we probably need to take into account tick order requirements for attached objects here
										MeshComponent->SetWorldTransform(ComponentWorldTransform);
										MeshComponent->SetForcedLOD(PoseMessage.LodIndex + 1);

										if (bIsTargetActor && !bTargetActorPositionValid)
										{
											// until we have actor transforms traced out, the first skeletal mesh component transform will be used as as the actor position 
											bTargetActorPositionValid = true;
											TargetActorPosition = ComponentWorldTransform.GetTranslation();
										}
									}
								});

								// calling this here, even on meshes which have no recorded data makes meshes with a MasterPoseComponent attach properly during replay
								// longer term solution: we should be recording those meshes
								MeshComponent->ApplyEditedComponentSpaceTransforms();
							}
						}
					}
				}
			}
		}

		// update extensions
		IterateExtensions([DeltaTime, this](IRewindDebuggerExtension* Extension)
			{
				Extension->Update(DeltaTime, this);
			}
		);
	}
}
