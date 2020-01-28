// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSubject.h"

#include "LiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "ITimedDataInput.h"
#include "LiveLinkFrameInterpolationProcessor.h"
#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkFrameTranslator.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkRole.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkTimedDataInputGroup.h"
#include "LiveLinkTypes.h"


struct FLiveLinkInterpolationInfo;

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
class FLiveLinkSubject : public ILiveLinkSubject, public ITimedDataInput
{
private:
	using Super = ILiveLinkSubject;

public:
	explicit FLiveLinkSubject(TWeakPtr<FLiveLinkTimedDataInputGroup> InTimedDataGroup);
	virtual ~FLiveLinkSubject();

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

	//~Begin ITimedDataSource Interface
public:
	virtual ITimedDataInputGroup* GetGroup() const override;
	virtual ETimedDataInputState GetState() const override;
	virtual FText GetDisplayName() const override;
	virtual FTimedDataInputSampleTime GetOldestDataTime() const override;
	virtual FTimedDataInputSampleTime GetNewestDataTime() const override;
	virtual TArray<FTimedDataInputSampleTime> GetDataTimes() const override;
	virtual ETimedDataInputEvaluationType GetEvaluationType() const override;
	//~ this will change all subjects of this subject's source
	virtual void SetEvaluationType(ETimedDataInputEvaluationType Evaluation) override;
	virtual double GetEvaluationOffsetInSeconds() const override;
	//~ this will change all subjects of this subject's source
	virtual void SetEvaluationOffsetInSeconds(double Offset) override;
	virtual FFrameRate GetFrameRate() const override;
	virtual int32 GetDataBufferSize() const override;
	//~ this will change all subjects of this subject's source
	virtual void SetDataBufferSize(int32 BufferSize) const override;
	virtual bool IsBufferStatsEnabled() const override;
	virtual void SetBufferStatsEnabled(bool bEnable) override;
	virtual int32 GetBufferUnderflowStat() const override;
	virtual int32 GetBufferOverflowStat() const override;
	virtual int32 GetFrameDroppedStat() const override;
	virtual void GetLastEvaluationData(FTimedDataInputEvaluationData& OutEvaluationData) const override;
	virtual void ResetBufferStats() override;
	//~End ITimedDataSrouce Interface

public:
	bool EvaluateFrameAtWorldTime(double InWorldTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame);
	bool EvaluateFrameAtSceneTime(const FQualifiedFrameTime& InSceneTime, TSubclassOf<ULiveLinkRole> InDesiredRole, FLiveLinkSubjectFrameData& OutFrame);

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
	
	// Verify interpolation result to update our internal statistics
	void VerifyInterpolationInfo(const FLiveLinkInterpolationInfo& InterpolationInfo);

	// Populate OutFrame with the latest frame.
	bool GetLatestFrame(FLiveLinkSubjectFrameData& OutFrame);

	void ResetFrame(FLiveLinkSubjectFrameData& OutFrame) const;

	// Update our internal statistics
	void IncreaseFrameDroppedStat();
	void IncreaseBufferUnderFlowStat();
	void IncreaseBufferOverFlowStat();
	void UpdateEvaluationData(const FTimedDataInputEvaluationData& EvaluationData);

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
	
	struct FSubjectEvaluationStatistics
	{
		TAtomic<int32> BufferUnderflow = 0;
		TAtomic<int32> BufferOverflow = 0;
		TAtomic<int32> FrameDrop = 0;
		FTimedDataInputEvaluationData LastEvaluationData;

		FSubjectEvaluationStatistics() = default;
		FSubjectEvaluationStatistics(const FSubjectEvaluationStatistics&) = delete;
		FSubjectEvaluationStatistics& operator=(const FSubjectEvaluationStatistics&) = delete;
	};

	// Static data of the subject
	FLiveLinkStaticDataStruct StaticData;

	// Frames added to the subject
	TArray<FLiveLinkFrameDataStruct> FrameData;

	// Current frame snapshot of the evaluation
	FLiveLinkSubjectFrameData FrameSnapshot;

	// Name of the subject
	FLiveLinkSubjectKey SubjectKey;

	// Timed data input group for the subject
	TWeakPtr<FLiveLinkTimedDataInputGroup> TimedDataGroup;

	// Connection settings specified by user
	FLiveLinkCachedSettings CachedSettings;

	// Last time a frame was pushed
	double LastPushTime = 0.0;

	// Logging stats is enabled by default. If monitor opens at a later stage,previous stats will be able to be seen
	bool bIsStatLoggingEnabled = true;

	// Some stats compiled by the subject.
	FSubjectEvaluationStatistics EvaluationStatistics;
	
	/** 
	 * Evaluation can be done on any thread so we need to protect statistic logging 
	 * Some stats requires more than atomic sized vars so a critical section is used to protect when necessary
	 */
	mutable FCriticalSection StatisticCriticalSection;
};
