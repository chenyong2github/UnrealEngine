// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimedDataMonitorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/TimecodeProvider.h"
#include "HAL/PlatformTime.h"
#include "ITimeManagementModule.h"
#include "Misc/App.h"
#include "Misc/QualifiedFrameTime.h"
#include "Stats/Stats2.h"
#include "TimedDataInputCollection.h"


static TAutoConsoleVariable<bool> CVarEnableTimedDataMonitorSubsystemStats(TEXT("TimedDataMonitor.EnableStatUpdate"), 1, TEXT("Enable calculating evaluation statistics of all registered channels."));

#define LOCTEXT_NAMESPACE "TimedDataMonitorSubsystem"

/**
 *
 */
FTimedDataMonitorInputIdentifier FTimedDataMonitorInputIdentifier::NewIdentifier()
{
	FTimedDataMonitorInputIdentifier Item;
	Item.Identifier = FGuid::NewGuid();
	return Item;
}


/**
 *
 */
FTimedDataMonitorChannelIdentifier FTimedDataMonitorChannelIdentifier::NewIdentifier()
{
	FTimedDataMonitorChannelIdentifier Item;
	Item.Identifier = FGuid::NewGuid();
	return Item;
}


/**
 * 
 */
void UTimedDataMonitorSubsystem::FTimeDataInputItem::ResetValue()
{
	ChannelIdentifiers.Reset();
}

/**
 *
 */
void UTimedDataMonitorSubsystem::FTimeDataChannelItem::ResetValue()
{
	Statistics.Reset();
}

/**
 * 
 */
void UTimedDataMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	bRequestSourceListRebuilt = true;
	ITimeManagementModule::Get().GetTimedDataInputCollection().OnCollectionChanged().AddUObject(this, &UTimedDataMonitorSubsystem::OnTimedDataSourceCollectionChanged);
	Super::Initialize(Collection);
}


void UTimedDataMonitorSubsystem::Deinitialize()
{
	if (ITimeManagementModule::IsAvailable())
	{
		ITimeManagementModule::Get().GetTimedDataInputCollection().OnCollectionChanged().RemoveAll(this);
	}

	bRequestSourceListRebuilt = true;
	InputMap.Reset();
	ChannelMap.Reset();
	OnIdentifierListChanged_Delegate.Broadcast();
	OnIdentifierListChanged_Dynamic.Broadcast();

	Super::Deinitialize();
}


ITimedDataInput* UTimedDataMonitorSubsystem::GetTimedDataInput(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input;
	}

	return nullptr;
}


ITimedDataInputChannel* UTimedDataMonitorSubsystem::GetTimedDataChannel(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* ChannelItem = ChannelMap.Find(Identifier))
	{
		return ChannelItem->Channel;
	}

	return nullptr;
}


TArray<FTimedDataMonitorInputIdentifier> UTimedDataMonitorSubsystem::GetAllInputs()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorInputIdentifier> Result;
	InputMap.GenerateKeyArray(Result);
	return Result;
}


TArray<FTimedDataMonitorChannelIdentifier> UTimedDataMonitorSubsystem::GetAllChannels()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorChannelIdentifier> Result;
	ChannelMap.GenerateKeyArray(Result);
	return Result;
}


TArray<FTimedDataMonitorChannelIdentifier> UTimedDataMonitorSubsystem::GetAllEnabledChannels()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorChannelIdentifier> Result;
	Result.Reset(ChannelMap.Num());
	for (const auto& ChannelMapItt : ChannelMap)
	{
		if (ChannelMapItt.Value.bEnabled)
		{
			Result.Add(ChannelMapItt.Key);
		}
	}
	return Result;
}


/** The algo for Calibration and TimeCorrection will use those data for their examples and comments. */
// EvaluationTime == 50.
//A1 10  11
//A2                   48  49  50  51
//A3     11  12
//B1                                                 99  100
//B2                                                     100
//B3                                                     100  101
//C1 10  11
//C2                                                     100
//D1                       49  50  51
//D2                   48  49  50  51
namespace TimedDataMonitorSubsystem
{
	struct FChannelSampleMinMax
	{
		FTimedDataMonitorChannelIdentifier ChannelIdentifier;
		FTimedDataChannelSampleTime Min;
		FTimedDataChannelSampleTime Max;
	};

	struct FSmallestBiggestSample
	{
		double SmallestMinInSeconds = TNumericLimits<double>::Max();
		double BiggestMaxInSeconds = TNumericLimits<double>::Lowest();
		double BiggerMinInSeconds = TNumericLimits<double>::Lowest();
		double SmallestMaxInSeconds = TNumericLimits<double>::Max();
	};

	double GetSeconds(ETimedDataInputEvaluationType Evaluation, const FTimedDataChannelSampleTime& SampleTime)
	{
		return Evaluation == ETimedDataInputEvaluationType::Timecode ? SampleTime.Timecode.AsSeconds() : SampleTime.PlatformSecond;
	}

	TArray<FChannelSampleMinMax> GetChannelsMinMax(UTimedDataMonitorSubsystem* TimedDataMonitor, const TArray<FTimedDataMonitorChannelIdentifier>& Channels)
	{
		TArray<FChannelSampleMinMax> Result;
		Result.Reset(Channels.Num());
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : Channels)
		{
			check(TimedDataMonitor->GetChannelNumberOfSamples(ChannelIdentifier) > 0);
			FChannelSampleMinMax NewItem;
			NewItem.ChannelIdentifier = ChannelIdentifier;
			NewItem.Min = TimedDataMonitor->GetChannelOldestDataTime(ChannelIdentifier);
			NewItem.Max = TimedDataMonitor->GetChannelNewestDataTime(ChannelIdentifier);
			Result.Add(NewItem);
		}
		return Result;
	}

	FSmallestBiggestSample GetSmallestBiggestSample(ETimedDataInputEvaluationType EvaluationType, const TArray<FChannelSampleMinMax>& Channels)
	{
		FSmallestBiggestSample Result;
		for (const FChannelSampleMinMax& ChannelItt : Channels)
		{
			Result.SmallestMinInSeconds = FMath::Min(GetSeconds(EvaluationType, ChannelItt.Min), Result.SmallestMinInSeconds);	//A == 10, B == 99, C == 10, D == 48
			Result.BiggestMaxInSeconds = FMath::Max(GetSeconds(EvaluationType, ChannelItt.Max), Result.BiggestMaxInSeconds);	//A == 51, B == 101, C == 100, D == 51

			Result.BiggerMinInSeconds = FMath::Max(GetSeconds(EvaluationType, ChannelItt.Min), Result.BiggerMinInSeconds);		//A == 48, B == 100, C == 10, D == 49
			Result.SmallestMaxInSeconds = FMath::Min(GetSeconds(EvaluationType, ChannelItt.Max), Result.SmallestMaxInSeconds);	//A == 11, B == 100, C == 11, D == 51
		}
		return Result;
	}

	bool IsInRange(ETimedDataInputEvaluationType EvaluationType, const FChannelSampleMinMax& SampleMinMax, double InSeconds)
	{
		return InSeconds >= TimedDataMonitorSubsystem::GetSeconds(EvaluationType, SampleMinMax.Min)
			&& InSeconds <= TimedDataMonitorSubsystem::GetSeconds(EvaluationType, SampleMinMax.Max);
	}

	bool IsInRange(ETimedDataInputEvaluationType EvaluationType, const TArray<FChannelSampleMinMax>& ChannelSamplesMinMax, double InSeconds)
	{
		if (ChannelSamplesMinMax.Num() == 0)
		{
			return false;
		}
		for (const TimedDataMonitorSubsystem::FChannelSampleMinMax& SampleMinMax : ChannelSamplesMinMax)
		{
			if (!IsInRange(EvaluationType, SampleMinMax, InSeconds))
			{
				return false;
			}
		}
		return true;
	}

	double CalculateAverageInDeltaTimeBetweenSample(ETimedDataInputEvaluationType EvaluationType, const TArray<FTimedDataChannelSampleTime>& SampleTimes)
	{
		double Average = 0.0;
		if (SampleTimes.Num() >= 2)
		{
			// Get the average of the last 10 samples in seconds
			const int32 AvgCounter = FMath::Min(SampleTimes.Num() - 1, 10 - 1);

			const int32 SampleTimeNum = SampleTimes.Num();

			for (int32 Index = 1; Index <= AvgCounter; ++Index)
			{
				double Delta = GetSeconds(EvaluationType, SampleTimes[SampleTimeNum - Index]) - GetSeconds(EvaluationType, SampleTimes[SampleTimeNum - Index - 1]);
				Average += (Delta - Average) / (double)Index;
			}
		}
		else
		{
			Average = FApp::GetDeltaTime(); // was not able to find a correct delta time. guess one.
		}
		return Average;
	}

	double CalculateAverageInDeltaTimeBetweenSample(UTimedDataMonitorSubsystem* TimedDataMonitor, ETimedDataInputEvaluationType EvaluationType, const TArray<FTimedDataMonitorChannelIdentifier>& ChannelIdentifiers)
	{
		double AverageBetweenSample = 0.0;
		int32 AverageCounter = 0;
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : ChannelIdentifiers)
		{
			TArray<FTimedDataChannelSampleTime> AllSamplesTimes = TimedDataMonitor->GetTimedDataChannel(ChannelIdentifier)->GetDataTimes();
			if (AllSamplesTimes.Num() > 1)
			{
				const double CurrentAverageBetweenSample = CalculateAverageInDeltaTimeBetweenSample(EvaluationType, AllSamplesTimes);

				++AverageCounter;
				AverageBetweenSample += (CurrentAverageBetweenSample - AverageBetweenSample) / (double)AverageCounter;
			}
		}
		if (FMath::IsNearlyZero(AverageBetweenSample))
		{
			AverageBetweenSample = FApp::GetDeltaTime();
		}
		return AverageBetweenSample;
	}
}


// With [A,B,C,D] We are not able to calibrate. (C:100-11 is too big of a gab)
// With [A,D] We need to increase the buffer size of A2 (48-11), D1 and D2. Set the TimecodeProvider offset to 39 (50-11)
FTimedDataMonitorCalibrationResult UTimedDataMonitorSubsystem::CalibrateWithTimecodeProvider()
{
	BuildSourcesListIfNeeded();

	FTimedDataMonitorCalibrationResult Result;
	Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Succeeded;


	UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
	FQualifiedFrameTime CurrentFrameTime;
	if (CurrentTimecodeProvider == nullptr || CurrentTimecodeProvider->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized || !FApp::GetCurrentFrameTime().IsSet())
	{
		Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_NoTimecode;
		return Result;
	}
	CurrentFrameTime = CurrentTimecodeProvider->GetQualifiedFrameTime(); // Without offset

	// Collect enabled input
	struct FEnabledInput
	{
		FTimedDataMonitorInputIdentifier InputIdentifier;
		TArray<FTimedDataMonitorChannelIdentifier> ChannelIdentifiers;
	};
	TArray<FEnabledInput> AllValidInputIndentifiers;
	AllValidInputIndentifiers.Reset(InputMap.Num());

	for (const auto& InputItt : InputMap)
	{
		if (ETimedDataMonitorInputEnabled::Disabled != GetInputEnabled(InputItt.Key))
		{
			FEnabledInput NewInput;
			NewInput.InputIdentifier = InputItt.Key;
			NewInput.ChannelIdentifiers.Reset(InputItt.Value.ChannelIdentifiers.Num());
			for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItt.Value.ChannelIdentifiers)
			{
				if (ChannelMap[ChannelIdentifier].bEnabled)
				{
					NewInput.ChannelIdentifiers.Add(ChannelIdentifier);
				}
			}
			AllValidInputIndentifiers.Add(MoveTemp(NewInput));
		}
	}

	// Are they responsive?
	for (const FEnabledInput& EnabledInput : AllValidInputIndentifiers)
	{
		if (GetInputState(EnabledInput.InputIdentifier) != ETimedDataInputState::Connected)
		{
			Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_UnresponsiveInput;
			Result.FailureInputIdentifiers.Add(EnabledInput.InputIdentifier);
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorCalibrationReturnCode::Failed_UnresponsiveInput)
	{
		return Result;
	}

	// Are they in evaluation type timecode?
	for (const FEnabledInput& EnabledInput : AllValidInputIndentifiers)
	{
		if (GetInputEvaluationType(EnabledInput.InputIdentifier) != ETimedDataInputEvaluationType::Timecode)
		{
			Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_InvalidEvaluationType;
			Result.FailureInputIdentifiers.Add(EnabledInput.InputIdentifier);
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorCalibrationReturnCode::Failed_InvalidEvaluationType)
	{
		return Result;
	}

	// Do they have invalid frame rate?
	for (const FEnabledInput& EnabledInput : AllValidInputIndentifiers)
	{
		if (InputMap[EnabledInput.InputIdentifier].Input->GetFrameRate() == ITimedDataInput::UnknownFrameRate)
		{
			Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_InvalidFrameRate;
			Result.FailureInputIdentifiers.Add(EnabledInput.InputIdentifier);
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorCalibrationReturnCode::Failed_InvalidFrameRate)
	{
		return Result;
	}

	// Do they all have buffers?
	for (const FEnabledInput& EnabledInput : AllValidInputIndentifiers)
	{
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : EnabledInput.ChannelIdentifiers)
		{
			if (ChannelMap[ChannelIdentifier].Channel->GetNumberOfSamples() <= 0)
			{
				Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_NoDataBuffered;
				Result.FailureInputIdentifiers.AddUnique(EnabledInput.InputIdentifier);
			}
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorCalibrationReturnCode::Failed_NoDataBuffered)
	{
		return Result;
	}

	// Collect the min and max of all inputs
	TArray<TimedDataMonitorSubsystem::FChannelSampleMinMax> AllInputsSampleMinMax;
	for (const FEnabledInput& EnabledInput : AllValidInputIndentifiers)
	{
		AllInputsSampleMinMax.Append(TimedDataMonitorSubsystem::GetChannelsMinMax(this, EnabledInput.ChannelIdentifiers));
	}

	// Test if all the samples are in the range of the EvaluationTime
	const double EvaluationTime = CurrentFrameTime.AsSeconds();
	const bool bAllChannelInRangeOfEvaluationTime = TimedDataMonitorSubsystem::IsInRange(ETimedDataInputEvaluationType::Timecode, AllInputsSampleMinMax, EvaluationTime);
	if (bAllChannelInRangeOfEvaluationTime)
	{
		CurrentTimecodeProvider->FrameDelay = 0.0f;
		Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Succeeded;
		return Result;
	}

	// Is there a range of data that everyone is happy with [A,D] == 11
	TimedDataMonitorSubsystem::FSmallestBiggestSample InputsSmallestBiggestSample = TimedDataMonitorSubsystem::GetSmallestBiggestSample(ETimedDataInputEvaluationType::Timecode, AllInputsSampleMinMax);

	const bool bAllChannelInRangeOfSmallestMax = TimedDataMonitorSubsystem::IsInRange(ETimedDataInputEvaluationType::Timecode, AllInputsSampleMinMax, InputsSmallestBiggestSample.SmallestMaxInSeconds);
	if (bAllChannelInRangeOfSmallestMax)
	{
		const double OffsetInSeconds = EvaluationTime - InputsSmallestBiggestSample.SmallestMaxInSeconds; // If [A,D], 50-11=39
		CurrentTimecodeProvider->FrameDelay = ITimedDataInput::ConvertSecondOffsetInFrameOffset(OffsetInSeconds, CurrentFrameTime.Rate);
		Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Succeeded;
		return Result;
	}

	Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_IncreaseBufferSize;
	return Result;
}


// For InputA, we should increase the buffer size of A2 (48-11) and set an offset so that 11 == 50
// For InputB, we set an offset so that 100 == 50
// For InputC, we cannot find anything since the difference is too big. (100 - 11) Failed.
// For InputD, we set an offset so that 50 == 50
FTimedDataMonitorTimeCorrectionResult UTimedDataMonitorSubsystem::ApplyTimeCorrection(const FTimedDataMonitorInputIdentifier& InputIdentifier)
{
	BuildSourcesListIfNeeded();

	FTimedDataMonitorTimeCorrectionResult Result;
	Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_InvalidInput;

	FTimeDataInputItem* InputItem = InputMap.Find(InputIdentifier);
	if (InputItem == nullptr)
	{
		Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_InvalidInput;
		return Result;
	}

	const ETimedDataInputEvaluationType EvaluationType = InputItem->Input->GetEvaluationType();
	const double CurrentPlatformTime = FApp::GetCurrentTime();
	UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
	FQualifiedFrameTime CurrentFrameTime;
	if (EvaluationType == ETimedDataInputEvaluationType::Timecode)
	{
		if (CurrentTimecodeProvider == nullptr || CurrentTimecodeProvider->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized || !FApp::GetCurrentFrameTime().IsSet())
		{
			Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_NoTimecode;
			return Result;
		}

		CurrentFrameTime = CurrentTimecodeProvider->GetQualifiedFrameTime();
	}

	// Collect all enabled channel
	TArray<FTimedDataMonitorChannelIdentifier> AllValidChannelIdentifiers;
	AllValidChannelIdentifiers.Reset(InputItem->ChannelIdentifiers.Num());
	for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
	{
		if (ChannelMap[ChannelIdentifier].bEnabled)
		{
			AllValidChannelIdentifiers.Add(ChannelIdentifier);
		}
	}

	// Test all Channels for Failed_UnresponsiveInput
	for (const FTimedDataMonitorChannelIdentifier& ChannelId : AllValidChannelIdentifiers)
	{
		if (ChannelMap[ChannelId].Channel->GetState() != ETimedDataInputState::Connected)
		{
			Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_UnresponsiveInput;
			Result.FailureChannelIdentifiers.Add(ChannelId);
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorTimeCorrectionReturnCode::Failed_UnresponsiveInput)
	{
		return Result;
	}

	if (EvaluationType == ETimedDataInputEvaluationType::None)
	{
		// Set the evaluation offset of everyone to 0
		InputItem->Input->SetEvaluationOffsetInSeconds(0.0);

		Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Succeeded;
		return Result;
	}

	for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : AllValidChannelIdentifiers)
	{
		const FTimeDataChannelItem& ChannelItem = ChannelMap[ChannelIdentifier];
		if (ChannelItem.Channel->GetNumberOfSamples() <= 0)
		{
			Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_NoDataBuffered;
			Result.FailureChannelIdentifiers.Add(ChannelIdentifier);
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorTimeCorrectionReturnCode::Failed_NoDataBuffered)
	{
		return Result;
	}

	// Collect all DataTimes
	TArray<TimedDataMonitorSubsystem::FChannelSampleMinMax> ChannelSamplesMinMax = TimedDataMonitorSubsystem::GetChannelsMinMax(this, AllValidChannelIdentifiers);
	TimedDataMonitorSubsystem::FSmallestBiggestSample SmallestBiggestSample = TimedDataMonitorSubsystem::GetSmallestBiggestSample(EvaluationType, ChannelSamplesMinMax);

	// Find what section that matches for each channel
	const double EvaluationTime = EvaluationType == ETimedDataInputEvaluationType::Timecode ? CurrentFrameTime.AsSeconds() : CurrentPlatformTime;

	//@todo use the stat when we are confident that they works properly
	//const double DistanceToNewestSTD = GetInputEvaluationDistanceToNewestSampleStandardDeviation(InputId) * NumberOfSigmaOfSignification;
	const double ExtraBufferWhenJamming = 0.25;

	// Test if all the samples are in the range of the EvaluationTime 
	const bool bAllChannelInRangeOfEvaluationTime = TimedDataMonitorSubsystem::IsInRange(EvaluationType, ChannelSamplesMinMax, EvaluationTime - ExtraBufferWhenJamming);
	if (bAllChannelInRangeOfEvaluationTime)
	{
		// Set the evaluation offset for later (case D)
		InputItem->Input->SetEvaluationOffsetInSeconds(ExtraBufferWhenJamming);

		Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Succeeded;
		return Result;
	}

	const bool bAllChannelInRangeOfSmallestMax = TimedDataMonitorSubsystem::IsInRange(EvaluationType, ChannelSamplesMinMax, SmallestBiggestSample.SmallestMaxInSeconds - ExtraBufferWhenJamming);
	if (bAllChannelInRangeOfSmallestMax)
	{
		// Set the evaluation offset for later (case B)
		InputItem->Input->SetEvaluationOffsetInSeconds(EvaluationTime - SmallestBiggestSample.SmallestMaxInSeconds + ExtraBufferWhenJamming);

		Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Succeeded;
		return Result;
	}

	// Test to see if we can increment the buffer size (case A or C)
	if (InputItem->Input->IsDataBufferSizeControlledByInput())
	{
		// Get the average in delta time of the last 10 frames
		double AverageBetweenSample = TimedDataMonitorSubsystem::CalculateAverageInDeltaTimeBetweenSample(this, EvaluationType, AllValidChannelIdentifiers);

		const int32 TotalNumberOfFrames = (SmallestBiggestSample.BiggestMaxInSeconds - SmallestBiggestSample.SmallestMaxInSeconds - ExtraBufferWhenJamming) / AverageBetweenSample;

		const int32 CurrentDataBufferSize = InputItem->Input->GetDataBufferSize();
		InputItem->Input->SetDataBufferSize(TotalNumberOfFrames);
		const int32 UpdatedDataBufferSize = InputItem->Input->GetDataBufferSize();
		if (UpdatedDataBufferSize < TotalNumberOfFrames)
		{
			// We were not able to increase the buffer size (case C)
			Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_BufferSizeCouldNotBeResized;
		}
	}
	else
	{
		// For each channel, check if we need to increase the buffer size. If so, by how much
		for (const TimedDataMonitorSubsystem::FChannelSampleMinMax& SampleMinMax : ChannelSamplesMinMax)
		{
			if (!TimedDataMonitorSubsystem::IsInRange(EvaluationType, ChannelSamplesMinMax, SmallestBiggestSample.SmallestMaxInSeconds - ExtraBufferWhenJamming))
			{
				FTimeDataChannelItem& ChannelItem = ChannelMap[SampleMinMax.ChannelIdentifier];
				TArray<FTimedDataChannelSampleTime> AllSamplesTimes = ChannelItem.Channel->GetDataTimes();
				const double AverageBetweenSample = TimedDataMonitorSubsystem::CalculateAverageInDeltaTimeBetweenSample(EvaluationType, AllSamplesTimes);

				const int32 NumberOfNewFrameRequested = (TimedDataMonitorSubsystem::GetSeconds(EvaluationType, SampleMinMax.Min) - SmallestBiggestSample.SmallestMaxInSeconds - ExtraBufferWhenJamming) / AverageBetweenSample;

				const int32 CurrentDataBufferSize = ChannelItem.Channel->GetDataBufferSize();
				const int32 RequestedBufferSize = NumberOfNewFrameRequested + CurrentDataBufferSize;
				ChannelItem.Channel->SetDataBufferSize(RequestedBufferSize);
				const int32 UpdatedDataBufferSize = ChannelItem.Channel->GetDataBufferSize();
				if (UpdatedDataBufferSize < RequestedBufferSize)
				{
					// We were not able to increase the buffer size (case C) 
					Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Failed_BufferSizeCouldNotBeResized;
					Result.FailureChannelIdentifiers.Add(SampleMinMax.ChannelIdentifier);
				}
			}
		}
	}
	if (Result.ReturnCode == ETimedDataMonitorTimeCorrectionReturnCode::Failed_BufferSizeCouldNotBeResized)
	{
		return Result;
	}

	// We found something but the buffer size need to be increased
	InputItem->Input->SetEvaluationOffsetInSeconds(EvaluationTime - SmallestBiggestSample.SmallestMaxInSeconds + ExtraBufferWhenJamming);

	Result.ReturnCode = ETimedDataMonitorTimeCorrectionReturnCode::Retry_BufferSizeHasBeenIncreased;
	return Result;
}


void UTimedDataMonitorSubsystem::ResetAllBufferStats()
{
	BuildSourcesListIfNeeded();

	for (auto& ChannelItt : ChannelMap)
	{
		ChannelItt.Value.Channel->ResetBufferStats();
		ChannelItt.Value.ResetValue();
	}
}


bool UTimedDataMonitorSubsystem::DoesInputExist(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return InputMap.Contains(Identifier);
}


ETimedDataMonitorInputEnabled UTimedDataMonitorSubsystem::GetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		int32 bCountEnabled = 0;
		int32 bCountDisabled = 0;
		for (const FTimedDataMonitorChannelIdentifier& Channel : InputItem->ChannelIdentifiers)
		{
			if (ChannelMap[Channel].bEnabled)
			{
				++bCountEnabled;
				if (bCountDisabled > 0)
				{
					return ETimedDataMonitorInputEnabled::MultipleValues;
				}
			}
			else
			{
				++bCountDisabled;
				if (bCountEnabled > 0)
				{
					return ETimedDataMonitorInputEnabled::MultipleValues;
				}
			}
		}
		return bCountEnabled > 0 ? ETimedDataMonitorInputEnabled::Enabled : ETimedDataMonitorInputEnabled::Disabled;
	}

	return ETimedDataMonitorInputEnabled::Disabled;
}


void UTimedDataMonitorSubsystem::SetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier, bool bInEnabled)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		for (const FTimedDataMonitorChannelIdentifier& ChannelId : InputItem->ChannelIdentifiers)
		{
			ChannelMap[ChannelId].bEnabled = bInEnabled;
		}
	}
}


FText UTimedDataMonitorSubsystem::GetInputDisplayName(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetDisplayName();
	}

	return FText::GetEmpty();
}


TArray<FTimedDataMonitorChannelIdentifier> UTimedDataMonitorSubsystem::GetInputChannels(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->ChannelIdentifiers;
	}

	return TArray<FTimedDataMonitorChannelIdentifier>();
}


ETimedDataInputEvaluationType UTimedDataMonitorSubsystem::GetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetEvaluationType();
	}

	return ETimedDataInputEvaluationType::None;
}


void UTimedDataMonitorSubsystem::SetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier, ETimedDataInputEvaluationType Evaluation)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->SetEvaluationType(Evaluation);
	}
}


float UTimedDataMonitorSubsystem::GetInputEvaluationOffsetInSeconds(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return (float)SourceItem->Input->GetEvaluationOffsetInSeconds();
	}

	return 0.f;
}


void UTimedDataMonitorSubsystem::SetInputEvaluationOffsetInSeconds(const FTimedDataMonitorInputIdentifier& Identifier, float Offset)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->SetEvaluationOffsetInSeconds(Offset);
	}
}


FFrameRate UTimedDataMonitorSubsystem::GetInputFrameRate(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetFrameRate();
	}

	return ITimedDataInput::UnknownFrameRate;
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetInputOldestDataTime(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	FTimedDataChannelSampleTime ResultSampleTime(0.0, FQualifiedFrameTime());
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		bool bFirstElement = true;
		for (FTimedDataMonitorChannelIdentifier ChannelIdentifier : SourceItem->ChannelIdentifiers)
		{
			FTimedDataChannelSampleTime OldestSampleTime = ChannelMap[ChannelIdentifier].Channel->GetOldestDataTime();
			if (bFirstElement)
			{
				ResultSampleTime = OldestSampleTime;
				bFirstElement = false;
			}
			else
			{
				ResultSampleTime.PlatformSecond = FMath::Min(OldestSampleTime.PlatformSecond, ResultSampleTime.PlatformSecond);
				if (OldestSampleTime.Timecode.AsSeconds() < ResultSampleTime.Timecode.AsSeconds())
				{
					ResultSampleTime.Timecode = OldestSampleTime.Timecode;
				}
			}
		}
	}

	return ResultSampleTime;
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetInputNewestDataTime(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	FTimedDataChannelSampleTime ResultSampleTime(0.0, FQualifiedFrameTime());
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		for (FTimedDataMonitorChannelIdentifier ChannelIdentifier : SourceItem->ChannelIdentifiers)
		{
			FTimedDataChannelSampleTime NewestSampleTime = ChannelMap[ChannelIdentifier].Channel->GetNewestDataTime();
			ResultSampleTime.PlatformSecond = FMath::Max(NewestSampleTime.PlatformSecond, ResultSampleTime.PlatformSecond);
			if (NewestSampleTime.Timecode.AsSeconds() > ResultSampleTime.Timecode.AsSeconds())
			{
				ResultSampleTime.Timecode = NewestSampleTime.Timecode;
			}
		}
	}

	return ResultSampleTime;
}


bool UTimedDataMonitorSubsystem::IsDataBufferSizeControlledByInput(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->IsDataBufferSizeControlledByInput();
	}

	return false;
}


int32 UTimedDataMonitorSubsystem::GetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetDataBufferSize();
	}

	return 0;
}


void UTimedDataMonitorSubsystem::SetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier, int32 BufferSize)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		if (SourceItem->Input->IsDataBufferSizeControlledByInput())
		{
			SourceItem->Input->SetDataBufferSize(BufferSize);
		}
	}
}

ETimedDataInputState UTimedDataMonitorSubsystem::GetInputState(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	ETimedDataInputState WorstState = ETimedDataInputState::Connected;
	bool bHasAtLeastOneItem = false;
	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
		{
			const FTimeDataChannelItem& ChannelItem = ChannelMap[ChannelIdentifier];
			if (ChannelItem.bEnabled)
			{
				bHasAtLeastOneItem = true;
				ETimedDataInputState InputState = ChannelItem.Channel->GetState();
				if (InputState == ETimedDataInputState::Disconnected)
				{
					WorstState = ETimedDataInputState::Disconnected;
					break;
				}
				else if (InputState == ETimedDataInputState::Unresponsive)
				{
					WorstState = ETimedDataInputState::Unresponsive;
				}
			}
		}
	}

	return bHasAtLeastOneItem ? WorstState : ETimedDataInputState::Disconnected;
}



float UTimedDataMonitorSubsystem::GetInputEvaluationDistanceToNewestSampleMean(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();
	
	float WorstNewestMean = 0.f;
	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		WorstNewestMean = TNumericLimits<float>::Lowest();
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
		{
			WorstNewestMean = FMath::Max(ChannelMap[ChannelIdentifier].Statistics.IncrementalAverageNewestDistance, WorstNewestMean);
		}
	}

	return WorstNewestMean;
}


float UTimedDataMonitorSubsystem::GetInputEvaluationDistanceToOldestSampleMean(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	float WorstOldesttMean = 0.f;
	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		WorstOldesttMean = TNumericLimits<float>::Max();
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
		{
			WorstOldesttMean = FMath::Min(ChannelMap[ChannelIdentifier].Statistics.IncrementalAverageOldestDistance, WorstOldesttMean);
		}
	}

	return WorstOldesttMean;
}


float UTimedDataMonitorSubsystem::GetInputEvaluationDistanceToNewestSampleStandardDeviation(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	float WorstNewestSSD = 0.f;
	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
		{
			WorstNewestSSD = FMath::Max(ChannelMap[ChannelIdentifier].Statistics.DistanceToNewestSTD, WorstNewestSSD);
		}
	}

	return WorstNewestSSD;
}


float UTimedDataMonitorSubsystem::GetInputEvaluationDistanceToOldestSampleStandardDeviation(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	float WorstOldestSSD = 0.f;
	if (FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
		{
			WorstOldestSSD = FMath::Max(ChannelMap[ChannelIdentifier].Statistics.DistanceToOldestSTD, WorstOldestSSD);
		}
	}

	return WorstOldestSSD;
}


bool UTimedDataMonitorSubsystem::DoesChannelExist(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return ChannelMap.Contains(Identifier);
}


bool UTimedDataMonitorSubsystem::IsChannelEnabled(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->bEnabled;
	}

	return false;
}


void UTimedDataMonitorSubsystem::SetChannelEnabled(const FTimedDataMonitorChannelIdentifier& Identifier, bool bInEnabled)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		SourceItem->bEnabled = bInEnabled;
	}
}


FTimedDataMonitorInputIdentifier UTimedDataMonitorSubsystem::GetChannelInput(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->InputIdentifier;
	}

	return FTimedDataMonitorInputIdentifier();
}


FText UTimedDataMonitorSubsystem::GetChannelDisplayName(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetDisplayName();
	}

	return FText::GetEmpty();
}


ETimedDataInputState UTimedDataMonitorSubsystem::GetChannelState(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetState();
	}

	return ETimedDataInputState::Disconnected;
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetChannelOldestDataTime(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetOldestDataTime();
	}

	return FTimedDataChannelSampleTime();
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetChannelNewestDataTime(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetNewestDataTime();
	}

	return FTimedDataChannelSampleTime();
}


int32 UTimedDataMonitorSubsystem::GetChannelNumberOfSamples(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetNumberOfSamples();
	}

	return 0;
}


int32 UTimedDataMonitorSubsystem::GetChannelDataBufferSize(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		if (InputMap[SourceItem->InputIdentifier].Input->IsDataBufferSizeControlledByInput())
		{
			return InputMap[SourceItem->InputIdentifier].Input->GetDataBufferSize();
		}
		else
		{
			return SourceItem->Channel->GetDataBufferSize();
		}
	}

	return 0;
}


void UTimedDataMonitorSubsystem::SetChannelDataBufferSize(const FTimedDataMonitorChannelIdentifier& Identifier, int32 BufferSize)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		if (!InputMap[SourceItem->InputIdentifier].Input->IsDataBufferSizeControlledByInput())
		{
			SourceItem->Channel->SetDataBufferSize(BufferSize);
		}
	}
}


int32 UTimedDataMonitorSubsystem::GetChannelBufferUnderflowStat(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetBufferUnderflowStat();
	}

	return 0;
}

int32 UTimedDataMonitorSubsystem::GetChannelBufferOverflowStat(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetBufferOverflowStat();
	}

	return 0;
}

int32 UTimedDataMonitorSubsystem::GetChannelFrameDroppedStat(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetFrameDroppedStat();
	}

	return 0;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToNewestSampleMean(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.IncrementalAverageNewestDistance;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToOldestSampleMean(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.IncrementalAverageOldestDistance;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToNewestSampleStandardDeviation(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.DistanceToNewestSTD;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToOldestSampleStandardDeviation(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.DistanceToOldestSTD;
	}

	return 0.0f;
}

void UTimedDataMonitorSubsystem::GetChannelLastEvaluationDataStat(const FTimedDataMonitorChannelIdentifier& Identifier, FTimedDataInputEvaluationData& Result)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		SourceItem->Channel->GetLastEvaluationData(Result);
	}
}

void UTimedDataMonitorSubsystem::BuildSourcesListIfNeeded()
{
	if (bRequestSourceListRebuilt)
	{
		if (!ITimeManagementModule::IsAvailable())
		{
			InputMap.Reset();
			ChannelMap.Reset();
		}
		else
		{
			bRequestSourceListRebuilt = false;

			const TArray<ITimedDataInput*>& TimedDataInputs = ITimeManagementModule::Get().GetTimedDataInputCollection().GetInputs();

			// Regenerate the list of inputs
			{
				TArray<FTimedDataMonitorInputIdentifier> PreviousInputList;
				InputMap.GenerateKeyArray(PreviousInputList);

				for (ITimedDataInput* TimedData : TimedDataInputs)
				{
					if(TimedData == nullptr)
					{
						continue;
					}

					FTimedDataMonitorInputIdentifier FoundIdentifier;
					for (const auto& Itt : InputMap)
					{
						if (Itt.Value.Input == TimedData)
						{
							FoundIdentifier = Itt.Key;
						}
					}

					if (FoundIdentifier.IsValid())
					{
						PreviousInputList.RemoveSingleSwap(FoundIdentifier);
						InputMap[FoundIdentifier].ResetValue();
					}
					else
					{
						// if not found, add it to the list
						FTimeDataInputItem NewInput; 
						NewInput.Input = TimedData;
						InputMap.Add(FTimedDataMonitorInputIdentifier::NewIdentifier(), MoveTemp(NewInput));
					}
				}

				// Remove old inputs
				for (const FTimedDataMonitorInputIdentifier& Old : PreviousInputList)
				{
					InputMap.Remove(Old);
				}
			}

			// Regenerate the list of channels
			{
				TArray<FTimedDataMonitorChannelIdentifier> PreviousChannelList;
				ChannelMap.GenerateKeyArray(PreviousChannelList);

				for (auto& InputItt : InputMap)
				{
					TArray<FTimedDataMonitorChannelIdentifier> OldChannelIdentifiers = InputItt.Value.ChannelIdentifiers;
					TArray<ITimedDataInputChannel*> NewChannels = InputItt.Value.Input->GetChannels();
					for (ITimedDataInputChannel* Channel : NewChannels)
					{
						if (Channel == nullptr)
						{
							continue;
						}

						FTimedDataMonitorChannelIdentifier FoundIdentifier;
						for (const auto& ChannelItt : ChannelMap)
						{
							if (ChannelItt.Value.Channel == Channel)
							{
								FoundIdentifier = ChannelItt.Key;
							}
						}

						if (FoundIdentifier.IsValid())
						{
							PreviousChannelList.RemoveSingleSwap(FoundIdentifier);
							OldChannelIdentifiers.RemoveSingleSwap(FoundIdentifier);
							InputItt.Value.ChannelIdentifiers.AddUnique(FoundIdentifier);
							ChannelMap[FoundIdentifier].ResetValue();
						}
						else
						{
							FoundIdentifier = FTimedDataMonitorChannelIdentifier::NewIdentifier();

							FTimeDataChannelItem NewChannel;
							NewChannel.Channel = Channel;
							ChannelMap.Add(FoundIdentifier, MoveTemp(NewChannel));
						}

						ChannelMap[FoundIdentifier].InputIdentifier = InputItt.Key;
						InputItt.Value.ChannelIdentifiers.AddUnique(FoundIdentifier);
					}

					for (const FTimedDataMonitorChannelIdentifier& Old : OldChannelIdentifiers)
					{
						InputItt.Value.ChannelIdentifiers.RemoveSingleSwap(Old);
					}
				}

				// Remove old channels
				for (const FTimedDataMonitorChannelIdentifier& Old : PreviousChannelList)
				{
					ChannelMap.Remove(Old);
				}
			}
		}
	}
}


void UTimedDataMonitorSubsystem::OnTimedDataSourceCollectionChanged()
{
	bRequestSourceListRebuilt = true;

	bool bCallDelegate = false;
	// update map right away to not have dandling pointer
	{
		const TArray<ITimedDataInput*>& TimedDataInputs = ITimeManagementModule::Get().GetTimedDataInputCollection().GetInputs();
		TArray<FTimedDataMonitorInputIdentifier, TInlineAllocator<4>> InputToRemove;
		for (const auto& Itt : InputMap)
		{
			if (!TimedDataInputs.Contains(Itt.Value.Input))
			{
				InputToRemove.Add(Itt.Key);
			}
		}
		for (const FTimedDataMonitorInputIdentifier& Id : InputToRemove)
		{
			bCallDelegate = true;
			InputMap.Remove(Id);
		}
	}

	{
		const TArray<ITimedDataInputChannel*>& TimedDataChannels = ITimeManagementModule::Get().GetTimedDataInputCollection().GetChannels();
		TArray<FTimedDataMonitorChannelIdentifier, TInlineAllocator<4>> ChannelToRemove;
		for (const auto& Itt : ChannelMap)
		{
			if (!TimedDataChannels.Contains(Itt.Value.Channel))
			{
				ChannelToRemove.Add(Itt.Key);
			}
		}
		for (const FTimedDataMonitorChannelIdentifier& Id : ChannelToRemove)
		{
			bCallDelegate = true;
			ChannelMap.Remove(Id);
		}
	}

	OnIdentifierListChanged_Delegate.Broadcast();
	OnIdentifierListChanged_Dynamic.Broadcast();
}


void UTimedDataMonitorSubsystem::Tick(float DeltaTime)
{
	const bool bUpdateStats = CVarEnableTimedDataMonitorSubsystemStats.GetValueOnGameThread();
	if (bUpdateStats)
	{
		UpdateEvaluationStatistics();
	}
}


TStatId UTimedDataMonitorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTimedDataMonitorSubsystem, STATGROUP_Tickables);
}


void UTimedDataMonitorSubsystem::UpdateEvaluationStatistics()
{
	BuildSourcesListIfNeeded();

	for (TPair<FTimedDataMonitorChannelIdentifier, FTimeDataChannelItem>& Item : ChannelMap)
	{
		if (Item.Value.bEnabled)
		{
			FTimedDataInputEvaluationData Data;
			GetChannelLastEvaluationDataStat(Item.Key, Data);

			Item.Value.Statistics.Update(Data.DistanceToOldestSampleSeconds, Data.DistanceToNewestSampleSeconds);
		}
	}
}

void FTimedDataChannelEvaluationStatistics::Update(float DistanceToOldest, float DistanceToNewest)
{
	//Compute running average and variance based on Welford's algorithm 
	//https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm

	//Update sample count to include this new one
	++SampleCount;

	//For running variance, keep previous mean distance
	const float NewDistanceToNewestToMean1 = DistanceToNewest - IncrementalAverageNewestDistance;
	const float NewDistanceToOldestToMean1 = DistanceToOldest - IncrementalAverageOldestDistance;

	//Update incremental average of distances to both ends of this input buffer
	IncrementalAverageNewestDistance = IncrementalAverageNewestDistance + (DistanceToNewest - IncrementalAverageNewestDistance) / SampleCount;
	IncrementalAverageOldestDistance = IncrementalAverageOldestDistance + (DistanceToOldest - IncrementalAverageOldestDistance) / SampleCount;

	//Compute sum of squares for running variance
	const float NewDistanceToNewestToMean2 = DistanceToNewest - IncrementalAverageNewestDistance;
	SumSquaredDistanceNewest = SumSquaredDistanceNewest + NewDistanceToNewestToMean2 * NewDistanceToNewestToMean1;
	const float NewDistanceToOldestToMean2 = DistanceToOldest - IncrementalAverageOldestDistance;
	SumSquaredDistanceOldest = SumSquaredDistanceOldest + NewDistanceToOldestToMean2 * NewDistanceToOldestToMean1;

	//Finally compute the variance
	IncrementalVarianceDistanceNewest = SumSquaredDistanceNewest / SampleCount;
	IncrementalVarianceDistanceOldest = SumSquaredDistanceOldest / SampleCount;

	//Square root of that average gives us sigma (standard deviation)
	DistanceToNewestSTD = FMath::Sqrt(IncrementalVarianceDistanceNewest);
	DistanceToOldestSTD = FMath::Sqrt(IncrementalVarianceDistanceOldest);

	LastDistanceToOldest = DistanceToOldest;
	LastDistanceToNewest = DistanceToNewest;
}

void FTimedDataChannelEvaluationStatistics::Reset()
{
	SampleCount = 0;
	IncrementalAverageOldestDistance = 0.0f;
	IncrementalAverageNewestDistance = 0.0f;
	IncrementalVarianceDistanceNewest = 0.0f;
	IncrementalVarianceDistanceOldest = 0.0f;
	SumSquaredDistanceNewest = 0.0f;
	SumSquaredDistanceOldest = 0.0f;
}

#undef LOCTEXT_NAMESPACE
