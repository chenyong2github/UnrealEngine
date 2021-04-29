// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/ControlRigSnapper.h"
#include "Tools/ControlRigTweener.h" //remove

#include "Channels/MovieSceneFloatChannel.h"
#include "ControlRig.h"
#include "ISequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "Rigs/RigControlHierarchy.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "IKeyArea.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "ILevelSequenceEditorToolkit.h"
#include "Tools/ControlRigSnapper.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "ControlRigObjectBinding.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MatineeImportTools.h"
#include "Tools/ControlRigSnapSettings.h"
#include "MovieSceneToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "ScopedTransaction.h"
#include "MovieSceneToolHelpers.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"

#define LOCTEXT_NAMESPACE "ControlRigSnapper"


FText FControlRigSnapperSelection::GetName() const
{
	FText Name;

	int32 Num = NumSelected();
	if (Num == 0)
	{
		Name = LOCTEXT("None", "None");
	}
	else if (Num == 1)
	{
		for (const FActorForWorldTransforms& Actor : Actors)
		{
			if (Actor.Actor.IsValid())
			{
				FString ActorLabel = Actor.Actor->GetActorLabel();
				if (Actor.SocketName != NAME_None)
				{
					FString SocketString = Actor.SocketName.ToString();
					ActorLabel += (FString(":") + SocketString);
				}
				Name = FText::FromString(ActorLabel);
				break;
			}
		}
		for (const FControlRigForWorldTransforms& Selection : ControlRigs)
		{
			if (Selection.ControlRig.IsValid())
			{
				if (Selection.ControlNames.Num() > 0)
				{
					FName ControlName = Selection.ControlNames[0];
					Name = FText::FromString(ControlName.ToString());
					break;
				}
			}
		}
	}
	else
	{
		Name = LOCTEXT("Multiple", "--Multiple--");
	}
	return Name;
}

int32 FControlRigSnapperSelection::NumSelected() const
{
	int32 Selected = 0;
	for (const FActorForWorldTransforms& Actor : Actors)
	{
		if (Actor.Actor.IsValid())
		{
			++Selected;
		}
	}
	for (const FControlRigForWorldTransforms& Selection : ControlRigs)
	{
		if (Selection.ControlRig.IsValid())
		{
			Selected += Selection.ControlNames.Num();
		}
	}
	return Selected;
}

TWeakPtr<ISequencer> FControlRigSnapper::GetSequencer()
{
	TWeakPtr<ISequencer> WeakSequencer = nullptr;
	ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (LevelSequence)
	{
		IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
		ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
		WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
	}
	return WeakSequencer;
}

static bool LocalGetControlRigControlTransforms(IMovieScenePlayer* Player, UMovieSceneSequence* MovieSceneSequence, FMovieSceneSequenceIDRef Template, FMovieSceneSequenceTransform& RootToLocalTransform,
	UControlRig* ControlRig, const FName& ControlName,
	const TArray<FFrameNumber>& Frames, const TArray<FTransform>& ParentTransforms, TArray<FTransform>& OutTransforms)
{
	if (Frames.Num() > ParentTransforms.Num())
	{
		UE_LOG(LogControlRig, Error, TEXT("Number of Frames %d to Snap greater  than Parent Frames %d"), Frames.Num(), ParentTransforms.Num());
		return false;
	}
	if (ControlRig->FindControl(ControlName) == nullptr)
	{
		UE_LOG(LogControlRig, Error, TEXT("Can not find Control %s"), *(ControlName.ToString()));
		return false;
	}
	if (UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene())
	{

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		OutTransforms.SetNum(Frames.Num());
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			const FFrameNumber& FrameNumber = Frames[Index];
			FFrameTime GlobalTime(FrameNumber);

			FMovieSceneContext Context = FMovieSceneContext(FMovieSceneEvaluationRange(GlobalTime, TickResolution), Player->GetPlaybackStatus()).SetHasJumped(true);

			Player->GetEvaluationTemplate().Evaluate(Context, *Player);
			ControlRig->Evaluate_AnyThread();
			OutTransforms[Index] = ControlRig->GetControlGlobalTransform(ControlName) * ParentTransforms[Index];
		}
	}
	return true;
}

bool FControlRigSnapper::GetControlRigControlTransforms(ISequencer* Sequencer,  UControlRig* ControlRig, const FName& ControlName,
	const TArray<FFrameNumber> &Frames, const TArray<FTransform>& ParentTransforms,TArray<FTransform>& OutTransforms)
{
	if (Sequencer->GetFocusedMovieSceneSequence())
	{
		FMovieSceneSequenceIDRef Template = Sequencer->GetFocusedTemplateID();
		FMovieSceneSequenceTransform RootToLocalTransform;
		return LocalGetControlRigControlTransforms(Sequencer, Sequencer->GetFocusedMovieSceneSequence(), Template, RootToLocalTransform,
			ControlRig, ControlName, Frames, ParentTransforms, OutTransforms);
	
	}
	return false;
}

bool FControlRigSnapper::GetControlRigControlTransforms(UWorld* World,ULevelSequence* LevelSequence, UControlRig* ControlRig, const FName& ControlName,
	const TArray<FFrameNumber>& Frames, const TArray<FTransform>& ParentTransforms, TArray<FTransform>& OutTransforms)
{
	if (LevelSequence)
	{
		ALevelSequenceActor* OutActor;
		FMovieSceneSequencePlaybackSettings Settings;
		FLevelSequenceCameraSettings CameraSettings;
		FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
		FMovieSceneSequenceTransform RootToLocalTransform;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, LevelSequence, Settings, OutActor);
		Player->Initialize(LevelSequence, World->PersistentLevel, Settings, CameraSettings);
		Player->State.AssignSequence(MovieSceneSequenceID::Root, *LevelSequence, *Player);
		bool Success = LocalGetControlRigControlTransforms(Player, LevelSequence, Template, RootToLocalTransform,
			ControlRig, ControlName, Frames, ParentTransforms, OutTransforms);
		World->DestroyActor(OutActor);
		return Success;
	}
	return false;
}

//Matinee version of this doesn't actually set the key it only adds..sigh
static void SetOrAddKey(TMovieSceneChannelData<FMovieSceneFloatValue>& ChannelData, FFrameNumber Time, float Value)
{
	int32 ExistingIndex = ChannelData.FindKey(Time);
	if (ExistingIndex != INDEX_NONE)
	{
		FMovieSceneFloatValue& FloatValue = ChannelData.GetValues()[ExistingIndex]; //-V758
		FloatValue.Value = Value;
	}
	else
	{
		FMovieSceneFloatValue NewKey(Value);
		ERichCurveTangentWeightMode WeightedMode = RCTWM_WeightedNone;
		NewKey.InterpMode = ERichCurveInterpMode::RCIM_Cubic;
		NewKey.TangentMode = ERichCurveTangentMode::RCTM_Auto;
		NewKey.Tangent.ArriveTangent = 0.0f;
		NewKey.Tangent.LeaveTangent = 0.0f;
		NewKey.Tangent.TangentWeightMode = WeightedMode;
		NewKey.Tangent.ArriveTangentWeight = 0.0f;
		NewKey.Tangent.LeaveTangentWeight = 0.0f;
		ChannelData.AddKey(Time, NewKey);
	}
}

struct FGuidAndActor
{
	FGuidAndActor(FGuid InGuid, AActor* InActor) : Guid(InGuid), Actor(InActor) {};
	FGuid Guid;
	AActor* Actor;

	bool SetTransform(ISequencer* Sequencer, const TArray<FFrameNumber>& Frames,
		const TArray<FTransform>& WorldTransformsToSnapTo, const UControlRigSnapSettings* SnapSettings)
	{
		if (!Sequencer || !Sequencer->GetFocusedMovieSceneSequence())
		{
			return false;
		}
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

		UMovieScene3DTransformTrack* TransformTrack = MovieScene->FindTrack<UMovieScene3DTransformTrack>(Guid);
		if (!TransformTrack)
		{
			MovieScene->Modify();
			TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Guid);
		}
		TransformTrack->Modify();

		bool bSectionAdded = false;
		UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->FindOrAddSection(0, bSectionAdded));
		if (!TransformSection)
		{
			return false;
		}

		TransformSection->Modify();

		if (bSectionAdded)
		{
			TransformSection->SetRange(TRange<FFrameNumber>::All());
		}

		TArray<FTransform> ParentWorldTransforms; 
		AActor* ParentActor = Actor->GetAttachParentActor();
		if (ParentActor)
		{
			FActorForWorldTransforms ActorSelection;
			ActorSelection.SocketName = Actor->GetAttachParentSocketName();
			MovieSceneToolHelpers::GetActorWorldTransforms(Sequencer, ActorSelection, Frames, ParentWorldTransforms);
		}
		else
		{
			ParentWorldTransforms.SetNumUninitialized(Frames.Num());
			for (FTransform& Transform : ParentWorldTransforms)
			{
				Transform = FTransform::Identity;
			}
		}

		TArrayView<FMovieSceneFloatChannel*> Channels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			const FFrameNumber& Frame = Frames[Index];
			FTransform ParentTransform = ParentWorldTransforms[Index];
			FTransform WorldTransform = WorldTransformsToSnapTo[Index];
			FTransform LocalTransform = WorldTransform.GetRelativeTransform(ParentTransform);
			FVector Location = LocalTransform.GetLocation();
			FRotator Rotation = LocalTransform.GetRotation().Rotator();
			FVector Scale3D = LocalTransform.GetScale3D();
			if (Index == 0) 
			{
				if (SnapSettings->bSnapPosition)
				{
					if (!Channels[0]->GetDefault().IsSet())
					{
						Channels[0]->SetDefault(Location.X);
					}
					if (!Channels[1]->GetDefault().IsSet())
					{
						Channels[1]->SetDefault(Location.Y);
					}
					if (!Channels[2]->GetDefault().IsSet())
					{
						Channels[2]->SetDefault(Location.Z);
					}
				}
				if (SnapSettings->bSnapRotation)
				{
					if (!Channels[3]->GetDefault().IsSet())
					{
						Channels[3]->SetDefault(Rotation.Yaw);
					}
					if (!Channels[4]->GetDefault().IsSet())
					{
						Channels[4]->SetDefault(Rotation.Pitch);
					}
					if (!Channels[5]->GetDefault().IsSet())
					{
						Channels[5]->SetDefault(Rotation.Roll);
					}
				}
				if (SnapSettings->bSnapScale)
				{
					if (!Channels[6]->GetDefault().IsSet())
					{
						Channels[6]->SetDefault(Scale3D.X);
					}
					if (!Channels[7]->GetDefault().IsSet())
					{
						Channels[7]->SetDefault(Scale3D.Y);
					}
					if (!Channels[8]->GetDefault().IsSet())
					{
						Channels[8]->SetDefault(Scale3D.Z);
					}
				}
			}

			if (SnapSettings->bSnapPosition)
			{
				TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channels[0]->GetData();
				SetOrAddKey(ChannelData, Frame, Location.X);
				ChannelData = Channels[1]->GetData();
				SetOrAddKey(ChannelData, Frame, Location.Y);
				ChannelData = Channels[2]->GetData();
				SetOrAddKey(ChannelData, Frame, Location.Z);
			}
			if (SnapSettings->bSnapRotation)
			{
				//todo winding
				TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channels[3]->GetData();
				SetOrAddKey(ChannelData, Frame, Rotation.Yaw);
				ChannelData = Channels[4]->GetData();
				SetOrAddKey(ChannelData, Frame, Rotation.Pitch);
				ChannelData = Channels[5]->GetData();
				SetOrAddKey(ChannelData, Frame, Rotation.Roll);
			}
			if (SnapSettings->bSnapScale)
			{
				TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channels[6]->GetData();
				SetOrAddKey(ChannelData, Frame, Scale3D.X);
				ChannelData = Channels[7]->GetData();
				SetOrAddKey(ChannelData, Frame, Scale3D.X);
				ChannelData = Channels[8]->GetData();
				SetOrAddKey(ChannelData, Frame, Scale3D.X);
			}		
		}
		//now we need to set auto tangents
		if (SnapSettings->bSnapPosition)
		{
			Channels[0]->AutoSetTangents();
			Channels[1]->AutoSetTangents();
			Channels[2]->AutoSetTangents();		
		}
		if (SnapSettings->bSnapRotation)
		{
			Channels[3]->AutoSetTangents();
			Channels[4]->AutoSetTangents();
			Channels[5]->AutoSetTangents();
		}
		if (SnapSettings->bSnapScale)
		{
			Channels[6]->AutoSetTangents();
			Channels[7]->AutoSetTangents();
			Channels[8]->AutoSetTangents();
		}
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

		return true;
	}
};

static void CalculateFramesToSnap(ISequencer* InSequencer, UMovieScene* MovieScene, FFrameNumber StartFrame, FFrameNumber EndFrame, TArray<FFrameNumber>& OutFrames)
{
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameRate DisplayResolution = MovieScene->GetDisplayRate();

	FFrameNumber StartTimeInDisplay = FFrameRate::TransformTime(FFrameTime(StartFrame), TickResolution, DisplayResolution).FloorToFrame();
	FFrameNumber EndTimeInDisplay = FFrameRate::TransformTime(FFrameTime(EndFrame), TickResolution, DisplayResolution).CeilToFrame();
	for (FFrameNumber DisplayFrameNumber = StartTimeInDisplay; DisplayFrameNumber <= EndTimeInDisplay; ++DisplayFrameNumber)
	{
		FFrameNumber TickFrameNumber = FFrameRate::TransformTime(FFrameTime(DisplayFrameNumber), DisplayResolution, TickResolution).FrameNumber;
		OutFrames.Add(TickFrameNumber);
	}
}

//returns true if world is calculated, false if there are no parents
static bool CalculateWorldTransformsFromParents(ISequencer* Sequencer, const FControlRigSnapperSelection& ParentToSnap,
	const TArray<FFrameNumber>& Frames, TArray<FTransform>& OutParentWorldTransforms)
{
	//just do first for now but may do average later.
	for (const FActorForWorldTransforms& ActorSelection : ParentToSnap.Actors)
	{
		if (ActorSelection.Actor.IsValid())
		{
			MovieSceneToolHelpers::GetActorWorldTransforms(Sequencer, ActorSelection, Frames, OutParentWorldTransforms);
			return true;
		}
	}

	for (const FControlRigForWorldTransforms& ControlRigAndSelection : ParentToSnap.ControlRigs)
	{
		//get actor transform...
		UControlRig* ControlRig = ControlRigAndSelection.ControlRig.Get();

		if (ControlRig)
		{
			if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
			{
				USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
				if (!Component)
				{
					continue;
				}
				AActor* Actor = Component->GetTypedOuter< AActor >();
				if (!Actor)
				{
					continue;
				}
				TArray<FTransform> ParentTransforms;
				FActorForWorldTransforms ActorSelection;
				ActorSelection.Actor = Actor;
				MovieSceneToolHelpers::GetActorWorldTransforms(Sequencer, ActorSelection, Frames, ParentTransforms);

				//just do first for now but may do average later.
				FControlRigSnapper Snapper;
				for (const FName& Name : ControlRigAndSelection.ControlNames)
				{
					Snapper.GetControlRigControlTransforms(Sequencer, ControlRig, Name, Frames, ParentTransforms, OutParentWorldTransforms);
					return true;
				}
			}
		}
	}
	OutParentWorldTransforms.SetNum(Frames.Num());
	for (FTransform& Transform : OutParentWorldTransforms)
	{
		Transform = FTransform::Identity;
	}
	return false;
}


bool FControlRigSnapper::SnapIt(FFrameNumber StartFrame, FFrameNumber EndFrame,const FControlRigSnapperSelection& ActorToSnap,
	const FControlRigSnapperSelection& ParentToSnap, const UControlRigSnapSettings* SnapSettings)
{
	TWeakPtr<ISequencer> InSequencer = GetSequencer();
	if (InSequencer.IsValid() && InSequencer.Pin()->GetFocusedMovieSceneSequence() && ActorToSnap.IsValid())
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("SnapAnimation", "Snap Animation"));

		ISequencer* Sequencer = InSequencer.Pin().Get();
		Sequencer->ForceEvaluate(); // force an evaluate so any control rig get's binding setup
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		MovieScene->Modify();
		
		TArray<FFrameNumber> Frames;
		CalculateFramesToSnap(Sequencer,MovieScene, StartFrame, EndFrame, Frames);

		TArray<FTransform> WorldTransformToSnap;
		bool bSnapToFirstFrameNotParents = !CalculateWorldTransformsFromParents(Sequencer, ParentToSnap, Frames, WorldTransformToSnap);
		if (Frames.Num() != WorldTransformToSnap.Num())
		{
			UE_LOG(LogControlRig, Error, TEXT("Number of Frames %d to Snap different than Parent Frames %d"), Frames.Num(),WorldTransformToSnap.Num());
			return false;
		}

		TArray<FGuidAndActor > ActorsToSnap;
		//There may be Actors here not in Sequencer so we add them to sequencer also
		for (const FActorForWorldTransforms& ActorSelection : ActorToSnap.Actors)
		{
			if (ActorSelection.Actor.IsValid())
			{
				AActor* Actor = ActorSelection.Actor.Get();
				FGuid ObjectHandle = Sequencer->GetHandleToObject(Actor,false);
				if (ObjectHandle.IsValid() == false)
				{
					TArray<TWeakObjectPtr<AActor> > ActorsToAdd;
					ActorsToAdd.Add(Actor);
					TArray<FGuid> ActorTracks = Sequencer->AddActors(ActorsToAdd, false);
					if (ActorTracks[0].IsValid())
					{
						ActorsToSnap.Add(FGuidAndActor(ActorTracks[0], Actor));
					}
				}
				else
				{
					ActorsToSnap.Add(FGuidAndActor(ObjectHandle, Actor));
				}
			}
		}
		//set transforms on these guys
		for (FGuidAndActor& GuidActor : ActorsToSnap)
		{
			if (bSnapToFirstFrameNotParents || SnapSettings->bKeepOffset) //if we are snapping to the first frame or keep offset we just don't set the parent transforms
			{
				TArray<FFrameNumber> OneFrame;
				OneFrame.Add(Frames[0]);
				TArray<FTransform> OneTransform;
				FActorForWorldTransforms ActorSelection;
				ActorSelection.Actor = GuidActor.Actor;
				MovieSceneToolHelpers::GetActorWorldTransforms(Sequencer, ActorSelection, OneFrame, OneTransform);
				if (bSnapToFirstFrameNotParents)
				{
					for (FTransform& Transform : WorldTransformToSnap)
					{
						Transform = OneTransform[0];
					}
				}
				else //we keep offset
				{
					FTransform FirstWorld = WorldTransformToSnap[0];
					for (FTransform& Transform : WorldTransformToSnap)
					{
						Transform = OneTransform[0].GetRelativeTransform(FirstWorld) * Transform;
					}
				}

			}
			GuidActor.SetTransform(Sequencer, Frames, WorldTransformToSnap,SnapSettings);
		}

		//Now do Control Rigs
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;

		for (const FControlRigForWorldTransforms& ControlRigAndSelection : ActorToSnap.ControlRigs)
		{
			//get actor transform...
			UControlRig* ControlRig = ControlRigAndSelection.ControlRig.Get();

			if (ControlRig)
			{
				if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
				{
					USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
					if (!Component)
					{
						continue;
					}
					AActor* Actor = Component->GetTypedOuter< AActor >();
					if (!Actor)
					{
						continue;
					}
					ControlRig->Modify();
					TArray<FTransform> ControlRigParentWorldTransforms;
					FActorForWorldTransforms ControlRigActorSelection;
					ControlRigActorSelection.Actor = Actor;
					MovieSceneToolHelpers::GetActorWorldTransforms(Sequencer, ControlRigActorSelection, Frames, ControlRigParentWorldTransforms);
					FControlRigSnapper Snapper;
					for (const FName& Name : ControlRigAndSelection.ControlNames)
					{
						TArray<FFrameNumber> OneFrame;
						OneFrame.SetNum(1);
						TArray<FTransform> CurrentControlRigTransform, CurrentParentWorldTransform;
						CurrentControlRigTransform.SetNum(1);
						CurrentParentWorldTransform.SetNum(1);
						if (bSnapToFirstFrameNotParents || SnapSettings->bKeepOffset)
						{
							OneFrame[0] = Frames[0];
							CurrentParentWorldTransform[0] = ControlRigParentWorldTransforms[0];
							Snapper.GetControlRigControlTransforms(Sequencer, ControlRig, Name, OneFrame, CurrentParentWorldTransform, CurrentControlRigTransform);
							if (bSnapToFirstFrameNotParents)
							{
								for (FTransform& Transform : WorldTransformToSnap)
								{
									Transform = CurrentControlRigTransform[0];
								}
							}
							else if (SnapSettings->bKeepOffset)
							{
								FTransform FirstWorld = WorldTransformToSnap[0];
								for (FTransform& Transform : WorldTransformToSnap)
								{
									Transform =  CurrentControlRigTransform[0].GetRelativeTransform(FirstWorld) * Transform;
								}
							}
						}

						for (int32 Index = 0; Index < WorldTransformToSnap.Num(); ++Index)
						{
							OneFrame[0] = Frames[Index];
							CurrentParentWorldTransform[0] = ControlRigParentWorldTransforms[Index];
							//this will evaluate at the current frame which we want
							GetControlRigControlTransforms(Sequencer, ControlRig, Name, OneFrame, CurrentParentWorldTransform, CurrentControlRigTransform);
							if (SnapSettings->bSnapPosition == false || SnapSettings->bSnapRotation == false || SnapSettings->bSnapScale == false)
							{
								FTransform& Transform = WorldTransformToSnap[Index];
								const FTransform& CurrentTransform = CurrentControlRigTransform[0];
								if (SnapSettings->bSnapPosition == false)
								{
									FVector CurrentPosition = CurrentTransform.GetLocation();
									Transform.SetLocation(CurrentPosition);
								}
								if (SnapSettings->bSnapRotation == false)
								{
									FQuat CurrentRotation = CurrentTransform.GetRotation();
									Transform.SetRotation(CurrentRotation);
								}
								if (SnapSettings->bSnapScale == false)
								{
									FVector Scale = CurrentTransform.GetScale3D();
									Transform.SetScale3D(Scale);
								}
								
							}
							const FFrameNumber& FrameNumber = Frames[Index];
							Context.LocalTime = TickResolution.AsSeconds(FFrameTime(FrameNumber));
							FTransform GlobalTransform = WorldTransformToSnap[Index].GetRelativeTransform(ControlRigParentWorldTransforms[Index]);
							ControlRig->SetControlGlobalTransform(Name, GlobalTransform, true, Context);
						}
					}
				}
			}
		}
	}
	return true;
}



#undef LOCTEXT_NAMESPACE


