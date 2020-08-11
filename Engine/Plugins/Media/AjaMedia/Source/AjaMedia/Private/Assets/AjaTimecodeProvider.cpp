// Copyright Epic Games, Inc. All Rights Reserved.

#include "AjaTimecodeProvider.h"
#include "AjaMediaPrivate.h"
#include "AJA.h"

#include "Misc/App.h"
#include "UObject/VirtualProductionObjectVersion.h"

#define LOCTEXT_NAMESPACE "AjaTimecodeProvider"


//~ IAJASyncChannelCallbackInterface implementation
//--------------------------------------------------------------------
// Those are called from the AJA thread. There's a lock inside AJA to prevent this object from dying while in this thread.
struct UAjaTimecodeProvider::FAJACallback : public AJA::IAJATimecodeChannelCallbackInterface
{
	UAjaTimecodeProvider* Owner;
	FAJACallback(UAjaTimecodeProvider* InOwner)
		: Owner(InOwner)
	{}

	//~ IAJAInputCallbackInterface interface
	virtual void OnInitializationCompleted(bool bSucceed) override
	{
		Owner->State = bSucceed ? ETimecodeProviderSynchronizationState::Synchronized : ETimecodeProviderSynchronizationState::Error;
		if (!bSucceed)
		{
			UE_LOG(LogAjaMedia, Error, TEXT("The initialization of '%s' failed. The TimecodeProvider won't be synchronized."), *Owner->GetName());
		}
	}
};

//~ UAjaTimecodeProvider implementation
//--------------------------------------------------------------------
UAjaTimecodeProvider::UAjaTimecodeProvider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TimecodeChannel(nullptr)
	, SyncCallback(nullptr)
#if WITH_EDITORONLY_DATA
	, InitializedEngine(nullptr)
	, LastAutoSynchronizeInEditorAppTime(0.0)
#endif
	, State(ETimecodeProviderSynchronizationState::Closed)
{
}

bool UAjaTimecodeProvider::FetchTimecode(FQualifiedFrameTime& OutFrameTime)
{
	if (!TimecodeChannel || (State != ETimecodeProviderSynchronizationState::Synchronized))
	{
		return false;
	}

	AJA::FTimecode NewTimecode;

	if (!TimecodeChannel->GetTimecode(NewTimecode))
	{
		State = ETimecodeProviderSynchronizationState::Error;
		return false;
	}

	// We expect the timecode to be processed in the library. 
	// What we receive will be a "linear" timecode even for frame rates greater than 30.

	FFrameRate FrameRate = bUseDedicatedPin ? LTCConfiguration.LtcFrameRate : VideoConfiguration.MediaConfiguration.MediaMode.FrameRate;
	FTimecode Timecode = FAja::ConvertAJATimecode2Timecode(NewTimecode, FrameRate);

	OutFrameTime = FQualifiedFrameTime(Timecode, FrameRate);

	return true;
}

bool UAjaTimecodeProvider::Initialize(class UEngine* InEngine)
{
#if WITH_EDITORONLY_DATA
	InitializedEngine = nullptr;
#endif

	State = ETimecodeProviderSynchronizationState::Closed;

	if (!FAja::IsInitialized())
	{
		State = ETimecodeProviderSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Warning, TEXT("The TimecodeProvider '%s' can't be initialized. AJA is not initialized on your machine."), *GetName());
		return false;
	}

	if (!FAja::CanUseAJACard())
	{
		State = ETimecodeProviderSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Warning, TEXT("The TimecodeProvider '%s' can't be initialized because Aja card cannot be used. Are you in a Commandlet? You may override this behavior by launching with -ForceAjaUsage"), *GetName());
		return false;
	}

	FString FailureReason;
	if ((bUseDedicatedPin && !LTCConfiguration.IsValid()) || (!bUseDedicatedPin && !VideoConfiguration.IsValid()))
	{
		State = ETimecodeProviderSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Warning, TEXT("The TimecodeProvider '%s' configuration is invalid."), *GetName());
		return false;
	}

	check(SyncCallback == nullptr);
	SyncCallback = new FAJACallback(this);

	const int32 DeviceIndex = bUseDedicatedPin ? LTCConfiguration.Device.DeviceIdentifier : VideoConfiguration.MediaConfiguration.MediaConnection.Device.DeviceIdentifier;
	AJA::AJADeviceOptions DeviceOptions(DeviceIndex);

	AJA::AJATimecodeChannelOptions Options(*GetName());
	Options.CallbackInterface = SyncCallback;
	Options.bUseDedicatedPin = bUseDedicatedPin;

	if (bUseDedicatedPin)
	{
		Options.bReadTimecodeFromReferenceIn = bUseReferenceIn;
		Options.LTCSourceIndex = LTCConfiguration.LtcIndex;
		Options.LTCFrameRateNumerator = LTCConfiguration.LtcFrameRate.Numerator;
		Options.LTCFrameRateDenominator = LTCConfiguration.LtcFrameRate.Denominator;
	}
	else
	{
		Options.bReadTimecodeFromReferenceIn = false;
		Options.LTCSourceIndex = 0;

		Options.ChannelIndex = VideoConfiguration.MediaConfiguration.MediaConnection.PortIdentifier;
		Options.VideoFormatIndex = VideoConfiguration.MediaConfiguration.MediaMode.DeviceModeIdentifier;

		Options.TransportType = AJA::ETransportType::TT_SdiSingle;
		{
			const EMediaIOTransportType TransportType = VideoConfiguration.MediaConfiguration.MediaConnection.TransportType;
			const EMediaIOQuadLinkTransportType QuadTransportType = VideoConfiguration.MediaConfiguration.MediaConnection.QuadTransportType;
			switch (TransportType)
			{
			case EMediaIOTransportType::SingleLink:
				Options.TransportType = AJA::ETransportType::TT_SdiSingle;
				break;
			case EMediaIOTransportType::DualLink:
				Options.TransportType = AJA::ETransportType::TT_SdiDual;
				break;
			case EMediaIOTransportType::QuadLink:
				Options.TransportType = QuadTransportType == EMediaIOQuadLinkTransportType::SquareDivision ? AJA::ETransportType::TT_SdiQuadSQ : AJA::ETransportType::TT_SdiQuadTSI;
				break;
			case EMediaIOTransportType::HDMI:
				Options.TransportType = AJA::ETransportType::TT_Hdmi;
				break;
			}
		}

		Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_None;
		switch (VideoConfiguration.TimecodeFormat)
		{
		case EMediaIOTimecodeFormat::None:
			Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_None;
			break;
		case EMediaIOTimecodeFormat::LTC:
			Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_LTC;
			break;
		case EMediaIOTimecodeFormat::VITC:
			Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_VITC1;
			break;
		default:
			break;
		}
	}

	check(TimecodeChannel == nullptr);
	TimecodeChannel = new AJA::AJATimecodeChannel();

	if (!TimecodeChannel->Initialize(DeviceOptions, Options))
	{
		State = ETimecodeProviderSynchronizationState::Error;
		ReleaseResources();
		return false;
	}

#if WITH_EDITORONLY_DATA
	InitializedEngine = InEngine;
#endif

	State = ETimecodeProviderSynchronizationState::Synchronizing;
	return true;
}

void UAjaTimecodeProvider::Shutdown(class UEngine* InEngine)
{
#if WITH_EDITORONLY_DATA
	InitializedEngine = nullptr;
#endif

	State = ETimecodeProviderSynchronizationState::Closed;
	ReleaseResources();
}

void UAjaTimecodeProvider::BeginDestroy()
{
	ReleaseResources();
	Super::BeginDestroy();
}

void UAjaTimecodeProvider::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FVirtualProductionObjectVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FVirtualProductionObjectVersion::GUID) < FVirtualProductionObjectVersion::SupportAjaLTCPin)
	{
		//When loading an old object, we need to fix up the dedicated pin boolean if reference was checked
		bUseDedicatedPin = bUseReferenceIn;
	}
}

void UAjaTimecodeProvider::ReleaseResources()
{
	if (TimecodeChannel)
	{
		TimecodeChannel->Uninitialize();
		delete TimecodeChannel;
		TimecodeChannel = nullptr;

		check(SyncCallback);
		delete SyncCallback;
		SyncCallback = nullptr;
	}
}

ETickableTickType UAjaTimecodeProvider::GetTickableTickType() const
{
#if WITH_EDITORONLY_DATA && WITH_EDITOR
	return ETickableTickType::Conditional;
#endif
	return ETickableTickType::Never;
}

bool UAjaTimecodeProvider::IsTickable() const
{
	return State == ETimecodeProviderSynchronizationState::Error;
}

void UAjaTimecodeProvider::Tick(float DeltaTime)
{
#if WITH_EDITORONLY_DATA && WITH_EDITOR
	if (State == ETimecodeProviderSynchronizationState::Error)
	{
		ReleaseResources();

		// In Editor only, when not in pie, reinitialized the device
		if (InitializedEngine && !GIsPlayInEditorWorld && GIsEditor)
		{
			const double TimeBetweenAttempt = 1.0;
			if (FApp::GetCurrentTime() - LastAutoSynchronizeInEditorAppTime > TimeBetweenAttempt)
			{
				Initialize(InitializedEngine);
				LastAutoSynchronizeInEditorAppTime = FApp::GetCurrentTime();
			}
		}
	}
#endif
}

#undef LOCTEXT_NAMESPACE
