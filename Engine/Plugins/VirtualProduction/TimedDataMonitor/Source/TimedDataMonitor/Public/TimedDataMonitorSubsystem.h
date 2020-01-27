// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ITimedDataInput.h"
#include "Subsystems/EngineSubsystem.h"

#include "TimedDataMonitorSubsystem.generated.h"


USTRUCT(BlueprintType)
struct TIMEDDATAMONITOR_API FTimedDataMonitorGroupIdentifier
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FGuid Group;

public:
	static FTimedDataMonitorGroupIdentifier NewIdentifier();

	bool IsValidGroup() const { return Group.IsValid(); }

	bool operator== (const FTimedDataMonitorGroupIdentifier& Other) const { return Other.Group == Group; }
	bool operator!= (const FTimedDataMonitorGroupIdentifier& Other) const { return Other.Group != Group; }

	friend uint32 GetTypeHash(const FTimedDataMonitorGroupIdentifier& Identifier) { return GetTypeHash(Identifier.Group); }
};

USTRUCT(BlueprintType)
struct TIMEDDATAMONITOR_API FTimedDataMonitorInputIdentifier
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FGuid Input;

public:
	static FTimedDataMonitorInputIdentifier NewIdentifier();

	bool IsValidInput() const { return Input.IsValid(); }

	bool operator== (const FTimedDataMonitorInputIdentifier& Other) const { return Other.Input == Input; }
	bool operator!= (const FTimedDataMonitorInputIdentifier& Other) const { return Other.Input != Input; }

	friend uint32 GetTypeHash(const FTimedDataMonitorInputIdentifier& Identifier) { return GetTypeHash(Identifier.Input); }
};


UENUM()
enum class ETimedDataMonitorGroupEnabled : uint8
{
	Disabled,
	Enabled,
	MultipleValues,
};


UENUM()
enum class ETimedDataMonitorCallibrationReturnCode : uint8
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
	/** It failed but, we increased the number of buffer for you of at least one source. You may retry to see if it works now. */
	Retry_BufferSizeHasBeenIncreased,
};


USTRUCT(BlueprintType)
struct FTimedDataMonitorCallibrationResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Result")
	ETimedDataMonitorCallibrationReturnCode ReturnCode;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Result")
	TArray<FTimedDataMonitorInputIdentifier> FailureInputIdentifiers;
};


UENUM()
enum class ETimedDataMonitorJamReturnCode : uint8
{
	/** Success. The values were synchronized. */
	Succeeded,
	/** Failed. The timecode provider was not existing or not synchronized. */
	Failed_NoTimecode,
	/** Failed. At least one input is unresponsive. */
	Failed_UnresponsiveInput,
	/** Failed. The evaluation type of at least of input doesn't match with what was requested. */
	Failed_EvaluationTypeDoNotMatch,
	/** Failed. The input doesn't have any data in it's buffer to synchronized with. */
	Failed_NoDataBuffered,
};

USTRUCT(BlueprintType)
struct FTimedDataMonitorJamResult
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Result")
	ETimedDataMonitorJamReturnCode ReturnCode;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Result")
	TArray<FTimedDataMonitorInputIdentifier> FailureInputIdentifiers;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTimedDataIdentifierListChangedSignature);


//~ Can be access via GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>()
/**
 * 
 */
UCLASS()
class TIMEDDATAMONITOR_API UTimedDataMonitorSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

private:

	struct FTimeDataInputItem
	{
		ITimedDataInput* Input = nullptr;
		FTimedDataMonitorGroupIdentifier GroupIdentifier;
		bool bEnabled = true;

	public:
		bool HasGroup() const;
		void ResetValue();
	};

	struct FTimeDataInputItemGroup
	{
		ITimedDataInputGroup* Group = nullptr;
		TArray<FTimedDataMonitorInputIdentifier> InputIdentifiers;

	public:
		void ResetValue();
	};

public:
	//~ Begin USubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem implementation

public:
	/** Delegate of when an element is added or removed. */
	UPROPERTY(BlueprintAssignable, Category = "Timed Data Monitor", meta=(DisplayName="OnSourceIdentifierListChanged"))
	FTimedDataIdentifierListChangedSignature OnIdentifierListChanged_Dynamic;

public:
	/** Delegate of when an element is added or removed. */
	FSimpleMulticastDelegate& OnIdentifierListChanged() { return OnIdentifierListChanged_Delegate; }

	/** Get the interface for a specific input identifier. */
	ITimedDataInput* GetTimedDataInput(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the interface for a specific group identifier. */
	ITimedDataInputGroup* GetTimedDataInputGroup(const FTimedDataMonitorGroupIdentifier& Identifier);

public:
	/** Get the list of all the groups. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	TArray<FTimedDataMonitorGroupIdentifier> GetAllGroups();

	/** Get the list of all the inputs. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	TArray<FTimedDataMonitorInputIdentifier> GetAllInputs();

	/**
	 * Set evaluation offset for all inputs and the engine's timecode provider to align all the buffers.
	 * If there is no data available, it may increase the buffer size of an input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	FTimedDataMonitorCallibrationResult CalibrateWithTimecodeProvider();

	/** Assume all data samples were produce at the same time and align them with the current platform's time */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	FTimedDataMonitorJamResult JamInputs(ETimedDataInputEvaluationType EvaluationType);

	/** Reset the stat of all the inputs. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor")
	void ResetAllBufferStats();

	/** Return true if the identifier is a valid group. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	bool DoesGroupExist(const FTimedDataMonitorGroupIdentifier& Identifier);

	/** Return the display name of a group. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	FText GetGroupDisplayName(const FTimedDataMonitorGroupIdentifier& Identifier);

	/** Get the min and max value of the inputs data buffer size. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	void GetGroupDataBufferSize(const FTimedDataMonitorGroupIdentifier& Identifier, int32& OutMinBufferSize, int32& OutMaxBufferSize);

	/** Set the data buffer size of all the input in that group. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	void SetGroupDataBufferSize(const FTimedDataMonitorGroupIdentifier& Identifier, int32 BufferSize);

	/** Get the worst state of all the input state of that group. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	ETimedDataInputState GetGroupState (const FTimedDataMonitorGroupIdentifier& Identifier);

	/** Reset the stat of all the inputs of that group. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	void ResetGroupBufferStats(const FTimedDataMonitorGroupIdentifier& Identifier);

	/** Return true if all inputs in the group are enabled. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	ETimedDataMonitorGroupEnabled GetGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier);

	/** Enable or disable all inputs in the group. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Group")
	void SetGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier, bool bEnabled);

	/** Return true if the identifier is a valid input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	bool DoesInputExist(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Return the display name of an input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FText GetInputDisplayName(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Return the group in which the input is part of. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FTimedDataMonitorGroupIdentifier GetInputGroup(const FTimedDataMonitorInputIdentifier& Identifier);

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

	/** Get the current input state. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	ETimedDataInputState GetInputState(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the frame rate at which the samples is produce. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FFrameRate GetInputFrameRate(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the input latest sample time at which it should be evaluated. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	FTimedDataInputSampleTime GetInputNewestDataTime(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Get the size of the buffer used by the input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	int32 GetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Set the size of the buffer used by the input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void SetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier, int32 BufferSize);

	/** Reset the stat of the input. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void ResetInputBufferStats(const FTimedDataMonitorInputIdentifier& Identifier);

	/** Is the input enabled in the monitor. */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	bool IsInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier);

	/**
	 * Enable or disable an input from the monitor.
	 * The input will still be evaluated but stats will not be tracked and the will not be used for calibration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Timed Data Monitor|Input")
	void SetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier, bool bEnabled);

private:
	void BuildSourcesListIfNeeded();

	void OnTimedDataSourceCollectionChanged();

private:
	bool bRequestSourceListRebuilt = false;
	TMap<FTimedDataMonitorInputIdentifier, FTimeDataInputItem> InputMap;
	TMap<FTimedDataMonitorGroupIdentifier, FTimeDataInputItemGroup> GroupMap;
	FTimedDataMonitorGroupIdentifier OtherGroupIdentifier;
	FSimpleMulticastDelegate OnIdentifierListChanged_Delegate;
};
