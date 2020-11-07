// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmixEffects/SubmixEffectConvolutionReverb.h"

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "ConvolutionReverb.h"
#include "DSP/ConvolutionAlgorithm.h"
#include "SynthesisModule.h"

namespace AudioConvReverbIntrinsics
{
	using namespace Audio;

	// names of various smart pointers
	using FSubmixEffectConvSharedPtr = TSharedPtr<FSubmixEffectConvolutionReverb, ESPMode::ThreadSafe>;
	using FSubmixEffectConvWeakPtr = TWeakPtr<FSubmixEffectConvolutionReverb, ESPMode::ThreadSafe>;

	using FVersionData = FSubmixEffectConvolutionReverb::FVersionData;

	// Task for creating convolution algorithm object.
	class FCreateConvolutionReverbTask : public FNonAbandonableTask
	{
		// This task can delete itself
		friend class FAutoDeleteAsyncTask<FCreateConvolutionReverbTask>;

		public:
			FCreateConvolutionReverbTask(
					FSubmixEffectConvWeakPtr InEffectObject,
					FConvolutionReverbInitData&& InInitData,
					FConvolutionReverbSettings& InSettings,
					const FVersionData& InVersionData)
			:	EffectWeakPtr(InEffectObject)
			,	InitData(MoveTemp(InInitData))
			,	Settings(InSettings)
			,	VersionData(InVersionData)
			{}

			void DoWork()
			{
				// Build the convolution reverb object. 
				TUniquePtr<FConvolutionReverb> ConvReverb = FConvolutionReverb::CreateConvolutionReverb(InitData, Settings);

				FSubmixEffectConvSharedPtr EffectSharedPtr = EffectWeakPtr.Pin();

				// Check that the submix effect still exists.
				if (EffectSharedPtr.IsValid())
				{
					// If the effect ptr is still valid, add to it's command queue to set the convolution
					// algorithm object in the audio render thread.
					TUniqueFunction<void()> Command = [Algo = MoveTemp(ConvReverb), EffectSharedPtr, VersionData = VersionData] () mutable
					{
						EffectSharedPtr->SetConvolutionReverbIfCurrent(MoveTemp(Algo), VersionData);
					};
					EffectSharedPtr->EffectCommand(MoveTemp(Command));
				}
			}

			FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(CreateConvolutionReverbTask,     STATGROUP_ThreadPoolAsyncTasks); }

		private:
			FSubmixEffectConvWeakPtr EffectWeakPtr;

			FConvolutionReverbInitData InitData;
			FConvolutionReverbSettings Settings;
			FVersionData VersionData;
	};
}

UAudioImpulseResponse::UAudioImpulseResponse()
:	NumChannels(0)
,	SampleRate(0)
,	NormalizationVolumeDb(-24.f)
{
}

#if WITH_EDITORONLY_DATA
void UAudioImpulseResponse::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnObjectPropertyChanged.Broadcast(PropertyChangedEvent);
}
#endif

FSubmixEffectConvolutionReverbSettings::FSubmixEffectConvolutionReverbSettings()
:	NormalizationVolumeDb(-24.f)
,	bBypass(false)
,	bMixInputChannelFormatToImpulseResponseFormat(true)
,	bMixReverbOutputToOutputChannelFormat(true)
,	SurroundRearChannelBleedDb(-60.f)
,	bInvertRearChannelBleedPhase(false)
,	bSurroundRearChannelFlip(false)
,	SurroundRearChannelBleedAmount_DEPRECATED(0.0f)
,	ImpulseResponse_DEPRECATED(nullptr)
,	AllowHardwareAcceleration_DEPRECATED(true)
{
}

FSubmixEffectConvolutionReverb::FVersionData::FVersionData()
: 	ConvolutionID(0)
{
}

bool FSubmixEffectConvolutionReverb::FVersionData::operator==(const FVersionData& Other) const
{
	// Check if versions are equal
	return ConvolutionID == Other.ConvolutionID;
}

FSubmixEffectConvolutionReverb::FSubmixEffectConvolutionReverb(const USubmixEffectConvolutionReverbPreset* InPreset)
: 	ConvolutionReverb(TUniquePtr<Audio::FConvolutionReverb>(nullptr))
,	SampleRate(0.0f)
,	NumInputChannels(2)
,	NumOutputChannels(2)
,	bBypass(false)
{
	UpdateConvolutionReverb(InPreset);

	if (nullptr != InPreset)
	{
		SetConvolutionReverbParameters(InPreset->GetSettings());
	}
}

FSubmixEffectConvolutionReverb::~FSubmixEffectConvolutionReverb()
{
}

void FSubmixEffectConvolutionReverb::Init(const FSoundEffectSubmixInitData& InitData)
{
	using namespace Audio;

	SampleRate = InitData.SampleRate;

	FVersionData UpdatedVersionData = UpdateVersion();

	// Create the convolution algorithm init data
	FConvolutionReverbInitData ConvolutionInitData = CreateConvolutionReverbInitData();

	FConvolutionReverbSettings Settings;
	Params.CopyParams(Settings);

	// Create the convolution reverb
	TUniquePtr<FConvolutionReverb> ConvReverb = FConvolutionReverb::CreateConvolutionReverb(ConvolutionInitData, Settings);

	// Set the convolution algorithm, ensuring that it's the most up-to-date
	SetConvolutionReverbIfCurrent(MoveTemp(ConvReverb), UpdatedVersionData);
}


void FSubmixEffectConvolutionReverb::OnPresetChanged()
{
	USubmixEffectConvolutionReverbPreset* ConvolutionPreset = CastChecked<USubmixEffectConvolutionReverbPreset>(Preset);
	FSubmixEffectConvolutionReverbSettings Settings = ConvolutionPreset->GetSettings();;

	// Copy settings from FSubmixEffectConvolutionReverbSettings needed for FConvolutionReverbSettings 
	// FConvolutionReverbSettings represents runtime settings which do not need the 
	// FConvlutionReverb object to be rebuilt. Some settings in FSubmixEffectConvolutionReverbSettings
	// force a rebuild of FConvolutionReverb. Those are handled in USubmixEffectConvolutinReverbPreset::PostEditChangeProperty
	SetConvolutionReverbParameters(Settings);
}

void FSubmixEffectConvolutionReverb::SetConvolutionReverbParameters(const FSubmixEffectConvolutionReverbSettings& InSettings)
{
	Audio::FConvolutionReverbSettings ReverbSettings;

	float NewVolume = Audio::ConvertToLinear(InSettings.NormalizationVolumeDb);
	float NewRearChannelBleed = Audio::ConvertToLinear(InSettings.SurroundRearChannelBleedDb);

	if (InSettings.bInvertRearChannelBleedPhase)
	{
		NewRearChannelBleed *= -1.f;
	}

	ReverbSettings.NormalizationVolume = NewVolume;
	ReverbSettings.RearChannelBleed = NewRearChannelBleed;
	ReverbSettings.bRearChannelFlip = InSettings.bSurroundRearChannelFlip;

	Params.SetParams(ReverbSettings);

	bBypass = InSettings.bBypass;
}


Audio::FConvolutionReverbInitData FSubmixEffectConvolutionReverb::CreateConvolutionReverbInitData() 
{
	FScopeLock ConvReverbInitDataLock(&ConvReverbInitDataCriticalSection);

	int32 NumInputChannelsLocal = NumInputChannels.Load();
	int32 NumOutputChannelsLocal = NumOutputChannels.Load();

	ConvReverbInitData.InputAudioFormat.NumChannels = NumInputChannelsLocal;
	ConvReverbInitData.OutputAudioFormat.NumChannels = NumOutputChannelsLocal;
	ConvReverbInitData.TargetSampleRate = SampleRate;

	// Mono to true stereo is a special case where we have the true stereo flag
	// checked but only have 2 IRs
	bool bIsMonoToTrueStereo = ConvReverbInitData.bIsImpulseTrueStereo && (ConvReverbInitData.AlgorithmSettings.NumImpulseResponses == 2);

	// Determine correct input channel counts 
	if (ConvReverbInitData.bMixInputChannelFormatToImpulseResponseFormat)
	{
		if (bIsMonoToTrueStereo)
		{
			// Force mono input.
			ConvReverbInitData.AlgorithmSettings.NumInputChannels = 1;
		}
		else if (ConvReverbInitData.bIsImpulseTrueStereo)
		{
			// If performing true stereo, force input to be stereo
			ConvReverbInitData.AlgorithmSettings.NumInputChannels = 2;
		}
		else
		{
			ConvReverbInitData.AlgorithmSettings.NumInputChannels = ConvReverbInitData.AlgorithmSettings.NumImpulseResponses;
		}
	}
	else
	{
		ConvReverbInitData.AlgorithmSettings.NumInputChannels = NumInputChannelsLocal;
	}


	// Determine correct output channel count
	if (bIsMonoToTrueStereo)
	{
		ConvReverbInitData.AlgorithmSettings.NumOutputChannels = 2;
	}
	else if (ConvReverbInitData.bIsImpulseTrueStereo)
	{
		// If IRs are true stereo, divide output channel count in half.
		ConvReverbInitData.AlgorithmSettings.NumOutputChannels = ConvReverbInitData.AlgorithmSettings.NumImpulseResponses / 2;
	}
	else
	{
		ConvReverbInitData.AlgorithmSettings.NumOutputChannels = ConvReverbInitData.AlgorithmSettings.NumImpulseResponses;
	}

	// Determine longest IR
	ConvReverbInitData.AlgorithmSettings.MaxNumImpulseResponseSamples = 0;

	if (ConvReverbInitData.AlgorithmSettings.NumImpulseResponses > 0)
	{
		// Determine sample rate ratio in order to calculate the final IR num samples. 
		float SampleRateRatio = 1.f;
		if (ConvReverbInitData.ImpulseSampleRate > 0.f)
		{
			SampleRateRatio = ConvReverbInitData.TargetSampleRate / ConvReverbInitData.ImpulseSampleRate;
		}

		ConvReverbInitData.AlgorithmSettings.MaxNumImpulseResponseSamples = FMath::CeilToInt(SampleRateRatio * ConvReverbInitData.Samples.Num() / ConvReverbInitData.AlgorithmSettings.NumImpulseResponses) + 256;
	}

	// Setup gain matrix.
	ConvReverbInitData.GainMatrix.Reset();

	if (bIsMonoToTrueStereo)
	{
		ConvReverbInitData.GainMatrix.Emplace(0, 0, 0, 1.f);
		ConvReverbInitData.GainMatrix.Emplace(0, 1, 1, 1.f);
	}
	else if (ConvReverbInitData.bIsImpulseTrueStereo)
	{
		// True stereo treats first group of IRs 
		int32 NumPairs = ConvReverbInitData.AlgorithmSettings.NumImpulseResponses / 2;

		for (int32 PairIndex = 0; PairIndex < NumPairs; PairIndex++)
		{
			int32 LeftImpulseIndex = PairIndex;
			int32 RightImpulseIndex = NumPairs + PairIndex;

			ConvReverbInitData.GainMatrix.Emplace(0, LeftImpulseIndex,  PairIndex, 1.f);
			ConvReverbInitData.GainMatrix.Emplace(1, RightImpulseIndex, PairIndex, 1.f);
		}
	}
	else
	{
		// Set up a 1-to-1 mapping. 
		int32 MinChannelCount = FMath::Min3(ConvReverbInitData.AlgorithmSettings.NumInputChannels, ConvReverbInitData.AlgorithmSettings.NumOutputChannels, ConvReverbInitData.AlgorithmSettings.NumImpulseResponses);

		for (int32 i = 0; i < MinChannelCount; i++)
		{
			ConvReverbInitData.GainMatrix.Emplace(i, i, i, 1.f);
		}
	}

	return ConvReverbInitData;
}

Audio::FConvolutionReverbSettings FSubmixEffectConvolutionReverb::GetParameters() const
{
	Audio::FConvolutionReverbSettings Settings;

	Params.CopyParams(Settings);

	return Settings;
}

void FSubmixEffectConvolutionReverb::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	using namespace Audio;

	check(nullptr != InData.AudioBuffer);
	check(nullptr != OutData.AudioBuffer);

	UpdateParameters();

	int32 ExpectedNumInputChannels = 0;
	int32 ExpectedNumOutputChannels = 0;

	if (ConvolutionReverb.IsValid())
	{
		ExpectedNumInputChannels = ConvolutionReverb->GetNumInputChannels();
		ExpectedNumOutputChannels = ConvolutionReverb->GetNumOutputChannels();
	}

	// Check if there is a channel mismatch between the convolution reverb and the input/output data
	bool bNumChannelsMismatch = false;
	bNumChannelsMismatch |= (ExpectedNumInputChannels != InData.NumChannels);
	bNumChannelsMismatch |= (ExpectedNumOutputChannels != OutData.NumChannels);

	if (bNumChannelsMismatch)
	{
		int32 NumInputChannelsTemp = NumInputChannels.Load();
		int32 NumOutputChannelsTemp = NumOutputChannels.Load();

		// We should create a new algorithm if the input and output channels are non-zero.
		// But we check the NumInputChannelsTemp/NumOutputChannelsTemp to check if we've
		// already tried and failed to create an algorithm for this channel configuration.
		bool bShouldCreateNewAlgo = (NumInputChannelsTemp != InData.NumChannels) || (NumOutputChannelsTemp != OutData.NumChannels);
		bShouldCreateNewAlgo &= (InData.NumChannels > 0);
		bShouldCreateNewAlgo &= (OutData.NumChannels > 0);

		if (bShouldCreateNewAlgo)
		{
			UE_LOG(LogSynthesis, Log, TEXT("Creating new convolution algorithm due to channel count update. Num Inputs %d -> %d. Num Outputs %d -> %d"), ExpectedNumInputChannels, InData.NumChannels, ExpectedNumOutputChannels, OutData.NumChannels);

			// We don't update version data when changing the channel configuration
			FVersionData CurrentVersionData;
			{
				FScopeLock VersionLock(&VersionDataCriticalSection);
				CurrentVersionData = VersionData;
			}

			// These should only be updated here and in the constructor since we use them
			// to see if we've tried this channel configuration already.
			NumInputChannels.Store(InData.NumChannels);
			NumOutputChannels.Store(OutData.NumChannels);

			// Preferably, this should be done in a task. To do so, we would need 
			// access to a weak ptr to "this". Currently that is not possible since this object
			// is owned by the Preset, and the TWeakObjectPtr cannot safely be copied across thread
			// boundaries. 
			{
				// Create the convolution algorithm init data
				FConvolutionReverbInitData ConvolutionInitData = CreateConvolutionReverbInitData();

				FConvolutionReverbSettings Settings;
				Params.CopyParams(Settings);

				// Create the convolution reverb
				TUniquePtr<FConvolutionReverb> ConvReverb = FConvolutionReverb::CreateConvolutionReverb(ConvolutionInitData, Settings);

				// Set the convolution algorithm, ensuring that it's the most up-to-date
				SetConvolutionReverbIfCurrent(MoveTemp(ConvReverb), CurrentVersionData);
			}

			if (ConvolutionReverb.IsValid())
			{
				ExpectedNumInputChannels = ConvolutionReverb->GetNumInputChannels();
				ExpectedNumOutputChannels = ConvolutionReverb->GetNumOutputChannels();
			}
		}
	}

	const bool bShouldProcessConvReverb = !bBypass &&
		ConvolutionReverb.IsValid() && 
		(ExpectedNumInputChannels == InData.NumChannels) && 
		(ExpectedNumOutputChannels == OutData.NumChannels);

	if (bShouldProcessConvReverb)
	{
		ConvolutionReverb->ProcessAudio(InData.NumChannels, *InData.AudioBuffer, OutData.NumChannels, *OutData.AudioBuffer);
	}
	else if (bBypass)
	{
		*OutData.AudioBuffer = *InData.AudioBuffer;
	}
	else
	{
		// Zero output data. Do *not* trigger rebuild here in case one is already in flight or simply because one cannot be built.
		OutData.AudioBuffer->Reset();

		int32 OutputNumFrames = InData.NumFrames * OutData.NumChannels;
		if (OutputNumFrames > 0)
		{
			OutData.AudioBuffer->AddZeroed(OutputNumFrames);
		}
	}
}

FSubmixEffectConvolutionReverb::FVersionData FSubmixEffectConvolutionReverb::UpdateVersion()
{
	FVersionData VersionDataCopy;
	FScopeLock VersionDataLock(&VersionDataCriticalSection);

	VersionData.ConvolutionID++;
	VersionDataCopy = VersionData;

	return VersionDataCopy;
}

void FSubmixEffectConvolutionReverb::UpdateParameters()
{
	// Call on the audio render thread to update parameters.
	if (ConvolutionReverb.IsValid())
	{
		Audio::FConvolutionReverbSettings NewSettings;
		if (Params.GetParams(&NewSettings))
		{
			ConvolutionReverb->SetSettings(NewSettings);
		}
    }
}

FSubmixEffectConvolutionReverb::FVersionData FSubmixEffectConvolutionReverb::UpdateConvolutionReverb(const USubmixEffectConvolutionReverbPreset* InPreset)
{
	using namespace Audio;

	// Copy data from preset into internal FConvolutionReverbInitData
	
	// TODO: Need to update AudioMixerSubmix initialization. Some initializations
	// happen on the audio render thread.
	// check(IsInGameThread() || IsInAudioThread());
	{
		FScopeLock ConvReverbInitDataLock(&ConvReverbInitDataCriticalSection);

		// Reset data
		ConvReverbInitData = FConvolutionReverbInitData();

		if (nullptr != InPreset)
		{
			if (InPreset->ImpulseResponse)
			{
				UAudioImpulseResponse* IR = InPreset->ImpulseResponse;

				ConvReverbInitData.Samples = IR->ImpulseResponse;
				ConvReverbInitData.AlgorithmSettings.NumImpulseResponses = IR->NumChannels;
				ConvReverbInitData.ImpulseSampleRate = IR->SampleRate;
				ConvReverbInitData.bIsImpulseTrueStereo = IR->bTrueStereo && (IR->NumChannels % 2 == 0);
				ConvReverbInitData.NormalizationVolume = Audio::ConvertToLinear(IR->NormalizationVolumeDb);
			}

			switch (InPreset->BlockSize)
			{
				case ESubmixEffectConvolutionReverbBlockSize::BlockSize256:
					ConvReverbInitData.AlgorithmSettings.BlockNumSamples = 256;
					break;

				case ESubmixEffectConvolutionReverbBlockSize::BlockSize512:
					ConvReverbInitData.AlgorithmSettings.BlockNumSamples = 512;
					break;

				case ESubmixEffectConvolutionReverbBlockSize::BlockSize1024:
				default:
					ConvReverbInitData.AlgorithmSettings.BlockNumSamples = 1024;
					break;
			}

			ConvReverbInitData.AlgorithmSettings.bEnableHardwareAcceleration = InPreset->bEnableHardwareAcceleration;
			ConvReverbInitData.bMixInputChannelFormatToImpulseResponseFormat = InPreset->Settings.bMixInputChannelFormatToImpulseResponseFormat;
			ConvReverbInitData.bMixReverbOutputToOutputChannelFormat = InPreset->Settings.bMixReverbOutputToOutputChannelFormat;
		}
	}

	return UpdateVersion();
}

void FSubmixEffectConvolutionReverb::SetConvolutionReverbIfCurrent(TUniquePtr<Audio::FConvolutionReverb> InReverb, const FVersionData& InVersionData)
{
	bool bIsCurrent = true;

	{
		FScopeLock VersionDataLock(&VersionDataCriticalSection);

		bIsCurrent &= (InVersionData == VersionData);
	}

	if (bIsCurrent)
	{
		ConvolutionReverb = MoveTemp(InReverb);

		if (ConvolutionReverb.IsValid())
		{
			ConvolutionReverb->SetSettings(GetParameters());
		}
	}
}

/********************************************************************/
/***************** USubmixEffectConvolutionReverbPreset *************/
/********************************************************************/

USubmixEffectConvolutionReverbPreset::USubmixEffectConvolutionReverbPreset(const FObjectInitializer& ObjectInitializer)
:	Super(ObjectInitializer)
,	ImpulseResponse(nullptr)
,	BlockSize(ESubmixEffectConvolutionReverbBlockSize::BlockSize1024)
,	bEnableHardwareAcceleration(true)
{}

bool USubmixEffectConvolutionReverbPreset::CanFilter() const 
{
	return false; 
}

bool USubmixEffectConvolutionReverbPreset::HasAssetActions() const 
{ 
	return true; 
}

FText USubmixEffectConvolutionReverbPreset::GetAssetActionName() const 
{ 
	return FText::FromString(TEXT("SubmixEffectConvolutionReverb")); 
}

UClass* USubmixEffectConvolutionReverbPreset::GetSupportedClass() const 
{ 
	return USubmixEffectConvolutionReverbPreset::StaticClass(); 
}

FSoundEffectBase* USubmixEffectConvolutionReverbPreset::CreateNewEffect() const 
{ 
	// Pass a pointer to self into this constructor.  This enables the submix effect to have
	// a copy of the impulse response data. The submix effect can then prepare the impulse
	// response during it's "Init" routine.
	return new FSubmixEffectConvolutionReverb(this); 
}

USoundEffectPreset* USubmixEffectConvolutionReverbPreset::CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const
{ 
	USoundEffectPreset* NewPreset = NewObject<USubmixEffectConvolutionReverbPreset>(InParent, GetSupportedClass(), Name, Flags); 
	NewPreset->Init(); 
	return NewPreset; 
}

void USubmixEffectConvolutionReverbPreset::Init() 
{ 
	FScopeLock ScopeLock(&SettingsCritSect);
	SettingsCopy = Settings;
}

void USubmixEffectConvolutionReverbPreset::UpdateSettings()
{
	{
		// Copy settings to audio-render-thread version
		FScopeLock ScopeLock(&SettingsCritSect);
		SettingsCopy = Settings; 
	}
	// This sets the 'bChanged' to true on related effect instances which will 
	// trigger a OnPresetChanged call on the audio render thread.
	Update(); 
} 

FSubmixEffectConvolutionReverbSettings USubmixEffectConvolutionReverbPreset::GetSettings() const
{ 
	FScopeLock ScopeLock(&SettingsCritSect);
	return SettingsCopy; 
} 

void USubmixEffectConvolutionReverbPreset::SetSettings(const FSubmixEffectConvolutionReverbSettings& InSettings)
{
	Settings = InSettings;

	if (nullptr != ImpulseResponse)
	{
		SetImpulseResponseSettings(ImpulseResponse);
	}

	UpdateSettings();
}

void USubmixEffectConvolutionReverbPreset::SetImpulseResponse(UAudioImpulseResponse* InImpulseResponse)
{
	ImpulseResponse = InImpulseResponse;
	SetImpulseResponseSettings(InImpulseResponse);
	RebuildConvolutionReverb();
}

void USubmixEffectConvolutionReverbPreset::RebuildConvolutionReverb()
{
	using namespace AudioConvReverbIntrinsics;
	using FEffectWeakPtr = TWeakPtr<FSoundEffectBase, ESPMode::ThreadSafe>;
	using FEffectSharedPtr = TSharedPtr<FSoundEffectBase, ESPMode::ThreadSafe>;

	// Iterator over effects and update the convolution reverb.
	for (FEffectWeakPtr& InstanceWeakPtr : Instances)
	{
		FEffectSharedPtr InstanceSharedPtr = InstanceWeakPtr.Pin();
		if (InstanceSharedPtr.IsValid())
		{
			FSubmixEffectConvSharedPtr ConvEffectSharedPtr = StaticCastSharedPtr<FSubmixEffectConvolutionReverb>(InstanceSharedPtr);

			FVersionData VersionData = ConvEffectSharedPtr->UpdateConvolutionReverb(this);

			FConvolutionReverbInitData ConvolutionInitData = ConvEffectSharedPtr->CreateConvolutionReverbInitData();

			Audio::FConvolutionReverbSettings ReverbParams = ConvEffectSharedPtr->GetParameters();

			(new FAutoDeleteAsyncTask<FCreateConvolutionReverbTask>(ConvEffectSharedPtr, MoveTemp(ConvolutionInitData), ReverbParams, VersionData))->StartBackgroundTask();

		}
	}
}

#if WITH_EDITORONLY_DATA
void USubmixEffectConvolutionReverbPreset::BindToImpulseResponseObjectChange()
{
	if (nullptr != ImpulseResponse)
	{
		if (!DelegateHandles.Contains(ImpulseResponse))
		{
			FDelegateHandle Handle = ImpulseResponse->OnObjectPropertyChanged.AddUObject(this, &USubmixEffectConvolutionReverbPreset::PostEditChangeImpulseProperty);
			if (Handle.IsValid())
			{
				DelegateHandles.Add(ImpulseResponse, Handle);
			}
		}
	}
}

void USubmixEffectConvolutionReverbPreset::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	if (nullptr == PropertyAboutToChange)
	{
		return;
	}

	const FName ImpulseResponseFName = GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, ImpulseResponse);

	if (PropertyAboutToChange->GetFName() == ImpulseResponseFName)
	{
		if (nullptr == ImpulseResponse)
		{
			return;
		}

		if (DelegateHandles.Contains(ImpulseResponse))
		{
			FDelegateHandle DelegateHandle = DelegateHandles[ImpulseResponse];

			if (DelegateHandle.IsValid())
			{
				ImpulseResponse->OnObjectPropertyChanged.Remove(DelegateHandle);
			}

			DelegateHandles.Remove(ImpulseResponse);
		}
	}
}



void USubmixEffectConvolutionReverbPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ImpulseResponseFName = GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, ImpulseResponse);

	// If any of these properties are updated, the FConvolutionReverb object needs
	// to be rebuilt.
	const TArray<FName> ConvolutionReverbProperties(
		{
			ImpulseResponseFName,
			GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, bEnableHardwareAcceleration),
			GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, BlockSize),
			GET_MEMBER_NAME_CHECKED(FSubmixEffectConvolutionReverbSettings, bMixInputChannelFormatToImpulseResponseFormat),
			GET_MEMBER_NAME_CHECKED(FSubmixEffectConvolutionReverbSettings, bMixReverbOutputToOutputChannelFormat),
		}
	);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
		{
			const FName& Name = PropertyThatChanged->GetFName();

			// Need to update impulse reponse bindings and normalization before rebuilding convolution reverb
			if (Name == ImpulseResponseFName)
			{
				BindToImpulseResponseObjectChange();
				
				if (nullptr != ImpulseResponse)
				{
					SetImpulseResponseSettings(ImpulseResponse);
				}
			}

			if (ConvolutionReverbProperties.Contains(Name))
			{
				RebuildConvolutionReverb();
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void USubmixEffectConvolutionReverbPreset::PostEditChangeImpulseProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	const FName NormalizationVolumeFName = GET_MEMBER_NAME_CHECKED(UAudioImpulseResponse, NormalizationVolumeDb);
	const FName TrueStereoFName = GET_MEMBER_NAME_CHECKED(UAudioImpulseResponse, bTrueStereo);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
		{
			const FName& Name = PropertyThatChanged->GetFName();

			if (Name == NormalizationVolumeFName)
			{
				if (nullptr != ImpulseResponse)
				{
					Settings.NormalizationVolumeDb = ImpulseResponse->NormalizationVolumeDb;
					UpdateSettings();
				}
			}
			else if (Name == TrueStereoFName)
			{
				RebuildConvolutionReverb();
			}
		}
	}
}

#endif

void USubmixEffectConvolutionReverbPreset::SetImpulseResponseSettings(UAudioImpulseResponse* InImpulseResponse)
{
	if (nullptr != InImpulseResponse)
	{
		// Set this value, but do not call UpdateSettings(). UpdateSettings() is handled elsewhere.
		Settings.NormalizationVolumeDb = InImpulseResponse->NormalizationVolumeDb;
	}
}

void USubmixEffectConvolutionReverbPreset::UpdateDeprecatedProperties()
{
	if (Settings.SurroundRearChannelBleedAmount_DEPRECATED != 0.f)
	{
		Settings.SurroundRearChannelBleedDb = Audio::ConvertToDecibels(FMath::Abs(Settings.SurroundRearChannelBleedAmount_DEPRECATED));
		Settings.bInvertRearChannelBleedPhase = Settings.SurroundRearChannelBleedAmount_DEPRECATED < 0.f;

		Settings.SurroundRearChannelBleedAmount_DEPRECATED = 0.f;
		Modify();
	}

	if (nullptr != Settings.ImpulseResponse_DEPRECATED)
	{
		ImpulseResponse = Settings.ImpulseResponse_DEPRECATED;

		// Need to make a copy of existing samples and reinterleave 
		// The previous version had these sampels chunked by channel
		// like [[all channel 0 samples][all channel 1 samples][...][allchannel N samples]]
		//
		// They need to be in interleave format to work in this class.

		int32 NumChannels = Settings.ImpulseResponse_DEPRECATED->NumChannels;

		if (NumChannels > 1)
		{
			const TArray<float>& IRData = Settings.ImpulseResponse_DEPRECATED->IRData_DEPRECATED;
			int32 NumSamples = IRData.Num();

			if (NumSamples > 0)
			{
				ImpulseResponse->ImpulseResponse.Reset();
				ImpulseResponse->ImpulseResponse.AddUninitialized(NumSamples);

				int32 NumFrames = NumSamples / NumChannels;

				const float* InputSamples = IRData.GetData();
				float* OutputSamples = ImpulseResponse->ImpulseResponse.GetData();

				for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ChannelIndex++)
				{
					for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
					{
						OutputSamples[FrameIndex * NumChannels + ChannelIndex] = InputSamples[ChannelIndex * NumFrames + FrameIndex];
					}
				}
			}
		}
		else
		{
			// Do not need to do anything for zero or one channels.
			ImpulseResponse->ImpulseResponse = ImpulseResponse->IRData_DEPRECATED;
		}

		// Discard samples after they have been copied.
		Settings.ImpulseResponse_DEPRECATED->IRData_DEPRECATED = TArray<float>();

		Settings.ImpulseResponse_DEPRECATED = nullptr;

		Modify();
	}

	if (!Settings.AllowHardwareAcceleration_DEPRECATED)
	{
		bEnableHardwareAcceleration = false;

		Settings.AllowHardwareAcceleration_DEPRECATED = true;
		Modify();
	}
}

void USubmixEffectConvolutionReverbPreset::PostLoad()
{
	Super::PostLoad();

	// This handles previous version 
	UpdateDeprecatedProperties();

#if WITH_EDITORONLY_DATA
	// Bind to trigger new convolution algorithms when UAudioImpulseResponse object changes.
	BindToImpulseResponseObjectChange();
#endif

	SetImpulseResponseSettings(ImpulseResponse);
}
