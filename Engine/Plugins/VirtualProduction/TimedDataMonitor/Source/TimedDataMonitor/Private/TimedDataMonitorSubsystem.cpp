// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimedDataMonitorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/TimecodeProvider.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ITimeManagementModule.h"
#include "Misc/App.h"
#include "Misc/QualifiedFrameTime.h"
#include "TimedDataInputCollection.h"


static TAutoConsoleVariable<bool> CVarEnableTimedDataMonitorSubsystemStats(TEXT("TimedDataMonitor.EnableStatUpdate"), 1, TEXT("Enable calculating evaluation statistics of all registered channels."));


#define LOCTEXT_NAMESPACE "TimedDataMonitorSubsystem"

/**
 *
 */
FTimedDataMonitorGroupIdentifier FTimedDataMonitorGroupIdentifier::NewIdentifier()
{
	FTimedDataMonitorGroupIdentifier Item;
	Item.Group = FGuid::NewGuid();
	return Item;
}


/**
 *
 */
FTimedDataMonitorInputIdentifier FTimedDataMonitorInputIdentifier::NewIdentifier()
{
	FTimedDataMonitorInputIdentifier Item;
	Item.Input = FGuid::NewGuid();
	return Item;
}


/**
 * 
 */
bool UTimedDataMonitorSubsystem::FTimeDataInputItem::HasGroup() const
{
	return GroupIdentifier.IsValidGroup();
}


void UTimedDataMonitorSubsystem::FTimeDataInputItem::ResetValue()
{
	Statistics.Reset();
}


void UTimedDataMonitorSubsystem::FTimeDataInputItemGroup::ResetValue()
{
	InputIdentifiers.Reset();
}

/**
 * 
 */
void UTimedDataMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	OtherGroupIdentifier = FTimedDataMonitorGroupIdentifier::NewIdentifier();
	bRequestSourceListRebuilt = true;
	FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UTimedDataMonitorSubsystem::OnWorldPostTick);
	ITimeManagementModule::Get().GetTimedDataInputCollection().OnCollectionChanged().AddUObject(this, &UTimedDataMonitorSubsystem::OnTimedDataSourceCollectionChanged);
	Super::Initialize(Collection);
}


void UTimedDataMonitorSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);

	if (ITimeManagementModule::IsAvailable())
	{
		ITimeManagementModule::Get().GetTimedDataInputCollection().OnCollectionChanged().RemoveAll(this);
	}

	bRequestSourceListRebuilt = true;
	InputMap.Reset();
	GroupMap.Reset();
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


ITimedDataInputGroup* UTimedDataMonitorSubsystem::GetTimedDataInputGroup(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		return GroupItem->Group;
	}

	return nullptr;
}


TArray<FTimedDataMonitorGroupIdentifier> UTimedDataMonitorSubsystem::GetAllGroups()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorGroupIdentifier> Result;
	GroupMap.GenerateKeyArray(Result);
	return Result;
}


TArray<FTimedDataMonitorInputIdentifier> UTimedDataMonitorSubsystem::GetAllInputs()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorInputIdentifier> Result;
	InputMap.GenerateKeyArray(Result);
	return Result;
}


FTimedDataMonitorCallibrationResult UTimedDataMonitorSubsystem::CalibrateWithTimecodeProvider()
{
	BuildSourcesListIfNeeded();

	FTimedDataMonitorCallibrationResult Result;
	Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_CanNotCallibrateWithoutJam;


	UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
	if (CurrentTimecodeProvider == nullptr
		|| CurrentTimecodeProvider->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized
		|| !FApp::GetCurrentFrameTime().IsSet())
	{
		Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_NoTimecode;
		return Result;
	}
	FQualifiedFrameTime CurrentFrameTime = CurrentTimecodeProvider->GetQualifiedFrameTime();

	struct FCollectedDataTimes
	{
		FTimedDataMonitorInputIdentifier InputId;
		TArray<FTimedDataInputSampleTime> DataTimes;
	};

	// Collect all DataTimes
	TArray<FCollectedDataTimes> AllCollectedDataTimes;
	AllCollectedDataTimes.Reset(InputMap.Num());
	for (const auto& InputItt : InputMap)
	{
		if (InputItt.Value.bEnabled)
		{
			if (InputItt.Value.Input->GetState() != ETimedDataInputState::Connected)
			{
				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_UnresponsiveInput;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			if (InputItt.Value.Input->GetEvaluationType() != ETimedDataInputEvaluationType::Timecode)
			{
				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_NoTimecode;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			FFrameRate FrameRate = InputItt.Value.Input->GetFrameRate();
			if (FrameRate == ITimedDataInput::UnknowedFrameRate)
			{
				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_InvalidFrameRate;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			TArray<FTimedDataInputSampleTime> DataTimes = InputItt.Value.Input->GetDataTimes();
			if (DataTimes.Num() == 0)
			{
				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_NoDataBuffered;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			FCollectedDataTimes Element;
			Element.InputId = InputItt.Key;
			Element.DataTimes = MoveTemp(DataTimes);
			AllCollectedDataTimes.Emplace(MoveTemp(Element));
		}
	}

	if (AllCollectedDataTimes.Num() == 0)
	{
		Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Succeeded;
		return Result;
	}

	// Is there a range of data that everyone is happy with
	{
		// With [A-C] it will return [10-11], with [A-D] it should not be able to find anything
		//TC == 12
		//A    10  11  12  13
		//B 9  10  11
		//C    10  11  12
		//D            12  13

		FFrameTime RangeMin;
		FFrameTime RangeMax;
		bool bFirstItem = true;
		bool bFoundRange = true;

		for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
		{
			check(DataTimesItt.DataTimes.Num() > 0);

			// On purpose do not use the evaluation offset
			//const FFrameTime FirstFrameTime = DataTimesItt.DataTimes[0].Timecode.ConvertTo(CurrentFrameTime.Rate) - DataTimesItt.EvaluationOffset.ConvertTo(CurrentFrameTime.Rate);

			const FFrameTime FirstFrameTime = DataTimesItt.DataTimes[0].Timecode.ConvertTo(CurrentFrameTime.Rate);
			const FFrameTime LastFrameTime = DataTimesItt.DataTimes.Last().Timecode.ConvertTo(CurrentFrameTime.Rate);

			if (!bFirstItem)
			{
				if (FirstFrameTime <= RangeMax && LastFrameTime >= RangeMin)
				{

					RangeMin = FMath::Max(RangeMin, FirstFrameTime);
					RangeMax = FMath::Min(RangeMax, LastFrameTime);
				}
				else
				{
					// Return an unset value
					bFoundRange = false;
					break;
				}
			}
			else
			{
				bFirstItem = false;
				RangeMin = FirstFrameTime;
				RangeMax = LastFrameTime;
			}
		}

		if (bFoundRange)
		{
			FFrameTime TimecodeProviderOffset = RangeMax - CurrentFrameTime.Time;
			if (RangeMin <= CurrentFrameTime.Time && CurrentFrameTime.Time <= RangeMax)
			{
				// TC in the range, if so use the TC provider value

				// Reset the FrameDelay
				check(CurrentTimecodeProvider);
				CurrentTimecodeProvider->FrameDelay = 0.f;

				// Reset previous evaluation offset
				for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
				{
					InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
				}
				Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Succeeded;
				return Result;
			}
			else if (CurrentFrameTime.Time < RangeMin)
			{
				// We need to increase buffer size of all inputs if possible
//@TODO
				//Is it possible
				//else set TimecodeProviderOffset to offset the TC provider

				//if (UTimecodeProvider* TimecodeProvider = GEngine->GetTimecodeProvider())
				//{
				//	TimecodeProvider->FrameDelay = 0.f;
				//}
				//Result.FailureInputIdentifiers.Reset(AllCollectedDataTimes.Num());
				//for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
				//{
				//	InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
				//	InputMap[DataTimesItt.InputId].Input->SetDataBufferSize(0);
				//}
				//Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Retry_BufferSizeHasBeenIncreased;
				//return Result;
			}


			// When we can't resize the buffer (because they would be too big, then offset the TC provider.
			//Or when the TC Provider is over all the input, then offset the TC provider
			check(CurrentTimecodeProvider);
			CurrentTimecodeProvider->FrameDelay = TimecodeProviderOffset.AsDecimal();
	
			// Reset previous evaluation offset
			for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
			{
				InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
			}
			Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Succeeded;
			return Result;
		}
	}

	// No range was found. What is the smallest acceptable LastFrameTime. Can we increase a buffer?
	{
		// It will return 11, and we will try to increase the buffer size of E by 1 and jam (A, F, G). We want to be closer to TC as possible.
		// TC == 50.
		//A 1  2
		//B                  48  49  50  51
		//C              47  48  49
		//D                  48  49  50
		//E                          50  51
		//F 1
		//G                                                     100


		//@todo. complete the algo
	}

	Result.ReturnCode = ETimedDataMonitorCallibrationReturnCode::Failed_CanNotCallibrateWithoutJam;
	return Result;
}


FTimedDataMonitorJamResult UTimedDataMonitorSubsystem::JamInputs(ETimedDataInputEvaluationType InEvaluationType)
{
	BuildSourcesListIfNeeded();

	FTimedDataMonitorJamResult Result;
	Result.ReturnCode = ETimedDataMonitorJamReturnCode::Succeeded;

	UTimecodeProvider* CurrentTimecodeProvider = GEngine->GetTimecodeProvider();
	TOptional<FQualifiedFrameTime> CurrentFrameTime = FApp::GetCurrentFrameTime();
	if (InEvaluationType == ETimedDataInputEvaluationType::Timecode && (CurrentTimecodeProvider == nullptr || CurrentTimecodeProvider->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized))
	{
		Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_NoTimecode;
		return Result;
	}

	struct FCollectedDataTimes
	{
		FTimedDataMonitorInputIdentifier InputId;
		TArray<FTimedDataInputSampleTime> DataTimes;
	};

	// Collect all DataTimes
	TArray<FCollectedDataTimes> AllCollectedDataTimes;
	AllCollectedDataTimes.Reset(InputMap.Num());
	for (const auto& InputItt : InputMap)
	{
		if (InputItt.Value.bEnabled)
		{
			if (InputItt.Value.Input->GetState() != ETimedDataInputState::Connected)
			{
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_UnresponsiveInput;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			// The UI show have make sure that all evaluation type are correct
			if (InputItt.Value.Input->GetEvaluationType() != InEvaluationType)
			{
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_EvaluationTypeDoNotMatch;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			TArray<FTimedDataInputSampleTime> DataTimes = InputItt.Value.Input->GetDataTimes();
			if (DataTimes.Num() == 0)
			{
				Result.ReturnCode = ETimedDataMonitorJamReturnCode::Failed_NoDataBuffered;
				Result.FailureInputIdentifiers.Add(InputItt.Key);
				return Result;
			}

			FCollectedDataTimes Element;
			Element.InputId = InputItt.Key;
			Element.DataTimes = MoveTemp(DataTimes);
			AllCollectedDataTimes.Emplace(MoveTemp(Element));
		}
	}

	if (InEvaluationType == ETimedDataInputEvaluationType::None)
	{
		// Set the evaluation offset of everyone to 0(InEvaluationType == ETimedDataInputEvaluationType::None)
		for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
		{
			InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(0.0);
		}
	}
	else if (InEvaluationType == ETimedDataInputEvaluationType::PlatformTime)
	{
		// set the evaluation offset of everyone to FApp::GetSeconds() - LastFrame
		const double NowSeconds = FPlatformTime::Seconds();
		for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
		{
			const double LastFrameTime = DataTimesItt.DataTimes.Last().PlatformSecond;
			const double EvaluationOffset = NowSeconds - LastFrameTime;
			InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(EvaluationOffset);
		}
	}
	else if (InEvaluationType == ETimedDataInputEvaluationType::Timecode)
	{
		// set offset of TC provider to 0
		check(CurrentTimecodeProvider);
		CurrentTimecodeProvider->FrameDelay = 0.f;
		FQualifiedFrameTime NowFrameTime = CurrentTimecodeProvider->GetQualifiedFrameTime();

		// set evaluation offset of everyone to TCProvider->GetFrameTime() - LastFrame (with Rate in mind)
		for (const FCollectedDataTimes& DataTimesItt : AllCollectedDataTimes)
		{
			const FQualifiedFrameTime LastFrameTime = FQualifiedFrameTime(DataTimesItt.DataTimes.Last().Timecode.ConvertTo(NowFrameTime.Rate), NowFrameTime.Rate);
			const double EvaluationOffset = NowFrameTime.AsSeconds() - LastFrameTime.AsSeconds();
			InputMap[DataTimesItt.InputId].Input->SetEvaluationOffsetInSeconds(EvaluationOffset);
		}
	}

	return Result;
}


void UTimedDataMonitorSubsystem::ResetAllBufferStats()
{
	BuildSourcesListIfNeeded();

	for (auto& InputItt : InputMap)
	{
		InputItt.Value.Input->ResetBufferStats();
		InputItt.Value.ResetValue();
	}
}


bool UTimedDataMonitorSubsystem::DoesGroupExist(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return GroupMap.Find(Identifier) != nullptr;
}


FText UTimedDataMonitorSubsystem::GetGroupDisplayName(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (Identifier == OtherGroupIdentifier)
	{
		return LOCTEXT("DefaultGroupName", "Other");
	}
	else if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		check(GroupItem->Group);
		return GroupItem->Group->GetDisplayName();
	}

	return FText::GetEmpty();
}


void UTimedDataMonitorSubsystem::GetGroupDataBufferSize(const FTimedDataMonitorGroupIdentifier& Identifier, int32& OutMinBufferSize, int32& OutMaxBufferSize)
{
	BuildSourcesListIfNeeded();

	int32 MinValue = TNumericLimits<int32>::Max();
	int32 MaxValue = TNumericLimits<int32>::Min();
	bool bHasElement = false;
	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			if (InputItem.bEnabled)
			{
				int32 BufferSize = InputItem.Input->GetDataBufferSize();
				MinValue = FMath::Min(BufferSize, MinValue);
				MaxValue = FMath::Max(BufferSize, MaxValue);
				bHasElement = true;
			}
		}
	}

	OutMinBufferSize = bHasElement ? MinValue : 0; 
	OutMaxBufferSize = bHasElement ? MaxValue : 0;
}


void UTimedDataMonitorSubsystem::SetGroupDataBufferSize(const FTimedDataMonitorGroupIdentifier& Identifier, int32 BufferSize)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			if (InputItem.bEnabled)
			{
				InputItem.Input->SetDataBufferSize(BufferSize);
			}
		}
	}
}


ETimedDataInputState UTimedDataMonitorSubsystem::GetGroupState(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	ETimedDataInputState WorstState = ETimedDataInputState::Connected;
	bool bHasAtLeastOneItem = false; 
	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			if (InputItem.bEnabled)
			{
				bHasAtLeastOneItem = true;
				ETimedDataInputState InputState = InputItem.Input->GetState();
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


void UTimedDataMonitorSubsystem::ResetGroupBufferStats(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			InputItem.Input->ResetBufferStats();
		}
	}
}


ETimedDataMonitorGroupEnabled UTimedDataMonitorSubsystem::GetGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		int32 bCountEnabled = 0;
		int32 bCountDisabled = 0;
		for (const FTimedDataMonitorInputIdentifier& Input : GroupItem->InputIdentifiers)
		{
			if (InputMap[Input].bEnabled)
			{
				++bCountEnabled;
				if (bCountDisabled > 0)
				{
					return ETimedDataMonitorGroupEnabled::MultipleValues;
				}
			}
			else
			{
				++bCountDisabled;
				if (bCountEnabled > 0)
				{
					return ETimedDataMonitorGroupEnabled::MultipleValues;
				}
			}
		}
		return bCountEnabled > 0 ? ETimedDataMonitorGroupEnabled::Enabled : ETimedDataMonitorGroupEnabled::Disabled;
	}

	return ETimedDataMonitorGroupEnabled::Disabled;
}


void UTimedDataMonitorSubsystem::SetGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier, bool bInEnabled)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputId : GroupItem->InputIdentifiers)
		{
			InputMap[InputId].bEnabled = bInEnabled;
		}
	}
}


bool UTimedDataMonitorSubsystem::DoesInputExist(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return InputMap.Find(Identifier) != nullptr;
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


FTimedDataMonitorGroupIdentifier UTimedDataMonitorSubsystem::GetInputGroup(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->GroupIdentifier;
	}

	return FTimedDataMonitorGroupIdentifier();
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


ETimedDataInputState UTimedDataMonitorSubsystem::GetInputState(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetState();
	}

	return ETimedDataInputState::Disconnected;
}


FFrameRate UTimedDataMonitorSubsystem::GetInputFrameRate(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetFrameRate();
	}

	return ITimedDataInput::UnknowedFrameRate;
}


FTimedDataInputSampleTime UTimedDataMonitorSubsystem::GetInputOldestDataTime(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetOldestDataTime();
	}

	return FTimedDataInputSampleTime();
}

FTimedDataInputSampleTime UTimedDataMonitorSubsystem::GetInputNewestDataTime(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetNewestDataTime();
	}

	return FTimedDataInputSampleTime();
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


bool UTimedDataMonitorSubsystem::IsInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->bEnabled;
	}

	return false;
}


void UTimedDataMonitorSubsystem::ResetInputBufferStats(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		SourceItem->Input->ResetBufferStats();
	}
}


void UTimedDataMonitorSubsystem::SetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier, bool bInEnabled)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		SourceItem->bEnabled = bInEnabled;
	}
}


int32 UTimedDataMonitorSubsystem::GetBufferUnderflowStat(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetBufferUnderflowStat();
	}

	return 0;
}

int32 UTimedDataMonitorSubsystem::GetBufferOverflowStat(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetBufferOverflowStat();
	}

	return 0;
}

int32 UTimedDataMonitorSubsystem::GetFrameDroppedStat(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetFrameDroppedStat();
	}

	return 0;
}

float UTimedDataMonitorSubsystem::GetEvaluationDistanceToNewestSampleMean(const FTimedDataMonitorInputIdentifier& Identifier)
{
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Statistics.IncrementalAverageNewestDistance;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetEvaluationDistanceToOldestSampleMean(const FTimedDataMonitorInputIdentifier& Identifier)
{
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Statistics.IncrementalAverageOldestDistance;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetEvaluationDistanceToNewestSampleStandardDeviation(const FTimedDataMonitorInputIdentifier& Identifier)
{
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Statistics.DistanceToNewestSTD;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetEvaluationDistanceToOldestSampleStandardDeviation(const FTimedDataMonitorInputIdentifier& Identifier)
{
	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Statistics.DistanceToOldestSTD;
	}

	return 0.0f;
}

void UTimedDataMonitorSubsystem::GetLastEvaluationDataStat(const FTimedDataMonitorInputIdentifier& Identifier, FTimedDataInputEvaluationData& Result)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		SourceItem->Input->GetLastEvaluationData(Result);
	}
}

void UTimedDataMonitorSubsystem::BuildSourcesListIfNeeded()
{
	if (bRequestSourceListRebuilt)
	{
		if (!ITimeManagementModule::IsAvailable())
		{
			GroupMap.Reset();
			InputMap.Reset();
		}
		else
		{
			bRequestSourceListRebuilt = false;

			// Build ReverseGroupMap
			TMap<ITimedDataInputGroup*, FTimedDataMonitorGroupIdentifier> ReverseGroupMap;
			for (const auto& Itt : GroupMap)
			{
				if (Itt.Value.Group)
				{
					ReverseGroupMap.Add(Itt.Value.Group, Itt.Key);
				}
			}

			const TArray<ITimedDataInputGroup*>& TimedDataGoups = ITimeManagementModule::Get().GetTimedDataInputCollection().GetGroups();
			const TArray<ITimedDataInput*>& TimedDataInputs = ITimeManagementModule::Get().GetTimedDataInputCollection().GetInputs();

			TArray<FTimedDataMonitorGroupIdentifier> PreviousGroupList;
			GroupMap.GenerateKeyArray(PreviousGroupList);

			TArray<FTimedDataMonitorInputIdentifier> PreviousInputList;
			InputMap.GenerateKeyArray(PreviousInputList);

			// Regenerate the list of group
			{
				for (ITimedDataInputGroup* TimedDataGoup : TimedDataGoups)
				{
					if(TimedDataGoup == nullptr)
					{
						continue;
					}

					FTimedDataMonitorGroupIdentifier* FoundGroupIdentifier = ReverseGroupMap.Find(TimedDataGoup);
					if (FoundGroupIdentifier)
					{
						PreviousGroupList.RemoveSingleSwap(*FoundGroupIdentifier);
						GroupMap[*FoundGroupIdentifier].ResetValue();
					}
					else
					{
						// if not found, add it to the list
						FTimeDataInputItemGroup NewGroup; 
						NewGroup.Group = TimedDataGoup;

						FTimedDataMonitorGroupIdentifier GroupIdentifier = FTimedDataMonitorGroupIdentifier::NewIdentifier();
						GroupMap.Add(GroupIdentifier, MoveTemp(NewGroup));
						ReverseGroupMap.Add(TimedDataGoup, GroupIdentifier);
					}
				}
			}

			// Look to see if there is any item that needs the Other group
			{
				for (ITimedDataInput* TimedDataInput : TimedDataInputs)
				{
					if (TimedDataInput && TimedDataInput->GetGroup() == nullptr)
					{
						PreviousGroupList.RemoveSingleSwap(OtherGroupIdentifier);
						FTimeDataInputItemGroup* FoundGroup = GroupMap.Find(OtherGroupIdentifier);
						if (FoundGroup)
						{
							FoundGroup->ResetValue();
						}
						break;
					}
				}
			}

			// Remove old groups
			{
				check(PreviousGroupList.Num() == 0);
				for (const FTimedDataMonitorGroupIdentifier& Old : PreviousGroupList)
				{
					GroupMap.Remove(Old);
				}
			}

			// Regenerate the list of inputs
			{
				for (ITimedDataInput* TimedDataInput : TimedDataInputs)
				{
					if (TimedDataInput == nullptr)
					{
						continue;
					}

					FTimedDataMonitorGroupIdentifier GroupIdentifier = OtherGroupIdentifier;
					if (ITimedDataInputGroup* Group = TimedDataInput->GetGroup())
					{
						FTimedDataMonitorGroupIdentifier* FoundGroupIdentifier = ReverseGroupMap.Find(Group);
						if (ensure(FoundGroupIdentifier))
						{
							GroupIdentifier = *FoundGroupIdentifier;
						}
					}

					// Find and remove from the Previous list
					bool bFound = false;
					for (auto& Itt : InputMap)
					{
						if (Itt.Value.Input == TimedDataInput)
						{
							bFound = true;
							PreviousInputList.RemoveSingleSwap(Itt.Key);

							Itt.Value.GroupIdentifier = GroupIdentifier;
							Itt.Value.ResetValue();
							break;
						}
					}

					// if not found, add it to the list
					if (!bFound)
					{
						FTimeDataInputItem NewInput;
						NewInput.Input = TimedDataInput;
						NewInput.GroupIdentifier = GroupIdentifier;
						
						FTimedDataMonitorInputIdentifier NewIdentifier = FTimedDataMonitorInputIdentifier::NewIdentifier();
						InputMap.Add(NewIdentifier, MoveTemp(NewInput));
					}
				}
			}

			// Remove old inputs
			{
				check(PreviousInputList.Num() == 0);
				for (const FTimedDataMonitorInputIdentifier& Old : PreviousInputList)
				{
					InputMap.Remove(Old);
				}
			}

			// generate group's input list
			for (const auto& Itt : InputMap)
			{
				FTimeDataInputItemGroup* FoundGroup = GroupMap.Find(Itt.Value.GroupIdentifier);
				if (ensure(FoundGroup))
				{
					FoundGroup->InputIdentifiers.Add(Itt.Key);
				}
			}
		}
	}
}


void UTimedDataMonitorSubsystem::OnTimedDataSourceCollectionChanged()
{
	bRequestSourceListRebuilt = true;

	// update map right away to not have dandling pointer
	const TArray<ITimedDataInputGroup*>& TimedDataGroups = ITimeManagementModule::Get().GetTimedDataInputCollection().GetGroups();
	TArray<FTimedDataMonitorGroupIdentifier, TInlineAllocator<4>> GroupToRemove;
	for (const auto& Itt : GroupMap)
	{
		if (!TimedDataGroups.Contains(Itt.Value.Group))
		{
			GroupToRemove.Add(Itt.Key);
		}
	}
	for (const FTimedDataMonitorGroupIdentifier& Id : GroupToRemove)
	{
		if (Id != OtherGroupIdentifier)
		{
			GroupMap.Remove(Id);
		}
	}

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
		InputMap.Remove(Id);
	}

	OnIdentifierListChanged_Delegate.Broadcast();
	OnIdentifierListChanged_Dynamic.Broadcast();
}

void UTimedDataMonitorSubsystem::OnWorldPostTick(UWorld* /*World*/, ELevelTick/**Tick Type*/, float/**Delta Seconds*/)
{
	const bool bUpdateStats = CVarEnableTimedDataMonitorSubsystemStats.GetValueOnGameThread();
	if (bUpdateStats)
	{
		UpdateEvaluationStatistics();
	}
}

void UTimedDataMonitorSubsystem::UpdateEvaluationStatistics()
{
	for (TPair<FTimedDataMonitorInputIdentifier, FTimeDataInputItem>& Item : InputMap)
	{
		if (Item.Value.bEnabled)
		{
			FTimedDataInputEvaluationData Data;
			GetLastEvaluationDataStat(Item.Key, Data);

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
