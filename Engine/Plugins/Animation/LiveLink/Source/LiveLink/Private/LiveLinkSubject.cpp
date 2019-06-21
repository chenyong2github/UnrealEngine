// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSubject.h"

#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkFrameTranslator.h"
#include "LiveLinkClient.h"
#include "LiveLinkRole.h"
#include "LiveLinkRoleTrait.h"
#include "LiveLinkSettings.h"
#include "Misc/App.h"
#include "Templates/SubclassOf.h"
#include "TimeSynchronizationSource.h"
#include "UObject/Class.h"


const int32 MIN_FRAMES_TO_REMOVE = 5;


void FLiveLinkSubject::Initialize(FLiveLinkSubjectKey InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, ILiveLinkClient* InLiveLinkClient)
{
	SubjectKey = InSubjectKey;
	Role = InRole;

	FrameData.Reset();
}

void FLiveLinkSubject::Update()
{
	// Invalid the snapshot
	FrameSnapshot.StaticData.Reset();
	FrameSnapshot.FrameData.Reset();

	// TODO Clear all frames that are too old
	bool bCanClearOldFrames = TimeSyncData.IsSet() ? TimeSyncData.GetValue().bHasEstablishedSync : true;
	if (bCanClearOldFrames)
	{

	}

	// Build a snapshot for this role 
	if (FrameData.Num() > 0)
	{
		if (GetMode() == ELiveLinkSourceMode::TimeSynchronized)
		{
			const FFrameRate FrameRate = FApp::GetTimecodeFrameRate();
			const FTimecode Timecode = FApp::GetTimecode();
			const FQualifiedFrameTime CurrentSyncTime(Timecode.ToFrameNumber(FrameRate), FrameRate);
			GetFrameAtSceneTime(CurrentSyncTime, FrameSnapshot);
		}
		else
		{
			GetFrameAtWorldTime(FApp::GetCurrentTime(), FrameSnapshot);
		}
	}
}

void FLiveLinkSubject::ClearFrames()
{
	FrameSnapshot.StaticData.Reset();
	FrameSnapshot.FrameData.Reset();

	LastReadFrame = INDEX_NONE;
	LastReadTime = 0;
	FrameData.Reset();
}

bool FLiveLinkSubject::HasValidFrameSnapshot() const
{
	return FrameSnapshot.StaticData.IsValid() && FrameSnapshot.FrameData.IsValid();
}

bool FLiveLinkSubject::EvaluateFrameAtWorldTime(double InWorldTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	if (Role == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject %s. No role has been set yet."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	if (InDesiredRole == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject %s. Invalid role was received for evaluation."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	bool bSuccess = false;
	if (FrameData.Num() != 0)
	{
		if (Role == InDesiredRole || Role->IsChildOf(InDesiredRole))
		{
			GetFrameAtWorldTime(InWorldTime, OutFrame);
			bSuccess = true;
		}
		else if (SupportsRole(InDesiredRole))
		{
			FLiveLinkSubjectFrameData TmpFrameData;
			GetFrameAtWorldTime(InWorldTime, TmpFrameData);
			bSuccess = Translate(this, InDesiredRole, TmpFrameData.StaticData, TmpFrameData.FrameData, OutFrame);
		}
		else
		{
			UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject %s. Role %s is incompatible with current role %s"), *SubjectKey.SubjectName.ToString(), *InDesiredRole->GetName(), *Role->GetName());
		}
	}

	return bSuccess;
}

bool FLiveLinkSubject::EvaluateFrameAtSceneTime(const FTimecode& InSceneTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	if (Role == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject %s. No role has been set yet."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	if (InDesiredRole == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject %s. Invalid role was received for evaluation."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	bool bSuccess = false;
	if (FrameData.Num() != 0)
	{
		const FFrameRate FrameRate = FApp::GetTimecodeFrameRate();
		const FQualifiedFrameTime UseTime(InSceneTime.ToFrameNumber(FrameRate), FrameRate);

		if (Role == InDesiredRole || Role->IsChildOf(InDesiredRole))
		{
			GetFrameAtSceneTime(UseTime, OutFrame);
			bSuccess = true;
		}
		else if (SupportsRole(InDesiredRole))
		{
			FLiveLinkSubjectFrameData TmpFrameData;
			GetFrameAtSceneTime(UseTime, TmpFrameData);
			bSuccess = Translate(this, InDesiredRole, TmpFrameData.StaticData, TmpFrameData.FrameData, OutFrame);
		}
		else
		{
			UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject %s. Role %s is incompatible with current role %s"), *SubjectKey.SubjectName.ToString(), *InDesiredRole->GetName(), *Role->GetName());
		}
	}

	return bSuccess;
}

bool FLiveLinkSubject::HasStaticData() const
{
	return StaticData.IsValid();
}

void FLiveLinkSubject::AddFrameData(FLiveLinkFrameDataStruct&& InFrameData)
{
	check(IsInGameThread());
	check(StaticData.IsValid());

	if (Role == nullptr)
	{
		return;
	}

	if (Role->GetDefaultObject<ULiveLinkRole>()->GetFrameDataStruct() != InFrameData.GetStruct())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't add frame for subject %s. The frame data is incompatible with current role %s"), *SubjectKey.SubjectName.ToString(), *Role->GetName());
		return;
	}

	if (!FLiveLinkRoleTrait::Validate(Role, InFrameData))
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Trying to add unsupported frame data type to role %s."), *Role->GetName());
		return;
	}

	int32 FrameIndex = INDEX_NONE;
	switch (CachedSettings.SourceMode)
	{
	case ELiveLinkSourceMode::TimeSynchronized:
		if (TimeSyncData.IsSet())
		{
			FrameIndex = AddFrame_TimeSynchronized(InFrameData.GetBaseData()->MetaData.SceneTime.Time, TimeSyncData.GetValue());
		}
		else
		{
			FrameIndex = AddFrame_Default(InFrameData.GetBaseData()->WorldTime);
		}
		break;

	case ELiveLinkSourceMode::Interpolated:
		FrameIndex = AddFrame_Interpolated(InFrameData.GetBaseData()->WorldTime);
		break;

	default:
		FrameIndex = AddFrame_Default(InFrameData.GetBaseData()->WorldTime);
		break;
	}
	
	for (ULiveLinkFramePreProcessor::FWorkerSharedPtr PreProcessor : FramePreProcessors)
	{
		PreProcessor->PreProcessFrame(InFrameData);
	}

	FrameData.Insert(MoveTemp(InFrameData), FrameIndex);
	LastPushTime = FApp::GetCurrentTime();
}

int32 FLiveLinkSubject::AddFrame_Default(const FLiveLinkWorldTime& WorldTime)
{
	if (WorldTime.Time < LastReadTime)
	{
		//Gone back in time
		FrameData.Reset();
		LastReadTime = 0;
		SubjectTimeOffset = WorldTime.Offset;
	}

	int32 FrameIndex = 0;
	if (FrameData.Num() == 0)
	{
		LastReadFrame = 0;
	}
	else
	{
		if (LastReadFrame > MIN_FRAMES_TO_REMOVE)
		{
			check(FrameData.Num() > LastReadFrame);
			FrameData.RemoveAt(0, LastReadFrame, false);
			LastReadFrame = 0;
		}

		for (FrameIndex = FrameData.Num() - 1; FrameIndex >= 0; --FrameIndex)
		{
			if (FrameData[FrameIndex].GetBaseData()->WorldTime.Time <= WorldTime.Time)
			{
				break;
			}
		}

		FrameIndex += 1;
	}

	return FrameIndex;
}

int32 FLiveLinkSubject::AddFrame_Interpolated(const FLiveLinkWorldTime& WorldTime)
{
	if (LastReadFrame > MIN_FRAMES_TO_REMOVE)
	{
		check(FrameData.Num() > LastReadFrame);
		FrameData.RemoveAt(0, LastReadFrame - 1, false);
		LastReadFrame = 1;
	}

	return AddFrame_Default(WorldTime);
}

int32 FLiveLinkSubject::AddFrame_TimeSynchronized(const FFrameTime& FrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData)
{
	int32 FrameIndex = 0;

	// If we're not actively synchronizing, we don't need to do anything special.
	if (FrameData.Num() == 0)
	{
		LastReadTime = 0;
		LastReadFrame = 0;
	}
	else if (InSynchronizationData.RolloverModulus.IsSet())
	{
		const FFrameTime UseFrameTime = UTimeSynchronizationSource::AddOffsetWithRolloverModulus(FrameTime, InSynchronizationData.Offset, InSynchronizationData.RolloverModulus.GetValue());
		FrameIndex = AddFrame_TimeSynchronized</*bWithRollover=*/true>(UseFrameTime, InSynchronizationData);
	}
	else
	{
		FrameIndex = AddFrame_TimeSynchronized</*bWithRollover=*/false>(FrameTime + InSynchronizationData.Offset, InSynchronizationData);
	}

	return FrameIndex;
}

template<bool bWithRollover>
int32 FLiveLinkSubject::AddFrame_TimeSynchronized(const FFrameTime& FrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData)
{
	//We keep buffering as long as synchronization hasn't been achieved
	if (InSynchronizationData.bHasEstablishedSync && LastReadFrame > MIN_FRAMES_TO_REMOVE)
	{
		check(FrameData.Num() > LastReadFrame);

		if (bWithRollover)
		{
			int32& RolloverFrame = InSynchronizationData.RolloverFrame;

			// If we had previously detected that a roll over had occurred in the range of frames we have,
			// then we need to adjust that as well.
			if (RolloverFrame > 0)
			{
				RolloverFrame = RolloverFrame - LastReadFrame;
				if (RolloverFrame <= 0)
				{
					RolloverFrame = INDEX_NONE;
				}
			}
		}

		FrameData.RemoveAt(0, LastReadFrame, false);
		LastReadFrame = 0;
	}

	return FindFrameIndex_TimeSynchronized</*bForInsert=*/true, bWithRollover>(FrameTime, InSynchronizationData);
}

void FLiveLinkSubject::ResetFrame(FLiveLinkSubjectFrameData& OutFrame) const
{
	//Allocate and copy over our static data for that frame.
	OutFrame.StaticData.InitializeWith(StaticData);

	//Only reset the frame data. Copy will be done later on depending on sampling type
	OutFrame.FrameData.Reset();
}

void FLiveLinkSubject::GetFrameAtWorldTime(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	ResetFrame(OutFrame);

	switch (CachedSettings.SourceMode)
	{
	case ELiveLinkSourceMode::TimeSynchronized:
		ensureMsgf(false, TEXT("Attempting to use WorldTime for a TimeSynchronized source! Source = %s"), *SubjectKey.SubjectName.ToString());
		GetFrameAtWorldTime_Default(InSeconds, OutFrame);
		break;

	case ELiveLinkSourceMode::Interpolated:

		GetFrameAtWorldTime_Interpolated(InSeconds, OutFrame);
		break;

	default:
		GetFrameAtWorldTime_Default(InSeconds, OutFrame);
		break;
	}
}

void FLiveLinkSubject::GetFrameAtSceneTime(const FQualifiedFrameTime& InSceneTime, FLiveLinkSubjectFrameData& OutFrame)
{
	ResetFrame(OutFrame);
	
	switch (CachedSettings.SourceMode)
	{
	case ELiveLinkSourceMode::TimeSynchronized:

		if (TimeSyncData.IsSet())
		{
			/**
			 * Get the actual frame of data at the specified time.
			 * Note, unlike GetTimeSyncData which subtracts the offset from the Saved Frames' Timecodes,
			 * we're adding the offset to the Incoming Timecode Value.
			 *
			 * The reason for this can be thought of as a simple translation between two spaces: Real Space and Source Space.
			 * InSceneTime (and generally timecode the engine is using) is considered Real Space.
			 * The timecode reported with our frames is considered Source Space.
			 * `Source Space = Real Space + Offset`, and therefore `Real Space = Source Space - Offset`.
			 *
			 * We could just apply the offset on the frame's when they are Pushed to Live Link, but in general
			 * we don't want to modify source data. Further, the Frame Offset is not available until synchronization
			 * is started now, but it may change to be more dynamic in the future, and we don't want to have to
			 * "restripe" the offsets in that case.
			 */
			const FFrameTime FrameTime = InSceneTime.ConvertTo(CachedSettings.TimeSynchronizationSettings->FrameRate) - CachedSettings.TimeSynchronizationSettings->FrameDelay;
			if (TimeSyncData->RolloverModulus.IsSet())
			{
				const FFrameTime UseFrameTime = UTimeSynchronizationSource::AddOffsetWithRolloverModulus(FrameTime, TimeSyncData->Offset, TimeSyncData->RolloverModulus.GetValue());
				GetFrameAtSceneTime_TimeSynchronized</*bWithRollover=*/true>(UseFrameTime, TimeSyncData.GetValue(), OutFrame);
			}
			else
			{
				const FFrameTime UseFrameTime = FrameTime + TimeSyncData->Offset;
				GetFrameAtSceneTime_TimeSynchronized</*bWithRollover=*/false>(UseFrameTime, TimeSyncData.GetValue(), OutFrame);
			}
		}
		else
		{
			GetFrameAtWorldTime_Default(InSceneTime.AsSeconds(), OutFrame);
		}
		break;

	default:
		ensureMsgf(false, TEXT("Attempting to use SceneTime for a non TimeSynchronized source! Source = %s Mode = %d"), *SubjectKey.SubjectName.ToString(), static_cast<int32>(CachedSettings.SourceMode));
		GetFrameAtWorldTime_Default(InSceneTime.AsSeconds(), OutFrame);
		break;
	}
}

void FLiveLinkSubject::GetFrameAtWorldTime_Default(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	FLiveLinkFrameDataStruct& LastFrame = FrameData.Last();

	//Copy current snapshot of our frame data
	OutFrame.FrameData.InitializeWith(LastFrame);

	LastReadTime = LastFrame.GetBaseData()->WorldTime.Time;
	LastReadFrame = FrameData.Num() - 1;
}

void FLiveLinkSubject::GetFrameAtWorldTime_Interpolated(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	if (FrameInterpolationProcessor.IsValid())
	{
		LastReadTime = (InSeconds - SubjectTimeOffset) - CachedSettings.InterpolationSettings->InterpolationOffset;
		FrameInterpolationProcessor->Interpolate(LastReadTime, StaticData, FrameData, OutFrame, LastReadFrame);
	}
	else
	{
		GetFrameAtWorldTime_Closest(InSeconds, OutFrame);
	}
}

void FLiveLinkSubject::GetFrameAtWorldTime_Closest(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	check(FrameData.Num() != 0);

	LastReadTime = InSeconds - SubjectTimeOffset - CachedSettings.InterpolationSettings->InterpolationOffset;
	for (int32 FrameIndex = FrameData.Num() - 1; FrameIndex >= 0; --FrameIndex)
	{
		const FLiveLinkFrameDataStruct& SourceFrameData = FrameData[FrameIndex];
		if (SourceFrameData.GetBaseData()->WorldTime.Time < LastReadTime)
		{
			if (FrameIndex == FrameData.Num() - 1)
			{
				LastReadFrame = FrameIndex;

				// Copy over the frame directly
				OutFrame.FrameData.InitializeWith(SourceFrameData);
				return;
			}
			else
			{
				LastReadFrame = FrameIndex;

				const FLiveLinkFrameDataStruct& PostFrame = FrameData[FrameIndex + 1];
				const float BlendWeight = (LastReadTime - SourceFrameData.GetBaseData()->WorldTime.Time) / (PostFrame.GetBaseData()->WorldTime.Time - SourceFrameData.GetBaseData()->WorldTime.Time);
				int32 CopyIndex = (BlendWeight > 0.5f) ? FrameIndex+1 : FrameIndex;
				OutFrame.FrameData.InitializeWith(FrameData[CopyIndex].GetStruct(), FrameData[CopyIndex].GetBaseData());
				return;
			}
		}
	}

	// Failed to find an interp point so just take oldest frame
	LastReadFrame = 0;
	OutFrame.FrameData.InitializeWith(FrameData[0].GetStruct(), FrameData[0].GetBaseData());
}

template<bool bWithRollover>
void FLiveLinkSubject::GetFrameAtSceneTime_TimeSynchronized(const FFrameTime& InTime, FLiveLinkTimeSynchronizationData& InSynchronizationData, FLiveLinkSubjectFrameData& OutFrame)
{
	const int32 UseFrame = FindFrameIndex_TimeSynchronized</*bForInsert=*/false, bWithRollover>(InTime, InSynchronizationData);

	OutFrame.FrameData.InitializeWith(FrameData[UseFrame]);

	LastReadTime = FrameData[UseFrame].GetBaseData()->WorldTime.Time;
	LastReadFrame = UseFrame;
}

template<bool bForInsert, bool bWithRollover>
int32 FLiveLinkSubject::FindFrameIndex_TimeSynchronized(const FFrameTime& InFrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData)
{
	if (FrameData.Num() == 0)
	{
		return 0;
	}

	// Preroll / Synchronization should handle the case where there are any time skips by simply clearing out the buffered data.
	// Therefore, there are only 2 cases where time would go backwards:
	// 1. We've received frames out of order. In this case, we want to push it backwards.
	// 2. We've rolled over. In that case, value have wrapped around zero (and appear "smaller") but should be treated as newer.

	// Further, when we're not inserting a value, we're guaranteed that the frame time should always go up
	// (or stay the same). So, in that case we only need to search between our LastReadFrameTime and the Newest Frame.
	// That assumption will break if external code tries to grab anything other than the frame of data we build internally.

	// Finally, we only update the RolloverFrame value when inserting values. This is because we may query for a rollover frame
	// before we receive a rollover frame (in the case of missing or unordered frames).
	// We generally don't want to modify state if we're just reading data.

	int32 HighFrame = FrameData.Num() - 1;
	int32 LowFrame = bForInsert ? 0 : LastReadFrame;
	int32 FrameIndex = HighFrame;

	if (bWithRollover)
	{
		bool bDidRollover = false;
		int32& RolloverFrame = InSynchronizationData.RolloverFrame;
		const FFrameTime& CompareFrameTime = ((RolloverFrame == INDEX_NONE) ? FrameData.Last() : FrameData[RolloverFrame - 1]).GetBaseData()->MetaData.SceneTime.Time;
		UTimeSynchronizationSource::FindDistanceBetweenFramesWithRolloverModulus(CompareFrameTime, InFrameTime, InSynchronizationData.RolloverModulus.GetValue(), bDidRollover);

		if (RolloverFrame == INDEX_NONE)
		{
			if (bDidRollover)
			{
				if (bForInsert)
				{
					RolloverFrame = HighFrame;
					FrameIndex = FrameData.Num();
				}
				else
				{
					FrameIndex = HighFrame;
				}

				return FrameIndex;
			}
		}
		else
		{
			if (bDidRollover)
			{
				LowFrame = RolloverFrame;
			}
			else
			{
				HighFrame = RolloverFrame - 1;
				if (bForInsert)
				{
					++RolloverFrame;
				}
			}
		}
	}

	if (bForInsert)
	{
		for (; LowFrame <= FrameIndex && FrameData[FrameIndex].GetBaseData()->MetaData.SceneTime.Time > InFrameTime; --FrameIndex);
		FrameIndex += 1;
	}
	else
	{
		for (; LowFrame < FrameIndex && FrameData[FrameIndex].GetBaseData()->MetaData.SceneTime.Time > InFrameTime; --FrameIndex);
	}

	return FrameIndex;
}

void FLiveLinkSubject::SetStaticData(TSubclassOf<ULiveLinkRole> InRole, FLiveLinkStaticDataStruct&& InStaticData)
{
	check(IsInGameThread());

	if (Role == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Setting static data for Subject %s before it was initialized."), *SubjectKey.SubjectName.ToString());
		return;
	}

	if(Role == InRole)
	{
		//Set initial blending processor to the role's default one. User will be able to modify it afterwards.
		FrameData.Reset();
		StaticData = MoveTemp(InStaticData);
		StaticDataGuid = FGuid::NewGuid();
	}
	else
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Subject %s received data of role %s but was already registered with a different role"), *SubjectKey.SubjectName.ToString(), *InRole->GetName());
	}	
}

void FLiveLinkSubject::CacheSettings(ULiveLinkSourceSettings* SourceSetting, ULiveLinkSubjectSettings* SubjectSetting)
{
	check(IsInGameThread());

	if (SourceSetting)
	{
		const bool bSourceModeChanged = SourceSetting->Mode != CachedSettings.SourceMode;
		if (bSourceModeChanged)
		{
			CachedSettings.TimeSynchronizationSettings.Reset();
			CachedSettings.InterpolationSettings.Reset();

			FrameData.Reset();

			switch (CachedSettings.SourceMode)
			{
			case ELiveLinkSourceMode::TimeSynchronized:
				TimeSyncData.Reset();
				break;

			default:
				break;
			}
		}

		CachedSettings.SourceMode = SourceSetting->Mode;

		// Even if the mode didn't change, settings may have updated.
		// Handle those changes now.
		switch (CachedSettings.SourceMode)
		{
		case ELiveLinkSourceMode::TimeSynchronized:
			CachedSettings.TimeSynchronizationSettings = SourceSetting->TimeSynchronizationSettings;
			break;

		case ELiveLinkSourceMode::Interpolated:
			CachedSettings.InterpolationSettings = SourceSetting->InterpolationSettings;
			break;

		default:
			break;
		}
	}

	if (SubjectSetting)
	{
		// Create the new PreProcessors for this frame
		FramePreProcessors.Reset();
		for (ULiveLinkFramePreProcessor* PreProcessor : SubjectSetting->PreProcessors)
		{
			if (PreProcessor)
			{
				ULiveLinkFramePreProcessor::FWorkerSharedPtr NewPreProcessor = PreProcessor->FetchWorker();
				if (NewPreProcessor.IsValid())
				{
					FramePreProcessors.Add(NewPreProcessor);
				}
			}
		}

		// Create the new interpolation for this frame
		FrameInterpolationProcessor.Reset();
		if (SubjectSetting->InterpolationProcessor)
		{
			FrameInterpolationProcessor = SubjectSetting->InterpolationProcessor->FetchWorker();
		}

		// Create the new translators for this frame
		FrameTranslators.Reset();
		for (ULiveLinkFrameTranslator* Translator : SubjectSetting->Translators)
		{
			if (Translator)
			{
				ULiveLinkFrameTranslator::FWorkerSharedPtr NewTranslator = Translator->FetchWorker();
				if (NewTranslator.IsValid())
				{
					FrameTranslators.Add(NewTranslator);
				}
			}
		}
	}
}

FLiveLinkSubjectTimeSyncData FLiveLinkSubject::GetTimeSyncData()
{
	FLiveLinkSubjectTimeSyncData SyncData;
	SyncData.bIsValid = FrameData.Num() > 0;
	SyncData.Settings = CachedSettings.TimeSynchronizationSettings.Get(FLiveLinkTimeSynchronizationSettings());

	if (SyncData.bIsValid)
	{
		const FFrameTime LastSceneTime = FrameData.Last().GetBaseData()->MetaData.SceneTime.Time;
		const FFrameTime FirstSceneTime = FrameData[0].GetBaseData()->MetaData.SceneTime.Time;

		if (TimeSyncData.IsSet())
		{
			/**
			* It's possible that the Timecode received by the Subject / Source doesn't align perfectly with other Timecode
			* Sources. This is because there is inherent delays between the initial transmission of a timecode signal
			* and its receipt on any given source machine. This can further be exacerbated if the source doesn't associate
			* timecode with a frame of data until after it's finished processing it.
			* To make things align properly in that case, users specify an additional FrameOffset for Subjects.
			*
			* For example, a Subject may report that it is at timecode X, whereas the raw source data that was processed
			* to generate that frame may correspond to timecode X-O (where O = offset).
			*
			* To compensate for that, we will just report our frame data shifted over by the offset here, and then adjust
			* the desired timecode in GetFrameAtSceneTime.
			*/
			const int32 FrameOffset = TimeSyncData->Offset;
			if (!TimeSyncData->RolloverModulus.IsSet())
			{
				SyncData.NewestSampleTime = LastSceneTime - FrameOffset;
				SyncData.OldestSampleTime = FirstSceneTime - FrameOffset;
			}
			else
			{
				const FFrameTime& RolloverModulus = TimeSyncData->RolloverModulus.GetValue();
				SyncData.NewestSampleTime = UTimeSynchronizationSource::AddOffsetWithRolloverModulus(LastSceneTime, -FrameOffset, RolloverModulus);
				SyncData.OldestSampleTime = UTimeSynchronizationSource::AddOffsetWithRolloverModulus(FirstSceneTime, -FrameOffset, RolloverModulus);
			}
		}
		else
		{
			SyncData.NewestSampleTime = LastSceneTime;
			SyncData.OldestSampleTime = FirstSceneTime;
		}
		
		SyncData.SkeletonGuid = StaticDataGuid;
	}

	return SyncData;
}

void FLiveLinkSubject::OnStartSynchronization(const FTimeSynchronizationOpenData& OpenData, const int32 FrameOffset)
{
	if (CachedSettings.SourceMode == ELiveLinkSourceMode::TimeSynchronized)
	{
		ensure(!TimeSyncData.IsSet());
		TimeSyncData = FLiveLinkTimeSynchronizationData();
		TimeSyncData->RolloverModulus = OpenData.RolloverFrame;
		TimeSyncData->SyncFrameRate = OpenData.SynchronizationFrameRate;
		TimeSyncData->Offset = FrameOffset;

		// Still need to check this, because OpenData.RolloverFrame is a TOptional which may be unset.
		if (TimeSyncData->RolloverModulus.IsSet())
		{
			TimeSyncData->RolloverModulus = FFrameRate::TransformTime(TimeSyncData->RolloverModulus.GetValue(), OpenData.SynchronizationFrameRate, CachedSettings.TimeSynchronizationSettings->FrameRate);
		}

		ClearFrames();
	}
	else
	{
		TimeSyncData.Reset();
	}
}

void FLiveLinkSubject::OnSynchronizationEstablished(const struct FTimeSynchronizationStartData& StartData)
{
	if (CachedSettings.SourceMode == ELiveLinkSourceMode::TimeSynchronized)
	{
		if (TimeSyncData.IsSet())
		{
			TimeSyncData->SyncStartTime = StartData.StartFrame;
			TimeSyncData->bHasEstablishedSync = true;

			// Prevent buffers from being deleted if new data is pushed before we build snapshots.
			LastReadTime = 0.f;
			LastReadFrame = 0.f;
		}
		else
		{
			UE_LOG(LogLiveLink, Warning, TEXT("OnSynchronizationEstablished called with invalid TimeSyncData. Subject may have switched modes or been recreated. %s"), *SubjectKey.SubjectName.ToString());
		}
	}
	else
	{
		TimeSyncData.Reset();
	}
}

void FLiveLinkSubject::OnStopSynchronization()
{
	TimeSyncData.Reset();
}
