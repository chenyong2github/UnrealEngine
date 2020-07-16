// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmixEffects/SubmixEffectConvolutionReverb.h"

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "ConvolutionReverb.h"
#include "DSP/ConvolutionAlgorithm.h"
#include "DSP/SampleRateConverter.h"
#include "SynthesisModule.h"

namespace AudioConvReverbIntrinsics
{
	// names of various smart pointers
	using FSubmixEffectConvSharedPtr = TSharedPtr<FSubmixEffectConvolutionReverb, ESPMode::ThreadSafe>;
	using FSubmixEffectConvWeakPtr = TWeakPtr<FSubmixEffectConvolutionReverb, ESPMode::ThreadSafe>;
	using FConvAlgoUniquePtr = TUniquePtr<Audio::IConvolutionAlgorithm>;

	// names of nested classes and structs
	using FConvolutionAlgorithmInitData = FSubmixEffectConvolutionReverb::FConvolutionAlgorithmInitData;
	using FConvolutionGainEntry = FSubmixEffectConvolutionReverb::FConvolutionGainEntry;
	using FVersionData = FSubmixEffectConvolutionReverb::FVersionData;


	// Create a convolution algorithm. This performs creation of the convolution algorithm object,
	// converting sample rates of impulse responses, sets the impulse response and initializes the
	// gain matrix of the convolution algorithm.
	//
	// @params InInitData - Contains all the information needed to create a convolution algorithm.
	//
	// @return TUniquePtr<Audio::IConvolutionAlgorithm>  Will be invalid if there was an error.
	static FConvAlgoUniquePtr CreateConvolutionAlgorithm(const FConvolutionAlgorithmInitData& InInitData)
	{
		// Check valid sample rates
		if ((InInitData.ImpulseSampleRate <= 0.f) || (InInitData.TargetSampleRate <= 0.f))
		{
			return FConvAlgoUniquePtr();
		}

		const Audio::FConvolutionSettings& AlgoSettings = InInitData.AlgorithmSettings;

		// Check valid channel counts
		if ((AlgoSettings.NumInputChannels < 1) || (AlgoSettings.NumOutputChannels < 1) || (AlgoSettings.NumImpulseResponses < 1))
		{
			return FConvAlgoUniquePtr();
		}

		// Create convolution algorithm
		FConvAlgoUniquePtr ConvolutionAlgorithm = Audio::FConvolutionFactory::NewConvolutionAlgorithm(AlgoSettings);

		if (!ConvolutionAlgorithm.IsValid())
		{
			UE_LOG(LogSynthesis, Warning, TEXT("Failed to greate convolution algorithm for convolution reverb"));
			return ConvolutionAlgorithm;
		}


		const TArray<float>* TargetImpulseSamples = &InInitData.Samples;

		TArray<float> ResampledImpulseSamples;
		// Prepare impulse samples by converting samplerate and deinterleaving.
		if (InInitData.ImpulseSampleRate != InInitData.TargetSampleRate)
		{
			// convert sample rate of impulse 
			float SampleRateRatio = InInitData.ImpulseSampleRate / InInitData.TargetSampleRate;

			TUniquePtr<Audio::ISampleRateConverter> Converter(Audio::ISampleRateConverter::CreateSampleRateConverter());

			if (!Converter.IsValid())
			{
				UE_LOG(LogSynthesis, Error, TEXT("Audio::ISampleRateConverter failed to create a sample rate converter"));
				return FConvAlgoUniquePtr();
			}

			Converter->Init(SampleRateRatio, AlgoSettings.NumImpulseResponses);
			Converter->ProcessFullbuffer(InInitData.Samples.GetData(), InInitData.Samples.Num(), ResampledImpulseSamples);

			TargetImpulseSamples = &ResampledImpulseSamples;
		}

		const int32 NumFrames = TargetImpulseSamples->Num() / AlgoSettings.NumImpulseResponses;

		// Prepare deinterleave pointers
		TArray<Audio::AlignedFloatBuffer> DeinterleaveSamples;
		while (DeinterleaveSamples.Num() < AlgoSettings.NumImpulseResponses)
		{
			Audio::AlignedFloatBuffer& Buffer = DeinterleaveSamples.Emplace_GetRef();
			if (NumFrames > 0)
			{
				Buffer.AddUninitialized(NumFrames);
			}
		}

		// Deinterleave impulse samples
		Audio::FConvolutionReverb::DeinterleaveBuffer(DeinterleaveSamples, *TargetImpulseSamples, AlgoSettings.NumImpulseResponses);

		// Set impulse responses in algorithm
		for (int32 i = 0; i < DeinterleaveSamples.Num(); i++)
		{
			const Audio::AlignedFloatBuffer& Buffer = DeinterleaveSamples[i];

			ConvolutionAlgorithm->SetImpulseResponse(i, Buffer.GetData(), Buffer.Num());
		}

		// Setup gain matrix in algorithm
		for (const FConvolutionGainEntry& Entry : InInitData.Gains)
		{
			// If an entry exceeds the index, ignore. Will not log a warning
			// or error as this might be a common occurence when channel counts
			// change.
			if (Entry.InputIndex >= ConvolutionAlgorithm->GetNumAudioInputs())
			{
				continue;
			}

			if (Entry.ImpulseIndex >= ConvolutionAlgorithm->GetNumImpulseResponses())
			{
				continue;
			}

			if (Entry.OutputIndex >= ConvolutionAlgorithm->GetNumAudioOutputs())
			{
				continue;
			}

			ConvolutionAlgorithm->SetMatrixGain(Entry.InputIndex, Entry.ImpulseIndex, Entry.OutputIndex, Entry.Gain);
		}
		
		return ConvolutionAlgorithm;
	}

	// Task for creating convolution algorithm object.
	class FCreateConvolutionAlgorithmTask : public FNonAbandonableTask
	{
		// This task can delete itself
		friend class FAutoDeleteAsyncTask<FCreateConvolutionAlgorithmTask>;

		public:
			FCreateConvolutionAlgorithmTask(
					FSubmixEffectConvWeakPtr InEffectObject,
					FConvolutionAlgorithmInitData&& InInitData,
					const FVersionData& InVersionData)
			:	EffectWeakPtr(InEffectObject)
			,	InitData(MoveTemp(InInitData))
			,	VersionData(InVersionData)
			{}

			void DoWork()
			{
				// Build the convolution algorithm object. 
				FConvAlgoUniquePtr ConvolutionAlgorithm = CreateConvolutionAlgorithm(InitData);

				FSubmixEffectConvSharedPtr EffectSharedPtr = EffectWeakPtr.Pin();

				// Check that the submix effect still exists.
				if (EffectSharedPtr.IsValid())
				{
					// If the effect ptr is still valid, add to it's command queue to set the convolution
					// algorithm object in the audio render thread.

					TUniqueFunction<void()> Command = [Algo = MoveTemp(ConvolutionAlgorithm), EffectSharedPtr, VersionData = VersionData] () mutable
					{
						EffectSharedPtr->SetConvolutionAlgorithmIfCurrent(MoveTemp(Algo), VersionData);
					};
					EffectSharedPtr->EffectCommand(MoveTemp(Command));
				}
			}

			FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(CreateConvolutionAlgorithmTask,     STATGROUP_ThreadPoolAsyncTasks); }

		private:
			FSubmixEffectConvWeakPtr EffectWeakPtr;
			FConvolutionAlgorithmInitData InitData;
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

FSubmixEffectConvolutionReverb::FIRAssetData::FIRAssetData()
:	NumChannels(0)
,	SampleRate(0.f)
,	bEnableHardwareAcceleration(false)
{
}

FSubmixEffectConvolutionReverb::FSubmixEffectConvolutionReverb(const USubmixEffectConvolutionReverbPreset* InPreset)
: 	ConvolutionReverb(TUniquePtr<Audio::IConvolutionAlgorithm>(nullptr))
,	SampleRate(0.0f)
,	NumInputChannels(2)
,	NumOutputChannels(2)
{
	UpdateConvolutionAlgorithm(InPreset);
}

FSubmixEffectConvolutionReverb::~FSubmixEffectConvolutionReverb()
{
}

void FSubmixEffectConvolutionReverb::Init(const FSoundEffectSubmixInitData& InitData)
{
	using namespace AudioConvReverbIntrinsics;

	SampleRate = InitData.SampleRate;

	FVersionData UpdatedVersionData = UpdateVersion();

	// Create the convolution algorithm init data
	FConvolutionAlgorithmInitData ConvolutionInitData = GetConvolutionAlgorithmInitData();

	// Create the convolution algorithm
	FConvAlgoUniquePtr ConvolutionAlgo = CreateConvolutionAlgorithm(MoveTemp(ConvolutionInitData));

	// Set the convolution algorithm, ensuring that it's the most up-to-date
	SetConvolutionAlgorithmIfCurrent(MoveTemp(ConvolutionAlgo), UpdatedVersionData);
}


void FSubmixEffectConvolutionReverb::OnPresetChanged()
{
	USubmixEffectConvolutionReverbPreset* ConvolutionPreset = CastChecked<USubmixEffectConvolutionReverbPreset>(Preset);
	FSubmixEffectConvolutionReverbSettings Settings = ConvolutionPreset->GetSettings();;

	Audio::FConvolutionReverbSettings ReverbSettings;


	float NewVolume = Audio::ConvertToLinear(Settings.NormalizationVolumeDb);
	float NewRearChannelBleed = Audio::ConvertToLinear(Settings.SurroundRearChannelBleedDb);

	if (Settings.bInvertRearChannelBleedPhase)
	{
		NewRearChannelBleed *= -1.f;
	}

	ReverbSettings.NormalizationVolume = NewVolume;
	ReverbSettings.RearChannelBleed = NewRearChannelBleed;
	ReverbSettings.bRearChannelFlip = Settings.bSurroundRearChannelFlip;

	Params.SetParams(ReverbSettings);
}


FSubmixEffectConvolutionReverb::FConvolutionAlgorithmInitData FSubmixEffectConvolutionReverb::GetConvolutionAlgorithmInitData() const
{
	FScopeLock IRAssetLock(&IRAssetCriticalSection);

	int32 ConvNumInputChannels = NumInputChannels.Load();
	int32 ConvNumOutputChannels = NumOutputChannels.Load();

	// Create convolution algorithm settings.
	FConvolutionAlgorithmInitData CreateConvolutionSettings;

	// IConvolutionAlgorithm settings
	CreateConvolutionSettings.AlgorithmSettings.bEnableHardwareAcceleration = IRAssetData.bEnableHardwareAcceleration;
	CreateConvolutionSettings.AlgorithmSettings.BlockNumSamples = IRAssetData.BlockSize;
	CreateConvolutionSettings.AlgorithmSettings.NumInputChannels = ConvNumInputChannels;
	CreateConvolutionSettings.AlgorithmSettings.NumOutputChannels = ConvNumOutputChannels;
	CreateConvolutionSettings.AlgorithmSettings.NumImpulseResponses = IRAssetData.NumChannels;
	CreateConvolutionSettings.AlgorithmSettings.MaxNumImpulseResponseSamples = 0;

	if (IRAssetData.NumChannels > 0)
	{
		float SampleRateRatio = 1.f;
		if (IRAssetData.SampleRate > 0.f)
		{
			SampleRateRatio = SampleRate / IRAssetData.SampleRate;
		}
		CreateConvolutionSettings.AlgorithmSettings.MaxNumImpulseResponseSamples = FMath::CeilToInt(SampleRateRatio * IRAssetData.Samples.Num() / IRAssetData.NumChannels) + 256;
	}

	// Sample rate conversion settings
	CreateConvolutionSettings.ImpulseSampleRate = IRAssetData.SampleRate;
	CreateConvolutionSettings.TargetSampleRate = SampleRate;

	// Setup gain matrix. Currently we are only setting up a 1-to-1 mapping. But this 
	// could be enhanced by adding info to the IR asset or Preset to express different
	// desired mappings.
	int32 MinChannelCount = FMath::Min3(ConvNumInputChannels, ConvNumOutputChannels, IRAssetData.NumChannels);
	for (int32 i = 0; i < MinChannelCount; i++)
	{
		CreateConvolutionSettings.Gains.Emplace(i, i, i, 1.f);
	}

	CreateConvolutionSettings.Samples = IRAssetData.Samples;

	return CreateConvolutionSettings;
}


void FSubmixEffectConvolutionReverb::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	check(nullptr != InData.AudioBuffer);
	check(nullptr != OutData.AudioBuffer);

	UpdateParameters();

	int32 ExpectedNumInputChannels = ConvolutionReverb.GetNumInputChannels();
	int32 ExpectedNumOutputChannels = ConvolutionReverb.GetNumOutputChannels();

	// Check if there is a channel mismatch between teh convolution reverb and the input/output data
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
			using namespace AudioConvReverbIntrinsics;

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

			// I would really rather this be done in a task. To do so, we would need 
			// access to a weak ptr to "this". Currently that is not possible since this object
			// is owned by the Preset, and the TWeakObjectPtr cannot safely be copied across thread
			// boundaries. 
			{
				// Create the convolution algorithm init data
				FConvolutionAlgorithmInitData ConvolutionInitData = GetConvolutionAlgorithmInitData();

				// Create the convolution algorithm
				FConvAlgoUniquePtr ConvolutionAlgo = CreateConvolutionAlgorithm(MoveTemp(ConvolutionInitData));

				// Set the convolution algorithm, ensuring that it's the most up-to-date
				SetConvolutionAlgorithmIfCurrent(MoveTemp(ConvolutionAlgo), CurrentVersionData);
			}

			ExpectedNumInputChannels = ConvolutionReverb.GetNumInputChannels();
			ExpectedNumOutputChannels = ConvolutionReverb.GetNumOutputChannels();
		}
	}


	if ((ExpectedNumInputChannels == InData.NumChannels) && (ExpectedNumOutputChannels == OutData.NumChannels))
	{
		ConvolutionReverb.ProcessAudio(InData.NumChannels, *InData.AudioBuffer, OutData.NumChannels, *OutData.AudioBuffer);
	}
	else
	{
		// Channel mismatch. zero output data. Do *not* trigger rebuild here in case one is already in flight or simply because one cannot be built.
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
    Audio::FConvolutionReverbSettings NewSettings;
    if (Params.GetParams(&NewSettings))
    {
        ConvolutionReverb.SetSettings(NewSettings);
    }
}


uint32 FSubmixEffectConvolutionReverb::GetDesiredInputChannelCountOverride() const
{
	return 2;
}

FSubmixEffectConvolutionReverb::FVersionData FSubmixEffectConvolutionReverb::UpdateConvolutionAlgorithm(const USubmixEffectConvolutionReverbPreset* InPreset)
{
	// Copy data from preset into internal IRAssetData.
	
	check(IsInGameThread() || IsInAudioThread());

	FScopeLock IRAssetLock(&IRAssetCriticalSection);

	IRAssetData.Samples.Reset();
	IRAssetData.NumChannels = 0;
	IRAssetData.SampleRate = 0.f;
	IRAssetData.bEnableHardwareAcceleration = false;
	IRAssetData.BlockSize = 1024;

	if (nullptr != InPreset)
	{
		if (InPreset->ImpulseResponse)
		{
			UAudioImpulseResponse* IR = InPreset->ImpulseResponse;

			IRAssetData.Samples = IR->ImpulseResponse;
			IRAssetData.NumChannels = IR->NumChannels;
			IRAssetData.SampleRate = IR->SampleRate;
		}
		switch (InPreset->BlockSize)
		{
			case ESubmixEffectConvolutionReverbBlockSize::BlockSize256:
				IRAssetData.BlockSize = 256;
				break;

			case ESubmixEffectConvolutionReverbBlockSize::BlockSize512:
				IRAssetData.BlockSize = 512;
				break;

			case ESubmixEffectConvolutionReverbBlockSize::BlockSize1024:
			default:
				IRAssetData.BlockSize = 1024;
				break;
		}
		IRAssetData.bEnableHardwareAcceleration = InPreset->bEnableHardwareAcceleration;
	}

	return UpdateVersion();
}

void FSubmixEffectConvolutionReverb::SetConvolutionAlgorithmIfCurrent(TUniquePtr<Audio::IConvolutionAlgorithm> InAlgo, const FVersionData& InVersionData)
{
	bool bIsCurrent = true;

	{
		FScopeLock VersionDataLock(&VersionDataCriticalSection);

		bIsCurrent &= (InVersionData == VersionData);
	}

	if (bIsCurrent)
	{
		ConvolutionReverb.SetConvolutionAlgorithm(MoveTemp(InAlgo));
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
	UpdateSettings();
}

void USubmixEffectConvolutionReverbPreset::SetImpulseResponse(UAudioImpulseResponse* InImpulseResponse)
{
	ImpulseResponse = InImpulseResponse;
	UpdateEffectConvolutionAlgorithm();
}

void USubmixEffectConvolutionReverbPreset::UpdateEffectConvolutionAlgorithm()
{
	using namespace AudioConvReverbIntrinsics;
	using FEffectWeakPtr = TWeakPtr<FSoundEffectBase, ESPMode::ThreadSafe>;
	using FEffectSharedPtr = TSharedPtr<FSoundEffectBase, ESPMode::ThreadSafe>;

	// Iterator over effects and set IR data.
	for (FEffectWeakPtr& InstanceWeakPtr : Instances)
	{
		FEffectSharedPtr InstanceSharedPtr = InstanceWeakPtr.Pin();
		if (InstanceSharedPtr.IsValid())
		{
			FSubmixEffectConvSharedPtr ConvEffectSharedPtr = StaticCastSharedPtr<FSubmixEffectConvolutionReverb>(InstanceSharedPtr);

			FVersionData VersionData = ConvEffectSharedPtr->UpdateConvolutionAlgorithm(this);

			FConvolutionAlgorithmInitData ConvolutionInitData = ConvEffectSharedPtr->GetConvolutionAlgorithmInitData();

			(new FAutoDeleteAsyncTask<FCreateConvolutionAlgorithmTask>(ConvEffectSharedPtr, MoveTemp(ConvolutionInitData), VersionData))->StartBackgroundTask();

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
		}
	}
}



void USubmixEffectConvolutionReverbPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ImpulseResponseFName = GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, ImpulseResponse);
	const TArray<FName> ConvolutionAlgorithmProperties(
		{
			ImpulseResponseFName,
			GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, bEnableHardwareAcceleration),
			GET_MEMBER_NAME_CHECKED(USubmixEffectConvolutionReverbPreset, BlockSize)
		}
	);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
		{
			const FName& Name = PropertyThatChanged->GetFName();

			if (ConvolutionAlgorithmProperties.Contains(Name))
			{
				UpdateEffectConvolutionAlgorithm();
			}

			if (Name == ImpulseResponseFName)
			{
				BindToImpulseResponseObjectChange();

				if (nullptr != ImpulseResponse)
				{
					Settings.NormalizationVolumeDb = ImpulseResponse->NormalizationVolumeDb;
					UpdateSettings();
				}
			}
		}
	}
}

void USubmixEffectConvolutionReverbPreset::PostEditChangeImpulseProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	const FName NormalizationVolumeFName = GET_MEMBER_NAME_CHECKED(UAudioImpulseResponse, NormalizationVolumeDb);

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
		}
	}
}

#endif

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
}
