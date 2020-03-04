// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimedDataMonitorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ITimeManagementModule.h"
#include "LatentActions.h"
#include "Misc/App.h"
#include "Misc/QualifiedFrameTime.h"
#include "Stats/Stats2.h"
#include "TimedDataInputCollection.h"
#include "TimedDataMonitorCalibration.h"
#include "UObject/Stack.h"


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


 double UTimedDataMonitorSubsystem::GetEvaluationTime(ETimedDataInputEvaluationType EvaluationType)
 {
	 double Result = 0.0;
	 switch (EvaluationType)
	 {
	 case ETimedDataInputEvaluationType::Timecode:
		 if (FApp::GetCurrentFrameTime().IsSet())
		 {
			 Result = FApp::GetCurrentFrameTime().GetValue().AsSeconds();
		 }
		 break;
	 case ETimedDataInputEvaluationType::PlatformTime:
		 Result = FApp::GetCurrentTime();
		 break;
	 case ETimedDataInputEvaluationType::None:
	 default:
		 break;
	 }
	 return Result;
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


void UTimedDataMonitorSubsystem::CalibrateLatent(UObject* WorldContextObject, struct FLatentActionInfo LatentInfo, const FTimedDataMonitorCalibrationParameters& CalibrationParameters, FTimedDataMonitorCalibrationResult& Result)
{
	struct FCalibrateAction : public FPendingLatentAction
	{
	public:
		FName ExecutionFunction;
		int32 Linkage;
		FWeakObjectPtr CallbackTarget;
		FTimedDataMonitorCalibrationResult& Result;
		TUniquePtr<FTimedDataMonitorCalibration> Calibration;
		bool bOnCompleted;

		FCalibrateAction(const FLatentActionInfo& InLatentInfo, FTimedDataMonitorCalibrationResult& InResult)
			: FPendingLatentAction()
			, ExecutionFunction(InLatentInfo.ExecutionFunction)
			, Linkage(InLatentInfo.Linkage)
			, CallbackTarget(InLatentInfo.CallbackTarget)
			, Result(InResult)
			, Calibration(new FTimedDataMonitorCalibration)
			, bOnCompleted(false)
		{
		}

		virtual void UpdateOperation(FLatentResponse& Response) override
		{
			if (bOnCompleted)
			{
				Response.FinishAndTriggerIf(true, ExecutionFunction, Linkage, CallbackTarget);
			}
		}

#if WITH_EDITOR
		virtual FString GetDescription() const override
		{
			return FString::Printf(TEXT("Calibrating."));
		}
#endif

		void OnCompleted(FTimedDataMonitorCalibrationResult InResult)
		{
			Result = InResult;
			bOnCompleted = true;
		}
	};

	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		FLatentActionManager& LatentManager = World->GetLatentActionManager();
		if (LatentManager.FindExistingAction<FCalibrateAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
		{
			FCalibrateAction* NewAction = new FCalibrateAction(LatentInfo, Result);
			NewAction->Calibration->CalibrateWithTimecode(CalibrationParameters, FTimedDataMonitorCalibration::FOnCalibrationCompletedSignature::CreateRaw(NewAction, &FCalibrateAction::OnCompleted));
			LatentManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, NewAction);
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("The calibration is already running."), ELogVerbosity::Warning, "CalibrationActionAlreadyStarted");
		}
	}
};


FTimedDataMonitorTimeCorrectionResult UTimedDataMonitorSubsystem::ApplyTimeCorrection(const FTimedDataMonitorInputIdentifier& InputIdentifier, const FTimedDataMonitorTimeCorrectionParameters& TimeCorrectionParameters)
{
	return FTimedDataMonitorCalibration::ApplyTimeCorrection(InputIdentifier, TimeCorrectionParameters);
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


ETimedDataMonitorEvaluationState UTimedDataMonitorSubsystem::GetEvaluationState()
{
	BuildSourcesListIfNeeded();

	ETimedDataMonitorEvaluationState WorstState = ETimedDataMonitorEvaluationState::NoSample;
	if (InputMap.Num() > 0)
	{
		WorstState = ETimedDataMonitorEvaluationState::Disabled;
		for (const auto& InputItt : InputMap)
		{
			const ETimedDataMonitorEvaluationState InputState = GetInputEvaluationState(InputItt.Key);
			uint8 InputValue = FMath::Min((uint8)InputState, (uint8)WorstState);
			WorstState = (ETimedDataMonitorEvaluationState)InputValue;
		}
	}

	return WorstState;
}


bool UTimedDataMonitorSubsystem::DoesInputExist(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return InputMap.Contains(Identifier);
}


ETimedDataMonitorInputEnabled UTimedDataMonitorSubsystem::GetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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

	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetDisplayName();
	}

	return FText::GetEmpty();
}


TArray<FTimedDataMonitorChannelIdentifier> UTimedDataMonitorSubsystem::GetInputChannels(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->ChannelIdentifiers;
	}

	return TArray<FTimedDataMonitorChannelIdentifier>();
}


ETimedDataInputEvaluationType UTimedDataMonitorSubsystem::GetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
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

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
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

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetFrameRate();
	}

	return ITimedDataInput::UnknownFrameRate;
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetInputOldestDataTime(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	FTimedDataChannelSampleTime ResultSampleTime(0.0, FQualifiedFrameTime());
	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		bool bFirstElement = true;
		for (const FTimedDataMonitorChannelIdentifier ChannelIdentifier : SourceItem->ChannelIdentifiers)
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
	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
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

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->IsDataBufferSizeControlledByInput();
	}

	return false;
}


int32 UTimedDataMonitorSubsystem::GetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
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

ETimedDataInputState UTimedDataMonitorSubsystem::GetInputConnectionState(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	ETimedDataInputState WorstState = ETimedDataInputState::Connected;
	bool bHasAtLeastOneItem = false;
	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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


ETimedDataMonitorEvaluationState UTimedDataMonitorSubsystem::GetInputEvaluationState(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	ETimedDataMonitorEvaluationState WorstState = ETimedDataMonitorEvaluationState::NoSample;
	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
	{
		if (InputItem->ChannelIdentifiers.Num() > 0)
		{
			WorstState = ETimedDataMonitorEvaluationState::Disabled;
			for (const FTimedDataMonitorChannelIdentifier& ChannelIdentifier : InputItem->ChannelIdentifiers)
			{
				const ETimedDataMonitorEvaluationState ChannelState = GetChannelEvaluationState(ChannelIdentifier);
				uint8 ChannelValue = FMath::Min((uint8)ChannelState, (uint8)WorstState);
				WorstState = (ETimedDataMonitorEvaluationState)ChannelValue;
			}
		}

	}

	return WorstState;
}


float UTimedDataMonitorSubsystem::GetInputEvaluationDistanceToNewestSampleMean(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();
	
	float WorstNewestMean = 0.f;
	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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
	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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
	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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
	if (const FTimeDataInputItem* InputItem = InputMap.Find(Identifier))
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

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
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

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->InputIdentifier;
	}

	return FTimedDataMonitorInputIdentifier();
}


FText UTimedDataMonitorSubsystem::GetChannelDisplayName(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetDisplayName();
	}

	return FText::GetEmpty();
}


ETimedDataInputState UTimedDataMonitorSubsystem::GetChannelConnectionState(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetState();
	}

	return ETimedDataInputState::Disconnected;
}


ETimedDataMonitorEvaluationState UTimedDataMonitorSubsystem::GetChannelEvaluationState(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		if (SourceItem->Channel->GetState() != ETimedDataInputState::Connected || !SourceItem->bEnabled)
		{
			return ETimedDataMonitorEvaluationState::Disabled;
		}
		if (SourceItem->Channel->GetNumberOfSamples() <= 0)
		{
			return ETimedDataMonitorEvaluationState::NoSample;
		}

		const ITimedDataInput* Input = InputMap[SourceItem->InputIdentifier].Input;
		check(Input);
		const ETimedDataInputEvaluationType EvaluationType = Input->GetEvaluationType();
		const double EvaluationOffset = Input->GetEvaluationOffsetInSeconds();
		const double OldestSampleTime = SourceItem->Channel->GetOldestDataTime().AsSeconds(EvaluationType);
		const double NewstedSampleTime = SourceItem->Channel->GetNewestDataTime().AsSeconds(EvaluationType);
		const double EvaluationTime = GetEvaluationTime(EvaluationType);
		const double OffsettedEvaluationTime = EvaluationTime - EvaluationOffset;
		bool bIsInRange = (OffsettedEvaluationTime >= OldestSampleTime) && (OffsettedEvaluationTime <= NewstedSampleTime);
		return bIsInRange ? ETimedDataMonitorEvaluationState::InsideRange : ETimedDataMonitorEvaluationState::OutsideRange;
	}

	return ETimedDataMonitorEvaluationState::Disabled;
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetChannelOldestDataTime(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetOldestDataTime();
	}

	return FTimedDataChannelSampleTime();
}


FTimedDataChannelSampleTime UTimedDataMonitorSubsystem::GetChannelNewestDataTime(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetNewestDataTime();
	}

	return FTimedDataChannelSampleTime();
}


int32 UTimedDataMonitorSubsystem::GetChannelNumberOfSamples(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetNumberOfSamples();
	}

	return 0;
}


int32 UTimedDataMonitorSubsystem::GetChannelDataBufferSize(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
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

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetBufferUnderflowStat();
	}

	return 0;
}

int32 UTimedDataMonitorSubsystem::GetChannelBufferOverflowStat(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetBufferOverflowStat();
	}

	return 0;
}

int32 UTimedDataMonitorSubsystem::GetChannelFrameDroppedStat(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Channel->GetFrameDroppedStat();
	}

	return 0;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToNewestSampleMean(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.IncrementalAverageNewestDistance;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToOldestSampleMean(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.IncrementalAverageOldestDistance;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToNewestSampleStandardDeviation(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.DistanceToNewestSTD;
	}

	return 0.0f;
}

float UTimedDataMonitorSubsystem::GetChannelEvaluationDistanceToOldestSampleStandardDeviation(const FTimedDataMonitorChannelIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
	{
		return SourceItem->Statistics.DistanceToOldestSTD;
	}

	return 0.0f;
}

void UTimedDataMonitorSubsystem::GetChannelLastEvaluationDataStat(const FTimedDataMonitorChannelIdentifier& Identifier, FTimedDataInputEvaluationData& Result)
{
	BuildSourcesListIfNeeded();

	if (const FTimeDataChannelItem* SourceItem = ChannelMap.Find(Identifier))
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
