// Copyright Epic Games, Inc. All Rights Reserved.

#include "AjaTimecodeProvider.h"
#include "AjaMediaPrivate.h"
#include "AJA.h"

#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "AjaTimecodeProvider"


//~ IAJASyncChannelCallbackInterface implementation
//--------------------------------------------------------------------
// Those are called from the AJA thread. There's a lock inside AJA to prevent this object from dying while in this thread.
struct UAjaTimecodeProvider::FAJACallback : public AJA::IAJASyncChannelCallbackInterface
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
	, SyncChannel(nullptr)
	, SyncCallback(nullptr)
#if WITH_EDITORONLY_DATA
	, InitializedEngine(nullptr)
	, LastAutoSynchronizeInEditorAppTime(0.0)
#endif
	, State(ETimecodeProviderSynchronizationState::Closed)
{
}

FQualifiedFrameTime UAjaTimecodeProvider::GetQualifiedFrameTime() const
{
	FFrameRate FrameRate = bUseReferenceIn ? ReferenceConfiguration.LtcFrameRate : VideoConfiguration.MediaConfiguration.MediaMode.FrameRate;
	if (SyncChannel)
	{
		if (State == ETimecodeProviderSynchronizationState::Synchronized)
		{
			AJA::FTimecode NewTimecode;
			if (SyncChannel->GetTimecode(NewTimecode))
			{
				//We expect the timecode to be processed in the library. What we receive will be a "linear" timecode even for frame rates greater than 30.
				FTimecode Timecode = FAja::ConvertAJATimecode2Timecode(NewTimecode, FrameRate);
				return FQualifiedFrameTime(Timecode, FrameRate);
			}
			else
			{
				const_cast<UAjaTimecodeProvider*>(this)->State = ETimecodeProviderSynchronizationState::Error;
			}
		}
	}

	return FQualifiedFrameTime(0, FrameRate);
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
	if ((bUseReferenceIn && !ReferenceConfiguration.IsValid()) || (!bUseReferenceIn && !VideoConfiguration.IsValid()))
	{
		State = ETimecodeProviderSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Warning, TEXT("The TimecodeProvider '%s' configuration is invalid."), *GetName());
		return false;
	}

	check(SyncCallback == nullptr);
	SyncCallback = new FAJACallback(this);

	const int32 DeviceIndex = bUseReferenceIn ? ReferenceConfiguration.Device.DeviceIdentifier : VideoConfiguration.MediaConfiguration.MediaConnection.Device.DeviceIdentifier;
	AJA::AJADeviceOptions DeviceOptions(DeviceIndex);

	AJA::AJASyncChannelOptions Options(*GetName());
	Options.CallbackInterface = SyncCallback;

	Options.bReadTimecodeFromReferenceIn = bUseReferenceIn;

	Options.LTCSourceIndex = ReferenceConfiguration.LtcIndex;
	Options.LTCFrameRateNumerator = ReferenceConfiguration.LtcFrameRate.Numerator;
	Options.LTCFrameRateDenominator = ReferenceConfiguration.LtcFrameRate.Denominator;

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
	switch(VideoConfiguration.TimecodeFormat)
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

	check(SyncChannel == nullptr);
	SyncChannel = new AJA::AJASyncChannel();
	if (!SyncChannel->Initialize(DeviceOptions, Options))
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

void UAjaTimecodeProvider::ReleaseResources()
{
	if (SyncChannel)
	{
		SyncChannel->Uninitialize();
		delete SyncChannel;
		SyncChannel = nullptr;

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
