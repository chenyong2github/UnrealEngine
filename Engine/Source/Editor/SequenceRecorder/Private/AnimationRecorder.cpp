// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationRecorder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Animation/AnimSequence.h"
#include "Misc/MessageDialog.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Editor.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimCompress.h"
#include "Animation/AnimCompress_BitwiseCompressOnly.h"
#include "SCreateAnimationDlg.h"
#include "AssetRegistryModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Animation/AnimationRecordingSettings.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "FAnimationRecorder"


static TAutoConsoleVariable<int32> CVarKeepNotifyAndCurvesOnAnimationRecord(
	TEXT("a.KeepNotifyAndCurvesOnAnimationRecord"),
	1,
	TEXT("If nonzero we keep anim notifies, curves and sync markers when animation recording, if 0 we discard them before recording."),
	ECVF_Default);

/////////////////////////////////////////////////////

FAnimationRecorder::FAnimationRecorder()
	: AnimationObject(nullptr)
	, bRecordLocalToWorld(false)
	, bAutoSaveAsset(false)
	, bRemoveRootTransform(true)
	, bCheckDeltaTimeAtBeginning(true)
	, InterpMode(ERichCurveInterpMode::RCIM_Linear)
	, TangentMode(ERichCurveTangentMode::RCTM_Auto)
	, AnimationSerializer(nullptr)
{
	SetSampleRateAndLength(FAnimationRecordingSettings::DefaultSampleRate, FAnimationRecordingSettings::DefaultMaximumLength);
}

FAnimationRecorder::~FAnimationRecorder()
{
	StopRecord(false);
}

void FAnimationRecorder::SetSampleRateAndLength(float SampleRateHz, float LengthInSeconds)
{
	if (SampleRateHz <= 0.f)
	{

		// invalid rate passed in, fall back to default
		SampleRateHz = FAnimationRecordingSettings::DefaultSampleRate;
	}

	if (LengthInSeconds <= 0.f)
	{
		// invalid length passed in, default to unbounded
		LengthInSeconds = FAnimationRecordingSettings::UnboundedMaximumLength;
	}

	IntervalTime = 1.0f / SampleRateHz;
	if (LengthInSeconds == FAnimationRecordingSettings::UnboundedMaximumLength)
	{
		// invalid length passed in, default to unbounded
		MaxFrame = UnBoundedFrameCount;
	}
	else
	{
		MaxFrame = SampleRateHz * LengthInSeconds;
	}
}

bool FAnimationRecorder::SetAnimCompressionScheme(UAnimBoneCompressionSettings* Settings)
{
	if (AnimationObject)
	{
		if (Settings == nullptr)
		{
			// The caller has not supplied a settings asset, use our default value
			Settings = FAnimationUtils::GetDefaultAnimationRecorderBoneCompressionSettings();
		}

		AnimationObject->BoneCompressionSettings = Settings;
		return true;
	}

	return false;
}

// Internal. Pops up a dialog to get saved asset path
static bool PromptUserForAssetPath(FString& AssetPath, FString& AssetName)
{
	TSharedRef<SCreateAnimationDlg> NewAnimDlg = SNew(SCreateAnimationDlg);
	if (NewAnimDlg->ShowModal() != EAppReturnType::Cancel)
	{
		AssetPath = NewAnimDlg->GetFullAssetPath();
		AssetName = NewAnimDlg->GetAssetName();
		return true;
	}

	return false;
}

bool FAnimationRecorder::TriggerRecordAnimation(USkeletalMeshComponent* Component)
{
	FString AssetPath;
	FString AssetName;

	if (!Component || !Component->SkeletalMesh || !Component->SkeletalMesh->GetSkeleton())
	{
		return false;
	}

	// ask for path
	if (PromptUserForAssetPath(AssetPath, AssetName))
	{
		return TriggerRecordAnimation(Component, AssetPath, AssetName);
	}

	return false;
}

bool FAnimationRecorder::TriggerRecordAnimation(USkeletalMeshComponent* Component, const FString& InAssetPath, const FString& InAssetName)
{
	if (!Component || !Component->SkeletalMesh || !Component->SkeletalMesh->GetSkeleton())
	{
		return false;
	}

	// create the asset
	FText InvalidPathReason;
	bool const bValidPackageName = FPackageName::IsValidLongPackageName(InAssetPath, false, &InvalidPathReason);
	if (bValidPackageName == false)
	{
		UE_LOG(LogAnimation, Log, TEXT("%s is an invalid asset path, prompting user for new asset path. Reason: %s"), *InAssetPath, *InvalidPathReason.ToString());
	}

	FString ValidatedAssetPath = InAssetPath;
	FString ValidatedAssetName = InAssetName;

	UObject* Parent = bValidPackageName ? CreatePackage( *ValidatedAssetPath) : nullptr;
	if (Parent == nullptr)
	{
		// bad or no path passed in, do the popup
		if (PromptUserForAssetPath(ValidatedAssetPath, ValidatedAssetName) == false)
		{
			return false;
		}
		
		Parent = CreatePackage( *ValidatedAssetPath);
	}

	UObject* const Object = LoadObject<UObject>(Parent, *ValidatedAssetName, nullptr, LOAD_Quiet, nullptr);
	// if object with same name exists, warn user
	if (Object)
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_AssetExist", "Asset with same name exists. Can't overwrite another asset"));
		return false;		// failed
	}

	// If not, create new one now.
	UAnimSequence* const NewSeq = NewObject<UAnimSequence>(Parent, *ValidatedAssetName, RF_Public | RF_Standalone);
	if (NewSeq)
	{
		// set skeleton
		NewSeq->SetSkeleton(Component->SkeletalMesh->GetSkeleton());
		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(NewSeq);
		StartRecord(Component, NewSeq);

		return true;
	}

	return false;
}

/** Helper function to get space bases depending on master pose component */
void FAnimationRecorder::GetBoneTransforms(USkeletalMeshComponent* Component, TArray<FTransform>& BoneTransforms)
{
	const USkinnedMeshComponent* const MasterPoseComponentInst = Component->MasterPoseComponent.Get();
	if(MasterPoseComponentInst)
	{
		const TArray<FTransform>& SpaceBases = MasterPoseComponentInst->GetComponentSpaceTransforms();
		BoneTransforms.Reset(BoneTransforms.Num());
		BoneTransforms.AddUninitialized(SpaceBases.Num());
		for(int32 BoneIndex = 0; BoneIndex < SpaceBases.Num(); BoneIndex++)
		{
			if(BoneIndex < Component->GetMasterBoneMap().Num())
			{
				int32 MasterBoneIndex = Component->GetMasterBoneMap()[BoneIndex];

				// If ParentBoneIndex is valid, grab matrix from MasterPoseComponent.
				if(MasterBoneIndex != INDEX_NONE && MasterBoneIndex < SpaceBases.Num())
				{
					BoneTransforms[BoneIndex] = SpaceBases[MasterBoneIndex];
				}
				else
				{
					BoneTransforms[BoneIndex] = FTransform::Identity;
				}
			}
			else
			{
				BoneTransforms[BoneIndex] = FTransform::Identity;
			}
		}
	}
	else
	{
		BoneTransforms = Component->GetComponentSpaceTransforms();
	}
}


void FAnimationRecorder::StartRecord(USkeletalMeshComponent* Component, UAnimSequence* InAnimationObject)
{
	TimePassed = 0.f;
	AnimationObject = InAnimationObject;

	const bool bKeepNotifiesAndCurves = CVarKeepNotifyAndCurvesOnAnimationRecord->GetInt() == 0 ? false : true;
	if (bKeepNotifiesAndCurves)
	{
		AnimationObject->CleanAnimSequenceForImport();
	}
	else
	{
		AnimationObject->RecycleAnimSequence();
	}
	AnimationObject->BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationRecorderBoneCompressionSettings();

	FAnimationRecorder::GetBoneTransforms(Component, PreviousSpacesBases);
	PreviousAnimCurves = Component->GetAnimationCurves();
	PreviousComponentToWorld = Component->GetComponentTransform();

	LastFrame = 0;
	AnimationObject->SequenceLength = 0.f;
	AnimationObject->SetRawNumberOfFrame(0);

	RecordedCurves.Reset();
	RecordedTimes.Empty();
	UIDToArrayIndexLUT = nullptr;

	USkeleton* AnimSkeleton = AnimationObject->GetSkeleton();
	// add all frames
	for (int32 BoneIndex=0; BoneIndex <PreviousSpacesBases.Num(); ++BoneIndex)
	{
		// verify if this bone exists in skeleton
		const int32 BoneTreeIndex = AnimSkeleton->GetSkeletonBoneIndexFromMeshBoneIndex(Component->MasterPoseComponent != nullptr ? Component->MasterPoseComponent->SkeletalMesh : Component->SkeletalMesh, BoneIndex);
		if (BoneTreeIndex != INDEX_NONE)
		{
			// add tracks for the bone existing
			FName BoneTreeName = AnimSkeleton->GetReferenceSkeleton().GetBoneName(BoneTreeIndex);
			AnimationObject->AddNewRawTrack(BoneTreeName);
		}
	}

	AnimationObject->RetargetSource = Component->SkeletalMesh ? AnimSkeleton->GetRetargetSourceForMesh(Component->SkeletalMesh) : NAME_None;

	// init notifies
	AnimationObject->InitializeNotifyTrack();
	// record the first frame
	Record(Component, PreviousComponentToWorld, PreviousSpacesBases, PreviousAnimCurves,  0);
}

void FAnimationRecorder::FixupNotifies()
{
	if (AnimationObject)
	{
		// build notify tracks - first find how many tracks we want
		for (FAnimNotifyEvent& Event : AnimationObject->Notifies)
		{
			if (Event.TrackIndex >= AnimationObject->AnimNotifyTracks.Num())
			{
				AnimationObject->AnimNotifyTracks.SetNum(Event.TrackIndex + 1);

				// remake track names to create a nice sequence
				const int32 TrackNum = AnimationObject->AnimNotifyTracks.Num();
				for (int32 TrackIndex = 0; TrackIndex < TrackNum; ++TrackIndex)
				{
					FAnimNotifyTrack& Track = AnimationObject->AnimNotifyTracks[TrackIndex];
					Track.TrackName = *FString::FromInt(TrackIndex + 1);
				}
			}
		}

		// now build tracks
		for (int32 EventIndex = 0; EventIndex < AnimationObject->Notifies.Num(); ++EventIndex)
		{
			FAnimNotifyEvent& Event = AnimationObject->Notifies[EventIndex];
			AnimationObject->AnimNotifyTracks[Event.TrackIndex].Notifies.Add(&AnimationObject->Notifies[EventIndex]);
		}
	}
}

UAnimSequence* FAnimationRecorder::StopRecord(bool bShowMessage)
{
	double StartTime, ElapsedTime = 0;

	if (AnimationObject)
	{
		int32 NumFrames = LastFrame  + 1;
		AnimationObject->SetRawNumberOfFrame(NumFrames);

		// can't use TimePassed. That is just total time that has been passed, not necessarily match with frame count
		AnimationObject->SequenceLength = (NumFrames>1) ? (NumFrames-1) * IntervalTime : MINIMUM_ANIMATION_LENGTH;

		FixupNotifies();

		// post-process applies compression etc.
		// @todo figure out why removing redundant keys is inconsistent

		// add to real curve data 
		if (RecordedCurves.Num() == NumFrames && UIDToArrayIndexLUT)
		{
			StartTime = FPlatformTime::Seconds();

			USkeleton* SkeletonObj = AnimationObject->GetSkeleton();
			for (int32 CurveUID = 0; CurveUID < UIDToArrayIndexLUT->Num(); ++CurveUID)
			{
				int32 CurveIndex = (*UIDToArrayIndexLUT)[CurveUID];

				if (CurveIndex != MAX_uint16)
				{
					FFloatCurve* FloatCurveData = nullptr;

					TArray<float> TimesToRecord;
					TArray<float> ValuesToRecord;
					TimesToRecord.SetNum(NumFrames);
					ValuesToRecord.SetNum(NumFrames);

					bool bSeenThisCurve = false;
					int32 WriteIndex = 0;
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
					{
						const float TimeToRecord = FrameIndex*IntervalTime;
						if(RecordedCurves[FrameIndex].ValidCurveWeights[CurveIndex])
						{
							float CurCurveValue = RecordedCurves[FrameIndex].CurveWeights[CurveIndex];
							if (!bSeenThisCurve)
							{
								bSeenThisCurve = true;

								// add one and save the cache
								FSmartName CurveName;
								if (SkeletonObj->GetSmartNameByUID(USkeleton::AnimCurveMappingName, CurveUID, CurveName))
								{
									// give default curve flag for recording 
									AnimationObject->RawCurveData.AddFloatCurveKey(CurveName, AACF_DefaultCurve, TimeToRecord, CurCurveValue);
									FloatCurveData = static_cast<FFloatCurve*>(AnimationObject->RawCurveData.GetCurveData(CurveUID, ERawCurveTrackTypes::RCT_Float));
								}
							}

							if (FloatCurveData)
							{
								TimesToRecord[WriteIndex] = TimeToRecord;
								ValuesToRecord[WriteIndex] = CurCurveValue;

								++WriteIndex;
							}
						}
					}

					// Fill all the curve data at once
					if (FloatCurveData)
					{
						TArray<FRichCurveKey> Keys;
						for (int32 Index = 0; Index < WriteIndex; ++Index)
						{
							FRichCurveKey Key(TimesToRecord[Index], ValuesToRecord[Index]);
							Key.InterpMode = InterpMode;
							Key.TangentMode = TangentMode;
							Keys.Add(Key);
						}

						FloatCurveData->FloatCurve.SetKeys(Keys);
					}
				}
			}	

			ElapsedTime = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogAnimation, Log, TEXT("Animation Recorder set keys in %0.02f seconds"), ElapsedTime);
		}

		//AnimationObject->RawCurveData.RemoveRedundantKeys();
		AnimationObject->PostProcessSequence();

		AnimationObject->MarkPackageDirty();
		
		// save the package to disk, for convenience and so we can run this in standalone mode
		if (bAutoSaveAsset)
		{
			UPackage* const Package = AnimationObject->GetOutermost();
			FString const PackageName = Package->GetName();
			FString const PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			
			StartTime = FPlatformTime::Seconds();

			UPackage::SavePackage(Package, NULL, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_NoError);

			ElapsedTime = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogAnimation, Log, TEXT("Animation Recorder saved %s in %0.2f seconds"), *PackageName, ElapsedTime);
		}

		UAnimSequence* ReturnObject = AnimationObject;

		// notify to user
		if (bShowMessage)
		{
			const FText NotificationText = FText::Format(LOCTEXT("RecordAnimation", "'{0}' has been successfully recorded [{1} frames : {2} sec(s) @ {3} Hz]"),
				FText::FromString(AnimationObject->GetName()),
				FText::AsNumber(AnimationObject->GetRawNumberOfFrames()),
				FText::AsNumber(AnimationObject->SequenceLength),
				FText::AsNumber(1.f / IntervalTime)
				);
					
			if (GIsEditor)
			{
				FNotificationInfo Info(NotificationText);
				Info.ExpireDuration = 8.0f;
				Info.bUseLargeFont = false;
				Info.Hyperlink = FSimpleDelegate::CreateLambda([=]()
				{
					TArray<UObject*> Assets;
					Assets.Add(ReturnObject);
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(Assets);
				});
				Info.HyperlinkText = FText::Format(LOCTEXT("OpenNewAnimationHyperlink", "Open {0}"), FText::FromString(AnimationObject->GetName()));
				TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
				if ( Notification.IsValid() )
				{
					Notification->SetCompletionState( SNotificationItem::CS_Success );
				}
			}

			FAssetRegistryModule::AssetCreated(AnimationObject);
		}

		AnimationObject = NULL;
		PreviousSpacesBases.Empty();
		PreviousAnimCurves.Empty();

		return ReturnObject;
	}

	UniqueNotifies.Empty();
	UniqueNotifyStates.Empty();

	return NULL;
}

void FAnimationRecorder::ProcessRecordedTimes(UAnimSequence* AnimSequence, USkeletalMeshComponent* SkeletalMeshComponent, const FString& HoursName, const FString& MinutesName, const FString& SecondsName, const FString& FramesName, const FString& SubFramesName, const FString& SlateName, const FString& Slate)
{
	if (!AnimSequence || !SkeletalMeshComponent)
	{
		return;
	}

	int32 NumFrames = LastFrame  + 1;
	if (RecordedTimes.Num() != NumFrames)
	{
		return;
	}

	TArray<int32> Hours, Minutes, Seconds, Frames;
	TArray<float> SubFrames;
	TArray<float> Times;

	Hours.Reserve(RecordedTimes.Num());
	Minutes.Reserve(RecordedTimes.Num());
	Seconds.Reserve(RecordedTimes.Num());
	Frames.Reserve(RecordedTimes.Num());
	SubFrames.Reserve(RecordedTimes.Num());
	Times.Reserve(RecordedTimes.Num());

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		const float TimeToRecord = FrameIndex*IntervalTime;

		FQualifiedFrameTime RecordedTime = RecordedTimes[FrameIndex];
		FTimecode Timecode = FTimecode::FromFrameNumber(RecordedTime.Time.FrameNumber, RecordedTime.Rate);
		
		Hours.Add(Timecode.Hours);
		Minutes.Add(Timecode.Minutes);
		Seconds.Add(Timecode.Seconds);
		Frames.Add(Timecode.Frames);

		float SubFrame = RecordedTime.Time.GetSubFrame();
		SubFrames.Add(SubFrame);

		Times.Add(TimeToRecord);
	}

	Hours.Shrink();
	Minutes.Shrink();
	Seconds.Shrink();
	Frames.Shrink();
	SubFrames.Shrink();
	Times.Shrink();
	
	USkeleton* AnimSkeleton = AnimSequence->GetSkeleton();

	const USkinnedMeshComponent* const MasterPoseComponentInst = SkeletalMeshComponent->MasterPoseComponent.Get();
	const TArray<FTransform>* SpaceBases;
	if (MasterPoseComponentInst)
	{
		SpaceBases = &MasterPoseComponentInst->GetComponentSpaceTransforms();
	}
	else
	{
		SpaceBases = &SkeletalMeshComponent->GetComponentSpaceTransforms();
	}

	// String is not animatable, just add 1 slate at the first key time
	TArray<FString> Slates(&Slate, 1);
	TArray<float> SlateTimes(&Times[0], 1);

	for (int32 BoneIndex = 0; BoneIndex < SpaceBases->Num(); ++BoneIndex)
	{
		// verify if this bone exists in skeleton
		const int32 BoneTreeIndex = AnimSkeleton->GetSkeletonBoneIndexFromMeshBoneIndex(SkeletalMeshComponent->MasterPoseComponent != nullptr ? SkeletalMeshComponent->MasterPoseComponent->SkeletalMesh : SkeletalMeshComponent->SkeletalMesh, BoneIndex);
		if (BoneTreeIndex != INDEX_NONE)
		{
			// add tracks for the bone existing
			FName BoneTreeName = AnimSkeleton->GetReferenceSkeleton().GetBoneName(BoneTreeIndex);
			
			AnimSequence->AddBoneIntegerCustomAttribute(BoneTreeName, FName(*HoursName), Times, Hours);
			AnimSequence->AddBoneIntegerCustomAttribute(BoneTreeName, FName(*MinutesName), Times, Minutes);
			AnimSequence->AddBoneIntegerCustomAttribute(BoneTreeName, FName(*SecondsName), Times, Seconds);
			AnimSequence->AddBoneIntegerCustomAttribute(BoneTreeName, FName(*FramesName), Times, Frames);
			AnimSequence->AddBoneFloatCustomAttribute(BoneTreeName, FName(*SubFramesName), Times, SubFrames);

			AnimSequence->AddBoneStringCustomAttribute(BoneTreeName, FName(*SlateName), SlateTimes, Slates);
		}
	}
}

void FAnimationRecorder::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (AnimationObject)
	{
		Collector.AddReferencedObject(AnimationObject);
	}
}

void FAnimationRecorder::UpdateRecord(USkeletalMeshComponent* Component, float DeltaTime)
{
	// if no animation object, return
	if (!AnimationObject || !Component)
	{
		return;
	}

	// no sim time, no record
	if (DeltaTime <= 0.f)
	{
		return;
	}

	// Take Recorder will turn this off, not sure if it's needed for persona animation recording or not.
	if (bCheckDeltaTimeAtBeginning)
	{
		// in-editor we can get a long frame update because of the modal dialog used to pick paths
		if (DeltaTime > IntervalTime && (LastFrame == 0 || LastFrame == 1))
		{
			DeltaTime = IntervalTime;
		}
	}

	float const PreviousTimePassed = TimePassed;
	TimePassed += DeltaTime;

	// time passed has been updated
	// now find what frames we need to update
	int32 FramesRecorded = LastFrame;
	int32 FramesToRecord = FPlatformMath::TruncToInt(TimePassed / IntervalTime);

	// notifies need to be done regardless of sample rate
	if (Component->GetAnimInstance())
	{
		RecordNotifies(Component, Component->GetAnimInstance()->NotifyQueue.AnimNotifies, DeltaTime, TimePassed);
	}

	TArray<FTransform> SpaceBases;
	FAnimationRecorder::GetBoneTransforms(Component, SpaceBases);

	if (FramesRecorded < FramesToRecord)
	{
		const FBlendedHeapCurve& AnimCurves = Component->GetAnimationCurves();

		check(SpaceBases.Num() == PreviousSpacesBases.Num());

		TArray<FTransform> BlendedSpaceBases;
		BlendedSpaceBases.AddZeroed(SpaceBases.Num());

		UE_LOG(LogAnimation, Log, TEXT("DeltaTime : %0.2f, Current Frame Count : %d, Frames To Record : %d, TimePassed : %0.2f"), DeltaTime
			, FramesRecorded, FramesToRecord, TimePassed);

		// if we need to record frame
		while (FramesToRecord > FramesRecorded)
		{
			// find what frames we need to record
			// convert to time
			float CurrentTime = (FramesRecorded + 1) * IntervalTime;
			float BlendAlpha = (CurrentTime - PreviousTimePassed) / DeltaTime;

			UE_LOG(LogAnimation, Log, TEXT("Current Frame Count : %d, BlendAlpha : %0.2f"), FramesRecorded + 1, BlendAlpha);

			// for now we just concern component space, not skeleton space
			for (int32 BoneIndex = 0; BoneIndex<SpaceBases.Num(); ++BoneIndex)
			{
				BlendedSpaceBases[BoneIndex].Blend(PreviousSpacesBases[BoneIndex], SpaceBases[BoneIndex], BlendAlpha);
			}

			FTransform BlendedComponentToWorld;
			BlendedComponentToWorld.Blend(PreviousComponentToWorld, Component->GetComponentTransform(), BlendAlpha);

			FBlendedHeapCurve BlendedCurve;
			if (AnimCurves.CurveWeights.Num() > 0 && PreviousAnimCurves.CurveWeights.Num() == AnimCurves.CurveWeights.Num() && PreviousAnimCurves.IsValid() && AnimCurves.IsValid())
			{
				BlendedCurve.Lerp(PreviousAnimCurves, AnimCurves, BlendAlpha);
			}
			else
			{
				// just override with AnimCurves for this frames, because UID list has changed
				// which means new curves are added in run-time
				BlendedCurve = AnimCurves;
			}

			if (!Record(Component, BlendedComponentToWorld, BlendedSpaceBases, BlendedCurve, FramesRecorded + 1))
			{
				StopRecord(true);
				return;
			}
			++FramesRecorded;
		}
	}

	//save to current transform
	PreviousSpacesBases = SpaceBases;
	PreviousAnimCurves = Component->GetAnimationCurves();
	PreviousComponentToWorld = Component->GetComponentTransform();

	// if we passed MaxFrame, just stop it
	if (MaxFrame != UnBoundedFrameCount && FramesRecorded >= MaxFrame)
	{
		UE_LOG(LogAnimation, Log, TEXT("Animation Recording exceeds the time limited (%d mins). Stopping recording animation... "), (int32)((float)MaxFrame / ((1.0f / IntervalTime) * 60.0f)));
		StopRecord(true);
	}
}

bool FAnimationRecorder::Record(USkeletalMeshComponent* Component, FTransform const& ComponentToWorld, const TArray<FTransform>& SpacesBases, const FBlendedHeapCurve& AnimationCurves, int32 FrameToAdd)
{
	if (ensure(AnimationObject))
	{
		USkeletalMesh* SkeletalMesh = Component->MasterPoseComponent != nullptr ? Component->MasterPoseComponent->SkeletalMesh : Component->SkeletalMesh;

		if (FrameToAdd == 0)
		{
			// Find the root bone & store its transform
			SkeletonRootIndex = INDEX_NONE;
			USkeleton* AnimSkeleton = AnimationObject->GetSkeleton();
			for (int32 TrackIndex = 0; TrackIndex < AnimationObject->GetRawAnimationData().Num(); ++TrackIndex)
			{
				// verify if this bone exists in skeleton
				int32 BoneTreeIndex = AnimationObject->GetSkeletonIndexFromRawDataTrackIndex(TrackIndex);
				if (BoneTreeIndex != INDEX_NONE)
				{
					int32 BoneIndex = AnimSkeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkeletalMesh, BoneTreeIndex);
					int32 ParentIndex = SkeletalMesh->GetRefSkeleton().GetParentIndex(BoneIndex);
					FTransform LocalTransform = SpacesBases[BoneIndex];
					if (ParentIndex == INDEX_NONE)
					{
						if (bRemoveRootTransform && AnimationObject->GetRawAnimationData().Num() > 1)
						{
							// Store initial root transform.
							// We remove the initial transform of the root bone and transform root's children
							// to remove any offset. We need to do this for sequence recording in particular
							// as we use root motion to build transform tracks that properly sync with
							// animation keyframes. If we have a transformed root bone then the assumptions 
							// we make about root motion use are incorrect.
							// NEW. But we don't do this if there is just one root bone. This has come up with recording
							// single bone props and cameras.
							InitialRootTransform = LocalTransform;
							InvInitialRootTransform = LocalTransform.Inverse();
						}
						else
						{
							InitialRootTransform = InvInitialRootTransform = FTransform::Identity;
						}
						SkeletonRootIndex = BoneIndex;
						break;
					}
				}
			}
		}

		FSerializedAnimation  SerializedAnimation;
		USkeleton* AnimSkeleton = AnimationObject->GetSkeleton();
		for (int32 TrackIndex = 0; TrackIndex < AnimationObject->GetRawAnimationData().Num(); ++TrackIndex)
		{
			// verify if this bone exists in skeleton
			int32 BoneTreeIndex = AnimationObject->GetSkeletonIndexFromRawDataTrackIndex(TrackIndex);
			if (BoneTreeIndex != INDEX_NONE)
			{
				int32 BoneIndex = AnimSkeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkeletalMesh, BoneTreeIndex);
				int32 ParentIndex = SkeletalMesh->GetRefSkeleton().GetParentIndex(BoneIndex);
				FTransform LocalTransform = SpacesBases[BoneIndex];
				if ( ParentIndex != INDEX_NONE )
				{
					LocalTransform.SetToRelativeTransform(SpacesBases[ParentIndex]);
				}
				// if record local to world, we'd like to consider component to world to be in root
				else
				{
					if (bRecordLocalToWorld)
					{
						LocalTransform *= ComponentToWorld;
					}
				}

				FRawAnimSequenceTrack& RawTrack = AnimationObject->GetRawAnimationTrack(TrackIndex);
				if (bRecordTransforms)
				{
					RawTrack.PosKeys.Add(LocalTransform.GetTranslation());
					RawTrack.RotKeys.Add(LocalTransform.GetRotation());
					RawTrack.ScaleKeys.Add(LocalTransform.GetScale3D());
					if (AnimationSerializer)
					{
						SerializedAnimation.AddTransform(TrackIndex, LocalTransform);
					}
				}
				// verification
				if (FrameToAdd != RawTrack.PosKeys.Num()-1)
				{
					UE_LOG(LogAnimation, Warning, TEXT("Mismatch in animation frames. Trying to record frame: %d, but only: %d frame(s) exist. Changing skeleton while recording is not supported."), FrameToAdd, RawTrack.PosKeys.Num());
					return false;
				}
			}
		}

		TOptional<FQualifiedFrameTime> CurrentTime = FApp::GetCurrentFrameTime();
		RecordedTimes.Add(CurrentTime.IsSet() ? CurrentTime.GetValue() : FQualifiedFrameTime());

		if (AnimationSerializer)
		{
			AnimationSerializer->WriteFrameData(AnimationSerializer->FramesWritten, SerializedAnimation);
		}
		// each RecordedCurves contains all elements
		if (bRecordCurves && AnimationCurves.CurveWeights.Num() > 0)
		{
			RecordedCurves.Emplace(AnimationCurves.CurveWeights, AnimationCurves.ValidCurveWeights);
			if (UIDToArrayIndexLUT == nullptr)
			{
				UIDToArrayIndexLUT = AnimationCurves.UIDToArrayIndexLUT;
			}
			else
			{
				ensureAlways(UIDToArrayIndexLUT->Num() == AnimationCurves.UIDToArrayIndexLUT->Num());
				if (UIDToArrayIndexLUT != AnimationCurves.UIDToArrayIndexLUT)
				{
					UIDToArrayIndexLUT = AnimationCurves.UIDToArrayIndexLUT;
				}
			}
		}

		LastFrame = FrameToAdd;
	}

	return true;
}

void FAnimationRecorder::RecordNotifies(USkeletalMeshComponent* Component, const TArray<FAnimNotifyEventReference>& AnimNotifies, float DeltaTime, float RecordTime)
{
	if (ensure(AnimationObject))
	{
		// flag notifies as possibly unused this frame
		for (auto& ActiveNotify : ActiveNotifies)
		{
			ActiveNotify.Value = false;
		}

		int32 AddedThisFrame = 0;
		for(const FAnimNotifyEventReference& NotifyEventRef : AnimNotifies)
		{
			if(const FAnimNotifyEvent* NotifyEvent = NotifyEventRef.GetNotify())
			{
				// we don't want to insert notifies with duration more than once
				if(NotifyEvent->GetDuration() > 0.0f)
				{
					// if this event is active already then don't add it
					bool bAlreadyActive = false;
					for (auto& ActiveNotify : ActiveNotifies)
					{
						if(NotifyEvent == ActiveNotify.Key)
						{
							// flag as active
							ActiveNotify.Value = true;
							bAlreadyActive = true;
							break;
						}
					}

					// already active, so skip adding
					if(bAlreadyActive)
					{
						continue;
					}
					else
					{
						// add a new active notify with duration
						ActiveNotifies.Emplace(NotifyEvent, true);
					}
				}

				// make a new notify from this event & set the current time
				FAnimNotifyEvent NewEvent = *NotifyEvent;
				NewEvent.SetTime(RecordTime);
				NewEvent.TriggerTimeOffset = 0.0f;
				NewEvent.EndTriggerTimeOffset = 0.0f;

				// see if we need to create a new notify
				if(NotifyEvent->Notify)
				{
					UAnimNotify** FoundNotify = UniqueNotifies.Find(NotifyEvent->Notify);
					if(FoundNotify == nullptr)
					{
						NewEvent.Notify = Cast<UAnimNotify>(StaticDuplicateObject(NewEvent.Notify, AnimationObject));
						UniqueNotifies.Add(NotifyEvent->Notify, NewEvent.Notify);
					}
					else
					{
						NewEvent.Notify = *FoundNotify;
					}
				}

				// see if we need to create a new notify state
				if (NotifyEvent->NotifyStateClass)
				{
					UAnimNotifyState** FoundNotifyState = UniqueNotifyStates.Find(NotifyEvent->NotifyStateClass);
					if (FoundNotifyState == nullptr)
					{
						NewEvent.NotifyStateClass = Cast<UAnimNotifyState>(StaticDuplicateObject(NewEvent.NotifyStateClass, AnimationObject));
						UniqueNotifyStates.Add(NotifyEvent->NotifyStateClass, NewEvent.NotifyStateClass);
					}
					else
					{
						NewEvent.NotifyStateClass = *FoundNotifyState;
					}
				}

				AnimationObject->Notifies.Add(NewEvent);
				AddedThisFrame++;
			}
		}

		// remove all notifies that didnt get added this time
		ActiveNotifies.RemoveAll([](TPair<const FAnimNotifyEvent*, bool>& ActiveNotify){ return !ActiveNotify.Value; });

		UE_LOG(LogAnimation, Log, TEXT("Added notifies : %d"), AddedThisFrame);
	}
}

void FAnimationRecorderManager::Tick(float DeltaTime)
{
	for (auto& Inst : RecorderInstances)
	{
		Inst.Update(DeltaTime);
	}
}

void FAnimationRecorderManager::Tick(USkeletalMeshComponent* Component, float DeltaTime)
{
	for (auto& Inst : RecorderInstances)
	{
		if(Inst.SkelComp == Component)
		{
			Inst.Update(DeltaTime);
		}
	}
}

FAnimationRecorderManager::FAnimationRecorderManager()
{
}

FAnimationRecorderManager::~FAnimationRecorderManager()
{
}

FAnimationRecorderManager& FAnimationRecorderManager::Get()
{
	static FAnimationRecorderManager AnimRecorderManager;
	return AnimRecorderManager;
}


FAnimRecorderInstance::FAnimRecorderInstance()
	: SkelComp(nullptr)
	, Recorder(nullptr)
	, CachedVisibilityBasedAnimTickOption(EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
	, bCachedEnableUpdateRateOptimizations(false)
{
}

void FAnimRecorderInstance::Init(USkeletalMeshComponent* InComponent, const FString& InAssetPath, const FString& InAssetName, const FAnimationRecordingSettings& Settings)
{
	AssetPath = InAssetPath;
	AssetName = InAssetName;

	InitInternal(InComponent, Settings);
}

void FAnimRecorderInstance::Init(USkeletalMeshComponent* InComponent, UAnimSequence* InSequence, FAnimationSerializer *InAnimationSerializer, const FAnimationRecordingSettings& Settings)
{
	Sequence = InSequence;
	InitInternal(InComponent, Settings,InAnimationSerializer);
}

void FAnimRecorderInstance::InitInternal(USkeletalMeshComponent* InComponent, const FAnimationRecordingSettings& Settings, FAnimationSerializer *InAnimationSerializer)
{
	SkelComp = InComponent;
	Recorder = MakeShareable(new FAnimationRecorder());
	Recorder->SetSampleRateAndLength(Settings.SampleRate, Settings.Length);
	Recorder->bRecordLocalToWorld = Settings.bRecordInWorldSpace;
	Recorder->InterpMode = Settings.InterpMode;
	Recorder->TangentMode = Settings.TangentMode;
	Recorder->bAutoSaveAsset = Settings.bAutoSaveAsset;
	Recorder->bRemoveRootTransform = Settings.bRemoveRootAnimation;
	Recorder->bCheckDeltaTimeAtBeginning = Settings.bCheckDeltaTimeAtBeginning;
	Recorder->AnimationSerializer = InAnimationSerializer;
	Recorder->bRecordTransforms = Settings.bRecordTransforms;
	Recorder->bRecordCurves = Settings.bRecordCurves;

	if (InComponent)
	{
		CachedSkelCompForcedLodModel = InComponent->GetForcedLOD();
		InComponent->SetForcedLOD(1);

		// turn off URO and make sure we always update even if out of view
		bCachedEnableUpdateRateOptimizations = InComponent->bEnableUpdateRateOptimizations;
		CachedVisibilityBasedAnimTickOption = InComponent->VisibilityBasedAnimTickOption;

		InComponent->bEnableUpdateRateOptimizations = false;
		InComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

FAnimRecorderInstance::~FAnimRecorderInstance()
{
}

bool FAnimRecorderInstance::BeginRecording()
{
	if (SkelComp.IsValid() == false)
	{
		UE_LOG(LogAnimation, Log, TEXT("Animation Recorder:  Begin Recording: SkelMeshComp not Valid, No Recording will occur."));
		return false;
	}
	if (Recorder.IsValid())
	{
		if (Sequence.IsValid())
		{
			Recorder->StartRecord(SkelComp.Get(), Sequence.Get());
			return true;
		}
		else
		{
			return Recorder->TriggerRecordAnimation(SkelComp.Get(), AssetPath, AssetName);
		}
	}

	UE_LOG(LogAnimation, Log, TEXT("Animation Recorder: Begin Recording: Recorder not Valid, No Recording will occur."));
	return false;
}

void FAnimRecorderInstance::Update(float DeltaTime)
{
	if (SkelComp.IsValid() == false)
	{
		UE_LOG(LogAnimation, Log, TEXT("Animation Recorder:  Update: SkelMeshComp not Valid, No Recording will occur."));
		return;
	}
	if (Recorder.IsValid())
	{
		Recorder->UpdateRecord(SkelComp.Get(), DeltaTime);
	}
	else
	{
		UE_LOG(LogAnimation, Log, TEXT("Animation Recorder:  Update: Recoder not Valid, No Recording will occur."));
	}
}
void FAnimRecorderInstance::FinishRecording(bool bShowMessage)
{
	const FText FinishRecordingAnimationSlowTask = LOCTEXT("FinishRecordingAnimationSlowTask", "Finalizing recorded animation");
	if (Recorder.IsValid())
	{
		Recorder->StopRecord(bShowMessage);
	}

	if (SkelComp.IsValid())
	{
		// restore force lod setting
		SkelComp->SetForcedLOD(CachedSkelCompForcedLodModel);

		// restore update flags
		SkelComp->bEnableUpdateRateOptimizations = bCachedEnableUpdateRateOptimizations;
		SkelComp->VisibilityBasedAnimTickOption = CachedVisibilityBasedAnimTickOption;
	}
}

void FAnimRecorderInstance::ProcessRecordedTimes(UAnimSequence* AnimSequence, USkeletalMeshComponent* SkeletalMeshComponent, const FString& HoursName, const FString& MinutesName, const FString& SecondsName, const FString& FramesName, const FString& SubFramesName, const FString& SlateName, const FString& Slate)
{
	if (Recorder.IsValid())
	{
		Recorder->ProcessRecordedTimes(AnimSequence, SkeletalMeshComponent, HoursName, MinutesName, SecondsName, FramesName, SubFramesName, SlateName, Slate);
	}
}


bool FAnimationRecorderManager::RecordAnimation(USkeletalMeshComponent* Component, const FString& AssetPath, const FString& AssetName, const FAnimationRecordingSettings& Settings)
{
	if (Component)
	{
		FAnimRecorderInstance NewInst;
		NewInst.Init(Component, AssetPath, AssetName, Settings);
		bool const bSuccess = NewInst.BeginRecording();
		if (bSuccess)
		{
			RecorderInstances.Add(NewInst);
		}

	#if WITH_EDITOR
			// if recording via PIE, be sure to stop recording cleanly when PIE ends
			UWorld const* const World = Component->GetWorld();
			if (World && World->IsPlayInEditor())
			{
				FEditorDelegates::EndPIE.AddRaw(this, &FAnimationRecorderManager::HandleEndPIE);
			}
	#endif

		return bSuccess;
	}

	return false;
}

bool FAnimationRecorderManager::RecordAnimation(USkeletalMeshComponent* Component, UAnimSequence* Sequence, const FAnimationRecordingSettings& Settings)
{
	if (Component)
	{
		FAnimRecorderInstance NewInst;
		NewInst.Init(Component, Sequence, nullptr, Settings);
		bool const bSuccess = NewInst.BeginRecording();
		if (bSuccess)
		{
			RecorderInstances.Add(NewInst);
		}

	#if WITH_EDITOR
			// if recording via PIE, be sure to stop recording cleanly when PIE ends
			UWorld const* const World = Component->GetWorld();
			if (World && World->IsPlayInEditor())
			{
				FEditorDelegates::EndPIE.AddRaw(this, &FAnimationRecorderManager::HandleEndPIE);
			}
	#endif

		return bSuccess;
	}

	return false;
}

bool FAnimationRecorderManager::RecordAnimation(USkeletalMeshComponent* Component, UAnimSequence* Sequence, FAnimationSerializer* InSerializer, const FAnimationRecordingSettings& Settings)
{
	if (Component)
	{
		FAnimRecorderInstance NewInst;
		NewInst.Init(Component, Sequence, InSerializer, Settings);
		bool const bSuccess = NewInst.BeginRecording();
		if (bSuccess)
		{
			RecorderInstances.Add(NewInst);
		}

#if WITH_EDITOR
		// if recording via PIE, be sure to stop recording cleanly when PIE ends
		UWorld const* const World = Component->GetWorld();
		if (World && World->IsPlayInEditor())
		{
			FEditorDelegates::EndPIE.AddRaw(this, &FAnimationRecorderManager::HandleEndPIE);
		}
#endif

		return bSuccess;
	}

	return false;
}

void FAnimationRecorderManager::HandleEndPIE(bool bSimulating)
{
	StopRecordingAllAnimations();

#if WITH_EDITOR
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif
}

bool FAnimationRecorderManager::IsRecording(USkeletalMeshComponent* Component)
{
	for (FAnimRecorderInstance& Instance : RecorderInstances)
	{
		if (Instance.SkelComp == Component)
		{
			return Instance.Recorder->InRecording();
		}
	}

	return false;
}

bool FAnimationRecorderManager::IsRecording()
{
	for (FAnimRecorderInstance& Instance : RecorderInstances)
	{
		if (Instance.Recorder->InRecording())
		{
			return true;
		}
	}

	return false;
}

UAnimSequence* FAnimationRecorderManager::GetCurrentlyRecordingSequence(USkeletalMeshComponent* Component)
{
	for (FAnimRecorderInstance& Instance : RecorderInstances)
	{
		if (Instance.SkelComp == Component)
		{
			return Instance.Recorder->GetAnimationObject();
		}
	}

	return nullptr;
}

float FAnimationRecorderManager::GetCurrentRecordingTime(USkeletalMeshComponent* Component)
{
	for (FAnimRecorderInstance& Instance : RecorderInstances)
	{
		if (Instance.SkelComp == Component)
		{
			return Instance.Recorder->GetTimeRecorded();
		}
	}

	return 0.0f;
}

const FTransform&  FAnimationRecorderManager::GetInitialRootTransform(USkeletalMeshComponent* Component) const
{
	for (const FAnimRecorderInstance& Instance : RecorderInstances)
	{
		if (Instance.SkelComp == Component)
		{
			return Instance.Recorder->GetInitialRootTransform();
		}
	}
	return FTransform::Identity;
}

void FAnimationRecorderManager::StopRecordingAnimation(USkeletalMeshComponent* Component, bool bShowMessage)
{
	for (int32 Idx = 0; Idx < RecorderInstances.Num(); ++Idx)
	{
		FAnimRecorderInstance& Inst = RecorderInstances[Idx];
		if (Inst.SkelComp == Component)
		{
			// stop and finalize recoded data
			Inst.FinishRecording(bShowMessage);

			// remove instance, which will clean itself up
			RecorderInstances.RemoveAtSwap(Idx);

			// all done
			break;
		}
	}
}

void FAnimationRecorderManager::StopRecordingDeadAnimations(bool bShowMessage)
{
	RecorderInstances.RemoveAll([&](FAnimRecorderInstance& Instance)
		{
			if (!Instance.SkelComp.IsValid())
			{
				// stop and finalize recorded data
				Instance.FinishRecording(bShowMessage);

				// make sure we are cleaned up
				return true;
			}

			return false;
		}
	);
}

void FAnimationRecorderManager::StopRecordingAllAnimations()
{
	for (auto& Inst : RecorderInstances)
	{
		Inst.FinishRecording();
	}

	RecorderInstances.Empty();
}

#undef LOCTEXT_NAMESPACE

