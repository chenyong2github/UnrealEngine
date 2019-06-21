// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSubject.h"

#include "LiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "LiveLinkFrameInterpolationProcessor.h"
#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkFrameTranslator.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkRole.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkTypes.h"



struct FLiveLinkTimeSynchronizationData
{
	// Whether or not synchronization has been established.
	bool bHasEstablishedSync = false;

	// The frame in our buffer where a rollover was detected. Only applicable for time synchronized sources.
	int32 RolloverFrame = INDEX_NONE;

	// Frame offset that will be used for this source.
	int32 Offset = 0;

	// Frame Time value modulus. When this value is not set, we assume no rollover occurs.
	TOptional<FFrameTime> RolloverModulus;

	// Frame rate used as the base for synchronization.
	FFrameRate SyncFrameRate;

	// Frame time that synchronization was established (relative to SynchronizationFrameRate).
	FFrameTime SyncStartTime;
};

/**
 * Manages subject manipulation either to add or get frame data for specific roles
 */
class FLiveLinkSubject : public ILiveLinkSubject
{
private:
	using Super = ILiveLinkSubject;

	//~ Begin ILiveLinkSubject Interface
public:
	virtual void Initialize(FLiveLinkSubjectKey SubjectKey, TSubclassOf<ULiveLinkRole> InRole, ILiveLinkClient* LiveLinkClient) override;
	virtual void Update() override;
	virtual void ClearFrames() override;
	virtual FLiveLinkSubjectKey GetSubjectKey() const { return SubjectKey; }
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override { return Role; }
	virtual bool HasValidFrameSnapshot() const override;
	virtual FLiveLinkStaticDataStruct& GetStaticData() override { return FrameSnapshot.StaticData; }
	virtual const FLiveLinkStaticDataStruct& GetStaticData() const override { return FrameSnapshot.StaticData; }
	virtual const TArray<ULiveLinkFrameTranslator::FWorkerSharedPtr> GetFrameTranslators() const override { return FrameTranslators; }
protected:
	virtual const FLiveLinkSubjectFrameData& GetFrameSnapshot() const { return FrameSnapshot; }
	//~ End ILiveLinkSubject Interface

public:
	bool EvaluateFrameAtWorldTime(double InWorldTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame);
	bool EvaluateFrameAtSceneTime(const FTimecode& InSceneTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame);

	bool HasStaticData() const;

	// Handling setting a new static data. Create role data if not found in map.
	void SetStaticData(TSubclassOf<ULiveLinkRole> InRole, FLiveLinkStaticDataStruct&& InStaticData);

	// Add a frame of data from a FLiveLinkFrameData
	void AddFrameData(FLiveLinkFrameDataStruct&& InFrameData);

	void CacheSettings(ULiveLinkSourceSettings* SourceSetting, ULiveLinkSubjectSettings* SubjectSetting);

	ELiveLinkSourceMode GetMode() const { return CachedSettings.SourceMode; }
	FLiveLinkSubjectTimeSyncData GetTimeSyncData();

	void OnStartSynchronization(const struct FTimeSynchronizationOpenData& OpenData, const int32 FrameOffset);
	void OnSynchronizationEstablished(const struct FTimeSynchronizationStartData& StartData);
	void OnStopSynchronization();

	bool IsTimeSynchronized() const { return (GetMode() == ELiveLinkSourceMode::TimeSynchronized  && TimeSyncData.IsSet()); }
	double GetLastPushTime() const { return LastPushTime; }

private:
	int32 AddFrame_Default(const FLiveLinkWorldTime& FrameTime);
	int32 AddFrame_Interpolated(const FLiveLinkWorldTime& FrameTime);
	int32 AddFrame_TimeSynchronized(const FFrameTime& FrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData);

	template<bool bWithRollover>
	int32 AddFrame_TimeSynchronized(const FFrameTime& FrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData);

	void GetFrameAtWorldTime_Default(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);
	void GetFrameAtWorldTime_Interpolated(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);
	void GetFrameAtWorldTime_Closest(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);

	template<bool bWithRollover>
	void GetFrameAtSceneTime_TimeSynchronized(const FFrameTime& FrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData, FLiveLinkSubjectFrameData& OutFrame);

	template<bool bForInsert, bool bWithRollover>
	int32 FindFrameIndex_TimeSynchronized(const FFrameTime& InFrameTime, FLiveLinkTimeSynchronizationData& InSynchronizationData);

	void ResetFrame(FLiveLinkSubjectFrameData& OutFrame) const;

	// Populate OutFrame with a frame based off of the supplied time and our own offsets
	void GetFrameAtWorldTime(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);

	// Populate OutFrame with a frame based off of the supplied scene time.
	void GetFrameAtSceneTime(const FQualifiedFrameTime& InSceneTime, FLiveLinkSubjectFrameData& OutFrame);

protected:
	// The role the subject was build with
	TSubclassOf<ULiveLinkRole> Role;

	TArray<ULiveLinkFramePreProcessor::FWorkerSharedPtr> FramePreProcessors;

	ULiveLinkFrameInterpolationProcessor::FWorkerSharedPtr FrameInterpolationProcessor = nullptr;

	/** List of available translator the subject can use. */
	TArray<ULiveLinkFrameTranslator::FWorkerSharedPtr> FrameTranslators;

private:
	struct FLiveLinkCachedSettings
	{
		ELiveLinkSourceMode SourceMode = ELiveLinkSourceMode::Default;
		TOptional<FLiveLinkInterpolationSettings> InterpolationSettings;
		TOptional<FLiveLinkTimeSynchronizationSettings> TimeSynchronizationSettings;
	};

	// Static data of the subject
	FLiveLinkStaticDataStruct StaticData;

	// Frames added to the subject
	TArray<FLiveLinkFrameDataStruct> FrameData;

	// Current frame snapshot of the evaluation
	FLiveLinkSubjectFrameData FrameSnapshot;

	// Name of the subject
	FLiveLinkSubjectKey SubjectKey;

	// Connection settings specified by user
	// May only store settings relevant to the current mode (ELiveLinkSourceMode).
	FLiveLinkCachedSettings CachedSettings;

	// Allow us to track changes to the ref skeleton
	FGuid StaticDataGuid;

	TOptional<FLiveLinkTimeSynchronizationData> TimeSyncData;

	// Time difference between current system time and TimeCode times 
	double SubjectTimeOffset;

	// Last time we read a frame from this subject. Used to determine whether any new incoming
	// frames are usable
	double LastReadTime;

	// Last time a frame was pushed
	double LastPushTime;

	//Cache of the last frame we read from, Used for frame cleanup
	int32 LastReadFrame;
};
