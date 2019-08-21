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

void FLiveLinkSubject::Initialize(FLiveLinkSubjectKey InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, ILiveLinkClient* InLiveLinkClient)
{
	SubjectKey = InSubjectKey;
	Role = InRole;

	FrameData.Reset();
}

void FLiveLinkSubject::Update()
{
	// Clear all frames that are too old
	if (FrameData.Num() > CachedSettings.BufferSettings.MaxNumberOfFrameToBuffered)
	{
		FrameData.RemoveAt(0, FrameData.Num() - CachedSettings.BufferSettings.MaxNumberOfFrameToBuffered, false);
	}

	if (GetMode() == ELiveLinkSourceMode::EngineTime)
	{
		double ValidEngineTime = FApp::GetCurrentTime() - CachedSettings.BufferSettings.EngineTimeOffset - CachedSettings.BufferSettings.ValidEngineTime;
		int32 FrameIndex = 0;
		for (const FLiveLinkFrameDataStruct& SourceFrameData : FrameData)
		{
			double FrameTime = SourceFrameData.GetBaseData()->WorldTime.GetOffsettedTime();
			double OffsetTime = ValidEngineTime;
			if (FrameTime > OffsetTime)
			{
				break;
			}
			++FrameIndex;
		}

		if (FrameIndex - 1 >= 0)
		{
			FrameData.RemoveAt(0, FrameIndex, false);
		}
	}
	else if (GetMode() == ELiveLinkSourceMode::Timecode)
	{
		const FQualifiedFrameTime CurrentSyncTime = FQualifiedFrameTime(FApp::GetTimecode(), FApp::GetTimecodeFrameRate());
		const FFrameTime CurrentFrameTimeInFrameSpace = CurrentSyncTime.ConvertTo(CachedSettings.BufferSettings.TimecodeFrameRate);
		int32 FrameIndex = 0;
		for (const FLiveLinkFrameDataStruct& SourceFrameData : FrameData)
		{
			FFrameTime UsedFrameTime = CurrentFrameTimeInFrameSpace - CachedSettings.BufferSettings.TimecodeFrameOffset - CachedSettings.BufferSettings.ValidTimecodeFrame;
			if (SourceFrameData.GetBaseData()->MetaData.SceneTime.Time > UsedFrameTime)
			{
				break;
			}
			++FrameIndex;
		}

		if (FrameIndex - 1 >= 0)
		{
			FrameData.RemoveAt(0, FrameIndex, false);
		}
	}

	// Build a snapshot for this role
	bool bSnapshotIsValid = false;
	if (FrameData.Num() > 0)
	{
		switch(GetMode())
		{
		case ELiveLinkSourceMode::Timecode:
		{
			const FQualifiedFrameTime CurrentSyncTime = FQualifiedFrameTime(FApp::GetTimecode(), FApp::GetTimecodeFrameRate());
			bSnapshotIsValid = GetFrameAtSceneTime(CurrentSyncTime, FrameSnapshot);
		}
		break;

		case ELiveLinkSourceMode::EngineTime:
			bSnapshotIsValid = GetFrameAtWorldTime(FApp::GetCurrentTime(), FrameSnapshot);
			break;

		case ELiveLinkSourceMode::Latest:
		default:
			bSnapshotIsValid = GetLatestFrame(FrameSnapshot);
		}
	}

	if (!bSnapshotIsValid)
	{
		// Invalidate the snapshot
		FrameSnapshot.FrameData.Reset();
	}
}

void FLiveLinkSubject::ClearFrames()
{
	FrameSnapshot.StaticData.Reset();
	FrameSnapshot.FrameData.Reset();
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
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject '%s'. No role has been set yet."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	if (InDesiredRole == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject '%s'. Invalid role was received for evaluation."), *SubjectKey.SubjectName.ToString());
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
			UE_LOG(LogLiveLink, Verbose, TEXT("Can't evaluate frame for subject %s. Role %s is incompatible with current role %s"), *SubjectKey.SubjectName.ToString(), *InDesiredRole->GetName(), *Role->GetName());
		}
	}

	return bSuccess;
}

bool FLiveLinkSubject::EvaluateFrameAtSceneTime(const FTimecode& InSceneTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame)
{
	if (Role == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject '%s'. No role has been set yet."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	if (InDesiredRole == nullptr)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't evaluate frame for subject '%s'. Invalid role was received for evaluation."), *SubjectKey.SubjectName.ToString());
		return false;
	}

	bool bSuccess = false;
	if (FrameData.Num() != 0)
	{
		const FQualifiedFrameTime UseTime = FQualifiedFrameTime(InSceneTime, FApp::GetTimecodeFrameRate());
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
			UE_LOG(LogLiveLink, Verbose, TEXT("Can't evaluate frame for subject '%s'. Role '%s' is incompatible with current role '%s'"), *SubjectKey.SubjectName.ToString(), *InDesiredRole->GetName(), *Role->GetName());
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
	if (!StaticData.IsValid())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't add frame for subject '%s'. The static frame data is invalid."), *SubjectKey.SubjectName.ToString());
		return;
	}

	if (Role == nullptr)
	{
		return;
	}

	if (Role->GetDefaultObject<ULiveLinkRole>()->GetFrameDataStruct() != InFrameData.GetStruct())
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Can't add frame for subject '%s'. The frame data is incompatible with current role '%s'."), *SubjectKey.SubjectName.ToString(), *Role->GetName());
		return;
	}

	if (!FLiveLinkRoleTrait::Validate(Role, InFrameData))
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Trying to add unsupported frame data type to role '%s'."), *Role->GetName());
		return;
	}

	int32 FrameIndex = INDEX_NONE;
	switch (CachedSettings.SourceMode)
	{
	case ELiveLinkSourceMode::EngineTime:
		FrameIndex = FindNewFrame_WorldTime(InFrameData.GetBaseData()->WorldTime);
		break;
	case ELiveLinkSourceMode::Timecode:
		FrameIndex = FindNewFrame_SceneTime(InFrameData.GetBaseData()->MetaData.SceneTime);
		break;
	case ELiveLinkSourceMode::Latest:
	default:
		FrameIndex = FindNewFrame_Latest();
		break;
	}
	
	if (FrameIndex != INDEX_NONE)
	{
		for (ULiveLinkFramePreProcessor::FWorkerSharedPtr PreProcessor : FramePreProcessors)
		{
			PreProcessor->PreProcessFrame(InFrameData);
		}

		FrameData.Insert(MoveTemp(InFrameData), FrameIndex);
	}

	LastPushTime = FApp::GetCurrentTime();
}

int32 FLiveLinkSubject::FindNewFrame_WorldTime(const FLiveLinkWorldTime& WorldTime) const
{
	int32 FrameIndex = FrameData.Num() - 1;
	const double NewFrameOffsettedTime = WorldTime.GetOffsettedTime();
	for (; FrameIndex >= 0; --FrameIndex)
	{
		if (FrameData[FrameIndex].GetBaseData()->WorldTime.GetOffsettedTime() <= NewFrameOffsettedTime)
		{
			break;
		}
	}

	return FrameIndex + 1;
}

int32 FLiveLinkSubject::FindNewFrame_SceneTime(const FQualifiedFrameTime& QualifiedFrameTime) const
{
	if (QualifiedFrameTime.Time.FloorToFrame() < 0)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Trying to add a frame that does not have a valid scene time (timecode). The Subject is '%s'."), *SubjectKey.SubjectName.ToString());
		return INDEX_NONE;
	}

	if (QualifiedFrameTime.Rate != CachedSettings.BufferSettings.TimecodeFrameRate)
	{
		UE_LOG(LogLiveLink, Warning, TEXT("Trying to add a frame that timecode frame rate does not match with the expected frame rate. The Subject is '%s'."), *SubjectKey.SubjectName.ToString());
		return INDEX_NONE;
	}

	int32 FrameIndex = FrameData.Num() - 1;
	for (; FrameIndex >= 0; --FrameIndex)
	{
		if (FrameData[FrameIndex].GetBaseData()->MetaData.SceneTime.Time <= QualifiedFrameTime.Time)
		{
			break;
		}
	}

	return FrameIndex + 1;
}

int32 FLiveLinkSubject::FindNewFrame_Latest() const
{
	return FrameData.Num();
}

bool FLiveLinkSubject::GetFrameAtWorldTime(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	bool bResult = FrameData.Num() != 0;
	if (bResult)
	{
		if (FrameInterpolationProcessor.IsValid())
		{
			bResult = GetFrameAtWorldTime_Interpolated(InSeconds, OutFrame);
		}
		else
		{
			bResult = GetFrameAtWorldTime_Closest(InSeconds, OutFrame);
		}

		if (bResult && !OutFrame.StaticData.IsValid())
		{
			OutFrame.StaticData.InitializeWith(StaticData.GetStruct(), StaticData.GetBaseData());
		}
	}
	return bResult;
}

bool FLiveLinkSubject::GetFrameAtWorldTime_Closest(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	check(FrameData.Num() != 0);

	bool bBuiltFrame = false;
	const double ReadTime = (InSeconds) - CachedSettings.BufferSettings.EngineTimeOffset;
	for (int32 FrameIndex = FrameData.Num() - 1; FrameIndex >= 0; --FrameIndex)
	{
		const double Time = FrameData[FrameIndex].GetBaseData()->WorldTime.GetOffsettedTime();
		if (Time < ReadTime)
		{
			if (FrameIndex == FrameData.Num() - 1)
			{
				//Copy over the frame directly
				OutFrame.FrameData.InitializeWith(FrameData[FrameIndex]);
				bBuiltFrame = true;
#if WITH_EDITOR
				SnapshotIndex = FrameIndex;
				NumberOfBufferAtSnapshot = FrameData.Num();
#endif 
				break;
			}
			else
			{
				const double NextTime = FrameData[FrameIndex + 1].GetBaseData()->WorldTime.GetOffsettedTime();
				const float BlendWeight = (ReadTime - NextTime) / (NextTime - Time);
				int32 CopyIndex = (BlendWeight > 0.5f) ? FrameIndex : FrameIndex + 1;
				OutFrame.FrameData.InitializeWith(FrameData[CopyIndex].GetStruct(), FrameData[CopyIndex].GetBaseData());
				bBuiltFrame = true;
#if WITH_EDITOR
				SnapshotIndex = CopyIndex;
				NumberOfBufferAtSnapshot = FrameData.Num();
#endif 
				break;
			}
		}
	}

	if (!bBuiltFrame)
	{
		// Failed to find an interp point so just take oldest frame
		OutFrame.FrameData.InitializeWith(FrameData[0].GetStruct(), FrameData[0].GetBaseData());
#if WITH_EDITOR
		SnapshotIndex = INDEX_NONE;
		NumberOfBufferAtSnapshot = FrameData.Num();
#endif 
	}

	return true;
}

bool FLiveLinkSubject::GetFrameAtWorldTime_Interpolated(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame)
{
	check(FrameData.Num() != 0);

	double ReadTime = InSeconds - CachedSettings.BufferSettings.EngineTimeOffset;
	FrameInterpolationProcessor->Interpolate(ReadTime, StaticData, FrameData, OutFrame);
	return true;
}

bool FLiveLinkSubject::GetFrameAtSceneTime(const FQualifiedFrameTime& InTimeInEngineFrameRate, FLiveLinkSubjectFrameData& OutFrame)
{
	bool bResult = FrameData.Num() != 0;
	if (bResult)
	{
		if (FrameInterpolationProcessor.IsValid())
		{
			bResult = GetFrameAtSceneTime_Interpolated(InTimeInEngineFrameRate, OutFrame);
		}
		else
		{
			bResult = GetFrameAtSceneTime_Closest(InTimeInEngineFrameRate, OutFrame);
		}

		if (bResult && !OutFrame.StaticData.IsValid())
		{
			OutFrame.StaticData.InitializeWith(StaticData.GetStruct(), StaticData.GetBaseData());
		}
	}
	return bResult;
}

bool FLiveLinkSubject::GetFrameAtSceneTime_Closest(const FQualifiedFrameTime& InTimeInEngineFrameRate, FLiveLinkSubjectFrameData& OutFrame)
{
	check(FrameData.Num() != 0);

	bool bBuiltFrame = false;

	const FFrameTime FrameOffset = FQualifiedFrameTime(CachedSettings.BufferSettings.TimecodeFrameOffset, CachedSettings.BufferSettings.TimecodeFrameRate).ConvertTo(InTimeInEngineFrameRate.Rate);
	const FFrameTime ReadTime = InTimeInEngineFrameRate.Time - FrameOffset;

	FTimecode ReadTimecode = FTimecode::FromFrameNumber(ReadTime.GetFrame(), InTimeInEngineFrameRate.Rate, false);

	for (int32 FrameIndex = FrameData.Num() - 1; FrameIndex >= 0; --FrameIndex)
	{
		const FFrameTime Time = FrameData[FrameIndex].GetBaseData()->MetaData.SceneTime.ConvertTo(InTimeInEngineFrameRate.Rate);
		FTimecode Timecode = FTimecode::FromFrameNumber(Time.GetFrame(), InTimeInEngineFrameRate.Rate, false);
		if (Time < ReadTime)
		{
			if (FrameIndex == FrameData.Num() - 1)
			{
				//Copy over the frame directly
				OutFrame.FrameData.InitializeWith(FrameData[FrameIndex]);
				bBuiltFrame = true;
#if WITH_EDITOR
				SnapshotIndex = FrameIndex;
				NumberOfBufferAtSnapshot = FrameData.Num();
#endif 

				break;
			}
			else
			{
				const FFrameTime NextTime = FrameData[FrameIndex+1].GetBaseData()->MetaData.SceneTime.ConvertTo(InTimeInEngineFrameRate.Rate);
				const double BlendWeight = (ReadTime - NextTime).AsDecimal() / (NextTime - Time).AsDecimal();
				int32 CopyIndex = (BlendWeight > 0.5) ? FrameIndex : FrameIndex + 1;
				OutFrame.FrameData.InitializeWith(FrameData[CopyIndex].GetStruct(), FrameData[CopyIndex].GetBaseData());
				bBuiltFrame = true;
#if WITH_EDITOR
				SnapshotIndex = CopyIndex;
				NumberOfBufferAtSnapshot = FrameData.Num();
#endif 
				break;
			}
		}
	}

	if (!bBuiltFrame)
	{
#if WITH_EDITOR
		SnapshotIndex = INDEX_NONE;
		NumberOfBufferAtSnapshot = FrameData.Num();
#endif 
		// Failed to find an interp point so just take oldest frame
		OutFrame.FrameData.InitializeWith(FrameData[0].GetStruct(), FrameData[0].GetBaseData());
	}

	return true;
}

bool FLiveLinkSubject::GetFrameAtSceneTime_Interpolated(const FQualifiedFrameTime& InTimeInEngineFrameRate, FLiveLinkSubjectFrameData& OutFrame)
{
	check(FrameData.Num() != 0);

	const FFrameTime ReadTime = InTimeInEngineFrameRate.Time - CachedSettings.BufferSettings.TimecodeFrameOffset;
	FrameInterpolationProcessor->Interpolate(FQualifiedFrameTime(ReadTime, InTimeInEngineFrameRate.Rate), StaticData, FrameData, OutFrame);
	return true;
}

bool FLiveLinkSubject::GetLatestFrame(FLiveLinkSubjectFrameData& OutFrame)
{
	if (FrameData.Num())
	{
		FLiveLinkFrameDataStruct& LastDataStruct = FrameData.Last();
		OutFrame.FrameData.InitializeWith(LastDataStruct.GetStruct(), LastDataStruct.GetBaseData());
		OutFrame.StaticData.InitializeWith(StaticData.GetStruct(), StaticData.GetBaseData());
#if WITH_EDITOR
		SnapshotIndex = FrameData.Num() - 1;
		NumberOfBufferAtSnapshot = FrameData.Num();
#endif 

		return true;
	}
	return false;
}

void FLiveLinkSubject::ResetFrame(FLiveLinkSubjectFrameData& OutFrame) const
{
	//Allocate and copy over our static data for that frame.
	OutFrame.StaticData.InitializeWith(StaticData);

	//Only reset the frame data. Copy will be done later on depending on sampling type
	OutFrame.FrameData.Reset();
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
#if WITH_EDITOR
		if (NumberOfBufferAtSnapshot > 0)
		{
			FLiveLinkSourceDebugInfo DebugInfo;
			DebugInfo.SubjectName = SubjectKey.SubjectName;
			DebugInfo.SnapshotIndex = SnapshotIndex + 1;
			DebugInfo.NumberOfBufferAtSnapshot = NumberOfBufferAtSnapshot;
			SourceSetting->SourceDebugInfos.Add(DebugInfo);
		}
		SnapshotIndex = INDEX_NONE;
		NumberOfBufferAtSnapshot = 0;
#endif

		const bool bSourceModeChanged = SourceSetting->Mode != CachedSettings.SourceMode;
		const bool bTimecodeFrameRateChanged = SourceSetting->Mode == ELiveLinkSourceMode::Timecode && SourceSetting->BufferSettings.TimecodeFrameRate != CachedSettings.BufferSettings.TimecodeFrameRate;
		if (bSourceModeChanged || bTimecodeFrameRateChanged)
		{
			FrameData.Reset();
		}

		CachedSettings.SourceMode = SourceSetting->Mode;
		CachedSettings.BufferSettings = SourceSetting->BufferSettings;

		CachedSettings.BufferSettings.MaxNumberOfFrameToBuffered = FMath::Max(CachedSettings.BufferSettings.MaxNumberOfFrameToBuffered, 1);

		// Create a new or fetch the PreProcessors for this frame
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

		// Create a new or fetch the interpolation for this frame
		FrameInterpolationProcessor.Reset();
		if (SubjectSetting->InterpolationProcessor)
		{
			FrameInterpolationProcessor = SubjectSetting->InterpolationProcessor->FetchWorker();
		}

		// Create a new or fetch the translators for this frame
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

	if (SyncData.bIsValid)
	{
		SyncData.NewestSampleTime = FrameData.Last().GetBaseData()->MetaData.SceneTime.Time;
		SyncData.OldestSampleTime = FrameData[0].GetBaseData()->MetaData.SceneTime.Time;
		SyncData.SampleFrameRate = FrameData[0].GetBaseData()->MetaData.SceneTime.Rate;
	}

	return SyncData;
}

bool FLiveLinkSubject::IsTimeSynchronized() const
{
	if (GetMode() == ELiveLinkSourceMode::Timecode)
	{
		const FLiveLinkSubjectFrameData& Snapshot = GetFrameSnapshot();
		if (Snapshot.StaticData.IsValid() && Snapshot.FrameData.IsValid() && Snapshot.FrameData.GetBaseData())
		{
			const FFrameNumber FrameDataInEngineFrameNumber = Snapshot.FrameData.GetBaseData()->MetaData.SceneTime.ConvertTo(FApp::GetTimecodeFrameRate()).GetFrame();
			const FFrameNumber CurrentEngineFrameNumber = FApp::GetTimecode().ToFrameNumber(FApp::GetTimecodeFrameRate());
			return FrameDataInEngineFrameNumber == CurrentEngineFrameNumber;
		}
	}
	return false;
}
