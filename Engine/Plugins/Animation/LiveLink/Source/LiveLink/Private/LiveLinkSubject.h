// Copyright Epic Games, Inc. All Rights Reserved.

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
	virtual FLiveLinkStaticDataStruct& GetStaticData() override { return StaticData; }
	virtual const FLiveLinkStaticDataStruct& GetStaticData() const override { return StaticData; }
	virtual TArray<FLiveLinkTime> GetFrameTimes() const override;
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
	bool IsTimeSynchronized() const;

	double GetLastPushTime() const { return LastPushTime; }

private:
	int32 FindNewFrame_WorldTime(const FLiveLinkWorldTime& FrameTime) const;
	int32 FindNewFrame_WorldTimeInternal(const FLiveLinkWorldTime& FrameTime) const;
	int32 FindNewFrame_SceneTime(const FQualifiedFrameTime& FrameTime, const FLiveLinkWorldTime& WorldTime) const;
	int32 FindNewFrame_Latest(const FLiveLinkWorldTime& FrameTime) const;

	// Reorder frame with the same timecode and create subframes
	void AdjustSubFrame_SceneTime(int32 FrameIndex);

	// Populate OutFrame with a frame based off of the supplied time and our own offsets
	bool GetFrameAtWorldTime(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);
	bool GetFrameAtWorldTime_Closest(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);
	bool GetFrameAtWorldTime_Interpolated(const double InSeconds, FLiveLinkSubjectFrameData& OutFrame);

	// Populate OutFrame with a frame based off of the supplied scene time.
	bool GetFrameAtSceneTime(const FQualifiedFrameTime& InSceneTime, FLiveLinkSubjectFrameData& OutFrame);
	bool GetFrameAtSceneTime_Closest(const FQualifiedFrameTime& InSceneTime, FLiveLinkSubjectFrameData& OutFrame);
	bool GetFrameAtSceneTime_Interpolated(const FQualifiedFrameTime& InSceneTime, FLiveLinkSubjectFrameData& OutFrame);

	// Populate OutFrame with the latest frame.
	bool GetLatestFrame(FLiveLinkSubjectFrameData& OutFrame);

	void ResetFrame(FLiveLinkSubjectFrameData& OutFrame) const;

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
		ELiveLinkSourceMode SourceMode = ELiveLinkSourceMode::EngineTime;
		FLiveLinkSourceBufferManagementSettings BufferSettings;
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
	FLiveLinkCachedSettings CachedSettings;

	// Last time a frame was pushed
	double LastPushTime = 0.0;

	//Cache of the last frame we used to build the snapshot, used to clean frames
	int32 LastReadFrame;

#if WITH_EDITORONLY_DATA
	int32 SnapshotIndex = INDEX_NONE;
	int32 NumberOfBufferAtSnapshot = 0;
#endif
};
