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


FTimedDataMonitorCalibrationResult UTimedDataMonitorSubsystem::CalibrateWithTimecodeProvider()
{
	BuildSourcesListIfNeeded();

	FTimedDataMonitorCalibrationResult Result;
	Result.ReturnCode = ETimedDataMonitorCalibrationReturnCode::Failed_CanNotCallibrateWithoutJam;
//	return Result;
//
//
//	UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
//	if (CurrentTimecodeProvider == nullptr
//		|| CurrentTimecodeProvider->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized
//		|| !FApp::GetCurrentFrameTime().IsSet())
//	{
//		Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_NoTimecode;
//		return Result;
//	}
//	FQualifiedFrameTime CurrentFrameTime = CurrentTimecodeProvider->GetQualifiedFrameTime();
//
//	struct FCollectedDataTimes
//	{
//		FTimedDataMonitorInputIdentifier InputId;
//		TArray<FTimedDataInputSampleTime> DataTimes;
//	};
//
//	// Collect all DataTimes
//	TArray<FCollectedDataTimes> AllCollectedDataTimes;
//	AllCollectedDataTimes.Reset(InputMap.Num());
//	for (const auto& InputItt : InputMap)
//	{
//		if (InputItt.Value.bEnabled)
//		{
//			if (InputItt.Value.Input->GetState() != ETimedDataInputState::Connected)
//			{
//				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_UnresponsiveInput;
//				Result.FailureInputIdentifiers.Add(InputItt.Key);
//				return Result;
//			}
//
//			if (InputItt.Value.Input->GetEvaluationType() != ETimedDataInputEvaluationType::Timecode)
//			{
//				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_NoTimecode;
//				Result.FailureInputIdentifiers.Add(InputItt.Key);
//				return Result;
//			}
//
//			FFrameRate FrameRate = InputItt.Value.Input->GetFrameRate();
//			if (FrameRate == ITimedDataInput::UnknowedFrameRate)
//			{
//				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_InvalidFrameRate;
//				Result.FailureInputIdentifiers.Add(InputItt.Key);
//				return Result;
//			}
//
//			TArray<FTimedDataInputSampleTime> DataTimes = InputItt.Value.Input->GetDataTimes();
//			if (DataTimes.Num() == 0)
//			{
//				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_NoDataBuffered;
//				Result.FailureInputIdentifiers.Add(InputItt.Key);
//				return Result;
//			}
//
//			FCollectedDataTimes Element;
//			Element.InputId = InputItt.Key;
//			Element.DataTimes = MoveTemp(DataTimes);
//			AllCollectedDataTimes.Emplace(MoveTemp(Element));
//		}
//	}
//
//	if (AllCollectedDataTimes.Num() == 0)
//	{
//		Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Succeeded;
//		return Result;
//	}
//
//	// Is there a range of data that everyone is happy with
//	{
//		// With [A-C] it will return [10-11], with [A-D] it should not be able to find anything
//		//TC == 12
//		//A    10  11  12  13
//		//B 9  10  11
//		//C    10  11  12
//		//D            12  13
//
//		FFrameTime RangeMin;
//		FFrameTime RangeMax;
//		bool bFirstItem = true;
//		bool bFoundRange = true;
//
//		for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
//		{
//			check(DataTimesItt.DataTimes.Num() > 0);
//
//			// On purpose do not use the evaluation offset
//			//const FFrameTime FirstFrameTime = DataTimesItt.DataTimes[0].Timecode.ConvertTo(CurrentFrameTime.Rate) - DataTimesItt.EvaluationOffset.ConvertTo(CurrentFrameTime.Rate);
//
//			const FFrameTime FirstFrameTime = DataTimesItt.DataTimes[0].Timecode.ConvertTo(CurrentFrameTime.Rate);
//			const FFrameTime LastFrameTime = DataTimesItt.DataTimes.Last().Timecode.ConvertTo(CurrentFrameTime.Rate);
//
//			if (!bFirstItem)
//			{
//				if (FirstFrameTime <= RangeMax && LastFrameTime >= RangeMin)
//				{
//
//					RangeMin = FMath::Max(RangeMin, FirstFrameTime);
//					RangeMax = FMath::Min(RangeMax, LastFrameTime);
//				}
//				else
//				{
//					// Return an unset value
//					bFoundRange = false;
//					break;
//				}
//			}
//			else
//			{
//				bFirstItem = false;
//				RangeMin = FirstFrameTime;
//				RangeMax = LastFrameTime;
//			}
//		}
//
//		if (bFoundRange)
//		{
//			FFrameTime TimecodeProviderOffset = RangeMax - CurrentFrameTime.Time;
//			if (RangeMin <= CurrentFrameTime.Time && CurrentFrameTime.Time <= RangeMax)
//			{
//				// TC in the range, if so use the TC provider value
//
//				// Reset the FrameDelay
//				check(CurrentTimecodeProvider);
//				CurrentTimecodeProvider->FrameDelay = 0.f;
//
//				// Reset previous evaluation offset
//				for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
//				{
//					InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
//				}
//				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Succeeded;
//				return Result;
//			}
//			else if (CurrentFrameTime.Time < RangeMin)
//			{
//				// We need to increase buffer size of all inputs if possible
////@TODO
//				//Is it possible
//				//else set TimecodeProviderOffset to offset the TC provider
//
//				//if (UTimecodeProvider* TimecodeProvider = GEngine->GetTimecodeProvider())
//				//{
//				//	TimecodeProvider->FrameDelay = 0.f;
//				//}
//				//Result.FailureInputIdentifiers.Reset(AllCollectedDataTimes.Num());
//				//for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
//				//{
//				//	InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
//				//	InputMap[DataTimesItt.InputId].Input->SetDataBufferSize(0);
//				//}
//				//Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Retry_BufferSizeHasBeenIncreased;
//				//return Result;
//			}
//
//
//			// When we can't resize the buffer (because they would be too big, then offset the TC provider.
//			//Or when the TC Provider is over all the input, then offset the TC provider
//			check(CurrentTimecodeProvider);
//			CurrentTimecodeProvider->FrameDelay = TimecodeProviderOffset.AsDecimal();
//	
//			// Reset previous evaluation offset
//			for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
//			{
//				InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
//			}
//			Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Succeeded;
//			return Result;
//		}
//	}
//
//	// No range was found. What is the smallest acceptable LastFrameTime. Can we increase a buffer?
//	{
//		// It will return 11, and we will try to increase the buffer size of E by 1 and jam (A, F, G). We want to be closer to TC as possible.
//		// TC == 50.
//		//A 1  2
//		//B                  48  49  50  51
//		//C              47  48  49
//		//D                  48  49  50
//		//E                          50  51
//		//F 1
//		//G                                                     100
//
//
//		//@todo. complete the algo
//	}
//
//	Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_CanNotCallibrateWithoutJam;
	return Result;
}


FTimedDataMonitorJamResult UTimedDataMonitorSubsystem::JamInputs(ETimedDataInputEvaluationType InEvaluationType)
{
	BuildSourcesListIfNeeded();

	FTimedDataMonitorJamResult Result;
	Result.ReturnCode = ETimedDataMonitorJamReturnCode::Succeeded;


	const double CurrentPlatformTime = FApp::GetCurrentTime();
	UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
	FQualifiedFrameTime CurrentFrameTime;
	if (InEvaluationType == ETimedDataInputEvaluationType::Timecode)
	{
		if (CurrentTimecodeProvider == nullptr || CurrentTimecodeProvider->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized || !FApp::GetCurrentFrameTime().IsSet())
		{
			Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_NoTimecode;
			return Result;
		}

		CurrentFrameTime = CurrentTimecodeProvider->GetQualifiedFrameTime();
	}


	// Test all Channels for Failed_UnresponsiveInput
	TArray<FTimedDataMonitorInputIdentifier> AllValidInputIndentifiers;
	for (const auto& ChannelItt : ChannelMap)
	{
		if (ChannelItt.Value.bEnabled)
		{	
			if (ChannelItt.Value.Channel->GetState() != ETimedDataInputState::Connected)
			{
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_UnresponsiveInput;
				Result.FailureChannelIdentifiers.Add(ChannelItt.Key);
			}

			AllValidInputIndentifiers.AddUnique(ChannelItt.Value.InputIdentifier);
		}
	}

	if (Result.ReturnCode == ETimedDataMonitorJamReturnCode::Failed_UnresponsiveInput)
	{
		return Result;
	}

	// Test all inputs for Failed_EvaluationTypeDoNotMatch
	for (const FTimedDataMonitorInputIdentifier& InputIndentifier : AllValidInputIndentifiers)
	{
		// The UI show have make sure that all evaluation type are correct
		if (InputMap[InputIndentifier].Input->GetEvaluationType() != InEvaluationType)
		{
			Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_EvaluationTypeDoNotMatch;
			Result.FailureInputIdentifiers.Add(InputIndentifier);
		}
	}

	if (Result.ReturnCode == ETimedDataMonitorJamReturnCode::Failed_EvaluationTypeDoNotMatch)
	{
		return Result;
	}

	if (InEvaluationType == ETimedDataInputEvaluationType::None)
	{
		// Set the evaluation offset of everyone to 0
		for (const FTimedDataMonitorInputIdentifier& InputId : AllValidInputIndentifiers)
		{
			InputMap[InputId].Input->SetEvaluationOffsetInSeconds(0.0);
		}
		Result.ReturnCode = ETimedDataMonitorJamReturnCode::Succeeded;
	}
	else
	{
		// Collect all DataTimes

		struct FChannelMinMaxSampleTime
		{
			FChannelMinMaxSampleTime(FTimedDataChannelSampleTime InMin, FTimedDataChannelSampleTime InMax)
				: Min(InMin), Max(InMax) {}
			FTimedDataChannelSampleTime Min;
			FTimedDataChannelSampleTime Max;
		};

		TMap<FTimedDataMonitorChannelIdentifier, FChannelMinMaxSampleTime> AllCollectedDataTimes;
		for (const auto& ChannelItt : ChannelMap)
		{
			if (ChannelItt.Value.bEnabled)
			{
				if (ChannelItt.Value.Channel->GetNumberOfSamples() <= 0)
				{
					Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_NoDataBuffered;
					Result.FailureChannelIdentifiers.Add(ChannelItt.Key);
				}

				FChannelMinMaxSampleTime DataTimes = FChannelMinMaxSampleTime(ChannelItt.Value.Channel->GetOldestDataTime() , ChannelItt.Value.Channel->GetNewestDataTime());
				AllCollectedDataTimes.Add(ChannelItt.Key, MoveTemp(DataTimes));
			}
		}

		if (Result.ReturnCode == ETimedDataMonitorJamReturnCode::Failed_NoDataBuffered)
		{
			return Result;
		}

		// Grouped by input, find what could we do to match data.
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
		// For InputA, we should increase the buffer size of A2 and set an offset so that 11 == 50
		// For InputB, we set an offset so that 100 == 50
		// For InputC, we cannot find anything since the difference is too big. Failed.
		// For InputD, we set an offset so that 50 == 50

		// For each input, go though all their channel and find what section that matches
		bool bAllInputInRange = true;
		const double EvaluationTime = InEvaluationType == ETimedDataInputEvaluationType::Timecode ? CurrentFrameTime.AsSeconds() : CurrentPlatformTime;
		for (const FTimedDataMonitorInputIdentifier& InputId : AllValidInputIndentifiers)
		{
			// Find newest PlatformTime
			double SmallestMinInSeconds = TNumericLimits<double>::Max();
			double BiggestMaxInSeconds = TNumericLimits<double>::Min();
			double BiggerMinInSeconds = TNumericLimits<double>::Min();
			double SmallestMaxInSeconds = TNumericLimits<double>::Max();
			for (const FTimedDataMonitorChannelIdentifier& ChannelId : InputMap[InputId].ChannelIdentifiers)
			{
				// test if the sample was collect (enabled)
				if (FChannelMinMaxSampleTime* SamplesTimes = AllCollectedDataTimes.Find(ChannelId))
				{
					SmallestMinInSeconds = FMath::Min(GetSeconds(InEvaluationType, SamplesTimes->Min), SmallestMinInSeconds);	//A == 10, B == 99, C == 10, D == 48
					BiggestMaxInSeconds = FMath::Max(GetSeconds(InEvaluationType, SamplesTimes->Max), BiggestMaxInSeconds);		//A == 51, B == 101, C == 100, D == 51

					BiggerMinInSeconds = FMath::Max(GetSeconds(InEvaluationType, SamplesTimes->Min), BiggerMinInSeconds);		//A == 48, B == 100, C == 10, D == 49
					SmallestMaxInSeconds = FMath::Min(GetSeconds(InEvaluationType, SamplesTimes->Max), SmallestMaxInSeconds);	//A == 11, B == 100, C == 11, D == 51
				}
			}

		//@todo use the stat when we are confident that they works properly
			//const double DistanceToNewestSTD = GetInputEvaluationDistanceToNewestSampleStandardDeviation(InputId) * NumberOfSigmaOfSignification;
			const double ExtraBufferWhenJamming = 0.5;

			// Test if all the samples are in the range of the EvaluationTime 
			bool bAllChannelInRangeOfEvaluationTime = true;
			bool bAllChannelInRangeOfSmallestMax = true;
			for (const FTimedDataMonitorChannelIdentifier& ChannelId : InputMap[InputId].ChannelIdentifiers)
			{
				if (FChannelMinMaxSampleTime* SamplesTimes = AllCollectedDataTimes.Find(ChannelId))
				{
					if (GetSeconds(InEvaluationType, SamplesTimes->Min) > EvaluationTime - ExtraBufferWhenJamming || GetSeconds(InEvaluationType, SamplesTimes->Max) < EvaluationTime - ExtraBufferWhenJamming)
					{
						bAllChannelInRangeOfEvaluationTime = false;
					}

					if (GetSeconds(InEvaluationType, SamplesTimes->Min) > SmallestMaxInSeconds - ExtraBufferWhenJamming || GetSeconds(InEvaluationType, SamplesTimes->Max) < SmallestMaxInSeconds - ExtraBufferWhenJamming)
					{
						bAllChannelInRangeOfSmallestMax = false;
					}
				}
			}

			if (bAllChannelInRangeOfEvaluationTime)
			{
				// Set the evaluation offset for later (case D)
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Succeeded;

				InputMap[InputId].Input->SetEvaluationOffsetInSeconds(ExtraBufferWhenJamming);
	//@todo reset the buffer size base on the stat
			}
			else if (bAllChannelInRangeOfSmallestMax)
			{
				// Set the evaluation offset for later (case B)
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Succeeded;

				InputMap[InputId].Input->SetEvaluationOffsetInSeconds(EvaluationTime - SmallestMaxInSeconds + ExtraBufferWhenJamming);
	//@todo reset the buffer size base on the stat
			}
			else
			{
				// Test to see if we can increment the buffer size (case A or C)
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Retry_BufferSizeHasBeenIncreased;

				if (InputMap[InputId].Input->IsDataBufferSizeControlledByInput())
				{
					// Get the average in delta time of the last 10 frames
					double AverageBetweenSample = 0.0;
					int32 AverageCounter = 0;
					for (const FTimedDataMonitorChannelIdentifier& ChannelId : InputMap[InputId].ChannelIdentifiers)
					{
						if (ChannelMap[ChannelId].bEnabled)
						{
							TArray<FTimedDataChannelSampleTime> AllSamplesTimes = ChannelMap[ChannelId].Channel->GetDataTimes();
							if (AllSamplesTimes.Num() > 1)
							{
								const double CurrentAverageBetweenSample = CalculateAverageInDeltaTimeBetweenSample(InEvaluationType, AllSamplesTimes);

								++AverageCounter;
								AverageBetweenSample += (CurrentAverageBetweenSample - AverageBetweenSample) / (double)AverageCounter;
							}
						}
					}
					if (FMath::IsNearlyZero(AverageBetweenSample))
					{
						AverageBetweenSample = FApp::GetDeltaTime();
					}

					const int32 TotalNumberOfFrames = (BiggestMaxInSeconds - SmallestMaxInSeconds - ExtraBufferWhenJamming) / AverageBetweenSample;
					const int32 CurrentDataBufferSize = InputMap[InputId].Input->GetDataBufferSize();
					InputMap[InputId].Input->SetDataBufferSize(TotalNumberOfFrames);
					const int32 UpdatedDataBufferSize = InputMap[InputId].Input->GetDataBufferSize();
					if (UpdatedDataBufferSize < TotalNumberOfFrames)
					{
						// We were not able to increase the buffer size (case C) 
						Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_BufferSizeHaveBeenMaxed;
						Result.FailureInputIdentifiers.Add(InputId);
					}
				}
				else
				{
					// For each channel, check if we need to increase the buffer size. If so, by how much
					for (const FTimedDataMonitorChannelIdentifier& ChannelId : InputMap[InputId].ChannelIdentifiers)
					{
						if (FChannelMinMaxSampleTime* SamplesTimes = AllCollectedDataTimes.Find(ChannelId))
						{
							if (GetSeconds(InEvaluationType, SamplesTimes->Min) > SmallestMaxInSeconds - ExtraBufferWhenJamming || GetSeconds(InEvaluationType, SamplesTimes->Max) < SmallestMaxInSeconds - ExtraBufferWhenJamming)
							{
								TArray<FTimedDataChannelSampleTime> AllSamplesTimes = ChannelMap[ChannelId].Channel->GetDataTimes();
								const double AverageBetweenSample = CalculateAverageInDeltaTimeBetweenSample(InEvaluationType, AllSamplesTimes);
								const int32 NumberOfNewFrameRequested = (GetSeconds(InEvaluationType, SamplesTimes->Min) - SmallestMaxInSeconds - ExtraBufferWhenJamming) / AverageBetweenSample;

								const int32 CurrentDataBufferSize = ChannelMap[ChannelId].Channel->GetDataBufferSize();
								const int32 RequestedBufferSize = NumberOfNewFrameRequested + CurrentDataBufferSize;
								ChannelMap[ChannelId].Channel->SetDataBufferSize(RequestedBufferSize);
								const int32 UpdatedDataBufferSize = ChannelMap[ChannelId].Channel->GetDataBufferSize();
								if (UpdatedDataBufferSize < RequestedBufferSize)
								{
									// We were not able to increase the buffer size (case C) 
									Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_BufferSizeHaveBeenMaxed;
									Result.FailureChannelIdentifiers.Add(ChannelId);
								}
							}
						}
					}
				}

				InputMap[InputId].Input->SetEvaluationOffsetInSeconds(EvaluationTime - SmallestMaxInSeconds + ExtraBufferWhenJamming);

				if (Result.ReturnCode == ETimedDataMonitorJamReturnCode::Retry_BufferSizeHasBeenIncreased)
				{
					return Result;
				}
			}

			if (InEvaluationType == ETimedDataInputEvaluationType::Timecode && CurrentTimecodeProvider)
			{
				CurrentTimecodeProvider->FrameDelay = 0.f;
			}
		}
	}

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

	FTimedDataChannelSampleTime ResultSampleTime(TNumericLimits<double>::Max(), FQualifiedFrameTime());
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		for (FTimedDataMonitorChannelIdentifier ChannelIdentifier : SourceItem->ChannelIdentifiers)
		{
			FTimedDataChannelSampleTime OldestSampleTime = ChannelMap[ChannelIdentifier].Channel->GetOldestDataTime();
			ResultSampleTime.PlatformSecond = FMath::Min(OldestSampleTime.PlatformSecond, ResultSampleTime.PlatformSecond);
			if (OldestSampleTime.Timecode.AsSeconds() < ResultSampleTime.Timecode.AsSeconds())
			{
				ResultSampleTime.Timecode = OldestSampleTime.Timecode;
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
		return SourceItem->Input->SetDataBufferSize(BufferSize);
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
		if (InputMap[SourceItem->InputIdentifier].Input->IsDataBufferSizeControlledByInput())
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


double UTimedDataMonitorSubsystem::GetSeconds(ETimedDataInputEvaluationType Evaluation, const FTimedDataChannelSampleTime& SampleTime) const
{
	return Evaluation == ETimedDataInputEvaluationType::Timecode ? SampleTime.Timecode.AsSeconds() : SampleTime.PlatformSecond;
}


double UTimedDataMonitorSubsystem::CalculateAverageInDeltaTimeBetweenSample(ETimedDataInputEvaluationType Evaluation, const TArray<FTimedDataChannelSampleTime>& SampleTimes) const
{
	double Average = 0.0;
	if (SampleTimes.Num() >= 2)
	{
		// Get the average of the last 10 samples in seconds
		const int32 AvgCounter = FMath::Min(SampleTimes.Num()-1, 10-1);
		
		const int32 SampleTimeNum = SampleTimes.Num();

		for (int32 Index = 1; Index <= AvgCounter; ++Index)
		{
			double Delta = GetSeconds(Evaluation, SampleTimes[SampleTimeNum - Index]) - GetSeconds(Evaluation, SampleTimes[SampleTimeNum - Index - 1]);
			Average += (Delta - Average) / (double)Index;
		}
	}
	else
	{
		Average = FApp::GetDeltaTime(); // was not able to find a correct delta time. guess one.
	}
	return Average;
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
