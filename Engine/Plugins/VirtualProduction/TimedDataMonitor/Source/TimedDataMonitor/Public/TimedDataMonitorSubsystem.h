// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "ITimedDataInput.h"
#include "Subsystems/EngineSubsystem.h"
#include "Tickable.h"

#include "TimedDataMonitorSubsystem.generated.h"


USTRUCT(BlueprintType)
struct TIMEDDATAMONITOR_API FTimedDataMonitorInputIdentifier
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FGuid Identifier;

public:
	static FTimedDataMonitorInputIdentifier NewIdentifier();

	bool IsValid() const { return Identifier.IsValid(); }

	bool operator== (const FTimedDataMonitorInputIdentifier& Other) const { return Other.Identifier == Identifier; }
	bool operator!= (const FTimedDataMonitorInputIdentifier& Other) const { return Other.Identifier != Identifier; }

	friend uint32 GetTypeHash(const FTimedDataMonitorInputIdentifier& InIdentifier) { return GetTypeHash(InIdentifier.Identifier); }
};


USTRUCT(BlueprintType)
struct TIMEDDATAMONITOR_API FTimedDataMonitorChannelIdentifier
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FGuid Identifier;

public:
	static FTimedDataMonitorChannelIdentifier NewIdentifier();

	bool IsValid() const { return Identifier.IsValid(); }

	bool operator== (const FTimedDataMonitorChannelIdentifier& Other) const { return Other.Identifier == Identifier; }
	bool operator!= (const FTimedDataMonitorChannelIdentifier& Other) const { return Other.Identifier != Identifier; }

	friend uint32 GetTypeHash(const FTimedDataMonitorChannelIdentifier& InIdentifier) { return GetTypeHash(InIdentifier.Identifier); }
};


UENUM()
enum class ETimedDataMonitorInputEnabled : uint8
{
	Disabled,
	Enabled,
	MultipleValues,
};


UENUM()
enum class ETimedDataMonitorCalibrationReturnCode : uint8
{
	/** Success. The values were synchronized. */
	Succeeded,
	/** Failed. The timecode provider doesn't have a proper timecode value. */
	Failed_NoTimecode,
	/** Failed. At least one input is unresponsive. */
	Failed_UnresponsiveInput,
	/** Failed. At least one input doesn't have a defined frame rate. */
	Failed_InvalidFrameRate,
	/** Failed. At least one input doesn't have data buffered. */
	Failed_NoDataBuffered,
	/** Failed. We tried to find a valid offset for the timecode provider and failed. */
	Failed_CanNotCallibrateWithoutJam,
	/** It failed but, we increased the number of buffer of at least one channel. You may retry to see if it works now. */
	Retry_BufferSizeHasBeenIncreased,
};


USTRUCT(BlueprintType)
struct FTimedDataMonitorCalibrationResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Result")
	ETimedDataMonitorCalibrationReturnCode ReturnCode;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Result")
	TArray<FTimedDataMonitorChannelIdentifier> FailureChannelIdentifiers;
};


UENUM()
enum class ETimedDataMonitorJamReturnCode : uint8
{
	/** Success. The values were synchronized. */
	Succeeded,
	/** Failed. The timecode provider was not existing or not synchronized. */
	Failed_NoTimecode,
	/** Failed. At least one channel is unresponsive. */
	Failed_UnresponsiveInput,
	/** Failed. The evaluation type of at least of input doesn't match with what was requested. */
	Failed_EvaluationTypeDoNotMatch,
	/** Failed. The channel doesn't have any data in it's buffer to synchronized with. */
	Failed_NoDataBuffered,
	/** Failed. The channel or buffer size has been increase but it's still not enough. */
	Failed_BufferSizeHaveBeenMaxed,
	/** Failed but we increased the number of buffer of at least one channel. You may retry to see if it works now. */
	Retry_BufferSizeHasBeenIncreased,
};

USTRUCT(BlueprintType)
struct FTimedDataMonitorJamResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Result")
	ETimedDataMonitorJamReturnCode ReturnCode;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Result")
	TArray<FTimedDataMonitorInputIdentifier> FailureInputIdentifiers;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Result")
	TArray<FTimedDataMonitorChannelIdentifier> FailureChannelIdentifiers;
};


/**
 * Structure to facilitate calculating running mean and variance of evaluation distance to buffered samples for a channel
 */
struct FTimedDataChannelEvaluationStatistics
{
	void Update(float DistanceToOldest, float DistanceToNewest);
	void Reset();

	/** Current number of samples used for the running average and variance */
	int32 SampleCount = 0;

	/** Running average of the distance between last evaluation time and oldest sample in the buffer */
	float IncrementalAverageOldestDistance = 0.0f;

	/** Running average of the distance between last evaluation time and newest sample in the buffer */
	float IncrementalAverageNewestDistance = 0.0f;

	/** Running variance of the distance between last evaluation time and oldest sample in the buffer */
	float IncrementalVarianceDistanceNewest = 0.0f;

	/** Running variance of the distance between last evaluation time and newest sample in the buffer */
	float IncrementalVarianceDistanceOldest = 0.0f;

	/** Standard deviation of the distance between last evaluation time and oldest sample in the buffer */
	float DistanceToNewestSTD = 0.0f;

	/** Standard deviation of the distance between last evaluation time and newest sample in the buffer */
	float DistanceToOldestSTD = 0.0f;

	/** Internal counters of squared distance to average to be able to compute running variance */
	double SumSquaredDistanceNewest = 0.0;
	double SumSquaredDistanceOldest = 0.0;

};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTimedDataIdentifierListChangedSignature);


//~ Can be access via GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>()
/**
 * 
 */
UCLASS()
class TIMEDDATAMONITOR_API UTimedDataMonitorSubsystem : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

private:

	struct FTimeDataInputItem
	{
		ITimedDataInput* Input = nullptr;
		TArray<FTimedDataMonitorChannelIdentifier> ChannelIdentifiers;

	public:
		void ResetValue();
	};

	struct FTimeDataChannelItem
	{
		ITimedDataInputChannel* Channel = nullptr;
		bool bEnabled = true;
		FTimedDataMonitorInputIdentifier InputIdentifier;
		FTimedDataChannelEvaluationStatistics Statistics;

	public:
		void ResetValue();
	};

public:
	//~ Begin USubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem implementation

	//~ Begin FTickableGameObject implementation
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	//~ End FTickableGameObject implementation

public:
	/** Delegate of when an element is added or removed. */
	UPROPERTY(BlueprintAssignable, Category = "Timed Data Monitor", meta=(DisplayName="OnSourceIdentifierListChanged"))
	FTimedDataIdentifierListChangedSignature OnIdentifierListChanged_Dynamic;

public:
	/** Delegate of when an element is added or removed. */
	FSimpleMulticastDelegate& OnIdentifierListChanged() { return OnIdentifierListChanged_Delegate; }

	/** Get the interface for a specific input identifier. */
	ITimedDataInput* GetTimedDataInput(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the interface for a specific channel identifier. */
	ITimedDataInputChannel* GetTimedDataChannel(const FTimedDataMonitorChannelIdentifier& Identifier);

public:
	/** Get the list of all the inputs. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	TArray<FTimedDataMonitorInputIdentifier> GetAllInputs();

	/** Get the list of all the channels. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	TArray<FTimedDataMonitorChannelIdentifier> GetAllChannels();

	/**
	 * Set evaluation offset for all inputs and the engine's timecode provider to align all the buffers.
	 * If there is no data available, it may increase the buffer size of an input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	FTimedDataMonitorCalibrationResult CalibrateWithTimecodeProvider();

	/** Assume all data samples were produce at the same time and align them with the current platform's time */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	FTimedDataMonitorJamResult JamInputs(ETimedDataInputEvaluationType EvaluationType);

	/** Reset the stat of all the inputs. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	void ResetAllBufferStats();

	/** Return true if the identifier is a valid input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	bool DoesInputExist(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Is the input enabled in the monitor. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	ETimedDataMonitorInputEnabled GetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Set all channels for the input enabled in the monitor. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void SetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier, bool bInEnabled);

	/** Return the display name of an input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FText GetInputDisplayName(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Return the list of all channels that are part of the input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	TArray<FTimedDataMonitorChannelIdentifier> GetInputChannels(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get how the input is evaluated type. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	ETimedDataInputEvaluationType GetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Set how the input is evaluated type. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void SetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier, ETimedDataInputEvaluationType Evaluation);

	/** Get the offset in seconds or frames (see GetEvaluationType) used at evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	float GetInputEvaluationOffsetInSeconds(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Set the offset in seconds or frames (see GetEvaluationType) used at evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void SetInputEvaluationOffsetInSeconds(const FTimedDataMonitorInputIdentifier& Identifier, float Seconds);

	/** Get the frame rate at which the samples is produce. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FFrameRate GetInputFrameRate(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the oldest sample time of all the channel in this input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FTimedDataChannelSampleTime GetInputOldestDataTime(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the latest sample time of all the channel in this input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FTimedDataChannelSampleTime GetInputNewestDataTime(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Does the channel support a different buffer size than it's input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	bool IsDataBufferSizeControlledByInput(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the size of the buffer used by the input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	int32 GetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Set the size of the buffer used by the input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void SetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier, int32 BufferSize);

	/** Get the worst state of all the channel state of that input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	ETimedDataInputState GetInputState(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Returns the standard deviation of the distance, in seconds, between evaluation time and newest sample */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	float GetInputEvaluationDistanceToNewestSampleStandardDeviation(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Returns the standard deviation of the distance, in seconds, between evaluation time and oldest sample */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	float GetInputEvaluationDistanceToOldestSampleStandardDeviation(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Return true if the identifier is a valid channel. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	bool DoesChannelExist(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Is the channel enabled in the monitor. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	bool IsChannelEnabled(const FTimedDataMonitorChannelIdentifier& Identifier);

	/**
	 * Enable or disable an input from the monitor.
	 * The input will still be evaluated but stats will not be tracked and the will not be used for calibration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	void SetChannelEnabled(const FTimedDataMonitorChannelIdentifier& Identifier, bool bEnabled);

	/** Return the input of this channel. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	FTimedDataMonitorInputIdentifier GetChannelInput(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Return the display name of an input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	FText GetChannelDisplayName(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Get the worst state of all the input state of that channel. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	ETimedDataInputState GetChannelState (const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Get the channel oldest sample time. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	FTimedDataChannelSampleTime GetChannelOldestDataTime(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Get the channel latest sample time. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	FTimedDataChannelSampleTime GetChannelNewestDataTime(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Get the number of data samples available. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	int32 GetChannelNumberOfSamples(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** If the channel does support it, get the current maximum sample count of channel. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	int32 GetChannelDataBufferSize(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** If the channel does support it, set the maximum sample count of the channel. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	void SetChannelDataBufferSize(const FTimedDataMonitorChannelIdentifier& Identifier, int32 BufferSize);

	/** Returns the number of buffer underflows detected by that input since the last reset. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	int32 GetChannelBufferUnderflowStat(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Returns the number of buffer overflows detected by that input since the last reset. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	int32 GetChannelBufferOverflowStat(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Returns the number of frames dropped by that input since the last reset. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	int32 GetChannelFrameDroppedStat(const FTimedDataMonitorChannelIdentifier& Identifier);
	
	/** 
	 * Retrieves information about last evaluation 
	 * Returns true if identifier was found
	 */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	void GetChannelLastEvaluationDataStat(const FTimedDataMonitorChannelIdentifier& Identifier, FTimedDataInputEvaluationData& Result);

	/** Returns the average distance, in seconds, between evaluation time and newest sample */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	float GetChannelEvaluationDistanceToNewestSampleMean(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Returns the average distance, in seconds, between evaluation time and oldest sample */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	float GetChannelEvaluationDistanceToOldestSampleMean(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Returns the standard deviation of the distance, in seconds, between evaluation time and newest sample */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	float GetChannelEvaluationDistanceToNewestSampleStandardDeviation(const FTimedDataMonitorChannelIdentifier& Identifier);

	/** Returns the standard deviation of the distance, in seconds, between evaluation time and oldest sample */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Channel")
	float GetChannelEvaluationDistanceToOldestSampleStandardDeviation(const FTimedDataMonitorChannelIdentifier& Identifier);

private:
	void BuildSourcesListIfNeeded();

	void OnTimedDataSourceCollectionChanged();

	double GetSeconds(ETimedDataInputEvaluationType Evaluation, const FTimedDataChannelSampleTime& SampleTime) const;
	double CalculateAverageInDeltaTimeBetweenSample(ETimedDataInputEvaluationType Evaluation, const TArray<FTimedDataChannelSampleTime>& SampleTimes) const;

	/** Update internal statistics for each enabled channel */
	void UpdateEvaluationStatistics();

private:
	bool bRequestSourceListRebuilt = false;
	TMap<FTimedDataMonitorInputIdentifier, FTimeDataInputItem> InputMap;
	TMap<FTimedDataMonitorChannelIdentifier, FTimeDataChannelItem> ChannelMap;
	FSimpleMulticastDelegate OnIdentifierListChanged_Delegate;
};
