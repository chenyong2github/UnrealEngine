// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AVEncoder.h"

#if PLATFORM_WINDOWS
	#include "Microsoft/Windows/NvVideoEncoder.h"
	#include "Microsoft/Windows/AmfVideoEncoder.h"
	#include "Microsoft/WmfAudioEncoder.h"
#elif PLATFORM_XBOXONE
	#include "Microsoft/WmfAudioEncoder.h"
	#include "Microsoft/XBoxOne/XboxOneVideoEncoder.h"
#endif

namespace AVEncoder
{

bool GDefaultFactoriesRegistered = false;
void RegisterDefaultFactories();

void DoDefaultRegistration()
{
	if (GDefaultFactoriesRegistered)
		return;
	RegisterDefaultFactories();
}

//////////////////////////////////////////////////////////////////////////
// FVideoEncoder
//////////////////////////////////////////////////////////////////////////

void FVideoEncoder::RegisterListener(IVideoEncoderListener& Listener)
{
	FScopeLock lock{ &ListenersMutex };
	check(Listeners.Find(&Listener)==INDEX_NONE);
	Listeners.AddUnique(&Listener);
}

void FVideoEncoder::UnregisterListener(IVideoEncoderListener& Listener)
{
	FScopeLock lock{ &ListenersMutex };
	int32 Count = Listeners.Remove(&Listener);
	check(Count == 1);
}

void FVideoEncoder::OnEncodedVideoFrame(const FAVPacket& Packet, TUniquePtr<FEncoderVideoFrameCookie> Cookie)
{
	FScopeLock lock{ &ListenersMutex };
	for (auto&& L : Listeners)
	{
		L->OnEncodedVideoFrame(Packet, Cookie.Get());
	}
}

//////////////////////////////////////////////////////////////////////////
// FAudioEncoder
//////////////////////////////////////////////////////////////////////////

void FAudioEncoder::RegisterListener(IAudioEncoderListener& Listener)
{
	FScopeLock lock{ &ListenersMutex };
	check(Listeners.Find(&Listener)==INDEX_NONE);
	Listeners.AddUnique(&Listener);
}

void FAudioEncoder::UnregisterListener(IAudioEncoderListener& Listener)
{
	FScopeLock lock{ &ListenersMutex };
	int32 Count = Listeners.Remove(&Listener);
	check(Count == 1);
}

void FAudioEncoder::OnEncodedAudioFrame(const FAVPacket& Packet)
{
	FScopeLock lock{ &ListenersMutex };
	for (auto&& L : Listeners)
	{
		L->OnEncodedAudioFrame(Packet);
	}
}

//////////////////////////////////////////////////////////////////////////
// FVideoEncoderFactory
//////////////////////////////////////////////////////////////////////////

TArray<FVideoEncoderFactory*> FVideoEncoderFactory::Factories;

void FVideoEncoderFactory::RegisterFactory(FVideoEncoderFactory& Factory)
{
	DoDefaultRegistration();

	Factories.AddUnique(&Factory);
}

void FVideoEncoderFactory::UnregisterFactory(FVideoEncoderFactory& Factory)
{
	Factories.Remove(&Factory);
}

FVideoEncoderFactory* FVideoEncoderFactory::FindFactory(const FString& Codec)
{
	DoDefaultRegistration();

	for (auto&& Factory : Factories)
	{
		if (Factory->GetSupportedCodecs().Find(Codec) != INDEX_NONE)
		{
			return Factory;
		}
	}

	return nullptr;
}

const TArray<FVideoEncoderFactory*> FVideoEncoderFactory::GetAllFactories()
{
	DoDefaultRegistration();

	return Factories;
}

//////////////////////////////////////////////////////////////////////////
// FAudioEncoderFactory
//////////////////////////////////////////////////////////////////////////

TArray<FAudioEncoderFactory*> FAudioEncoderFactory::Factories;

void FAudioEncoderFactory::RegisterFactory(FAudioEncoderFactory& Factory)
{
	DoDefaultRegistration();

	Factories.AddUnique(&Factory);
}

void FAudioEncoderFactory::UnregisterFactory(FAudioEncoderFactory& Factory)
{
	Factories.Remove(&Factory);
}

FAudioEncoderFactory* FAudioEncoderFactory::FindFactory(const FString& Codec)
{
	DoDefaultRegistration();

	for (auto&& Factory : Factories)
	{
		if (Factory->GetSupportedCodecs().Find(Codec) != INDEX_NONE)
		{
			return Factory;
		}
	}

	return nullptr;
}

const TArray<FAudioEncoderFactory*> FAudioEncoderFactory::GetAllFactories()
{
	DoDefaultRegistration();

	return Factories;
}

void RegisterDefaultFactories()
{
	// We need to set this at the top, otherwise RegisterFactory will call this recursively
	GDefaultFactoriesRegistered = true;

#if PLATFORM_WINDOWS
	// Nvidia NvEnc
	static FNvVideoEncoderFactory NvVideoEncoderFactory;
	FVideoEncoderFactory::RegisterFactory(NvVideoEncoderFactory);

	// AMD Amf
	static FAmfVideoEncoderFactory AmfVideoEncoderFactory;
	FAmfVideoEncoderFactory::RegisterFactory(AmfVideoEncoderFactory);

#else PLATFORM_XBOXONE

	static FXboxOneVideoEncoderFactory XboxOneVideoEncoderFactory;
	FVideoEncoderFactory::RegisterFactory(XboxOneVideoEncoderFactory);

#endif

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	// Generic Windows/XBox Wmf encoder
	static FWmfAudioEncoderFactory WmfAudioEncoderFactory;
	FAudioEncoderFactory::RegisterFactory(WmfAudioEncoderFactory);
#endif

	// Log all available encoders
	{
		auto CodecsInfo = [&](auto&& Factories) -> FString
		{
			FString Str;
			for (auto&& Factory : Factories)
			{
				Str += FString::Printf(TEXT(", %s(%s) "), Factory->GetName(), *FString::Join(Factory->GetSupportedCodecs(), TEXT("/")));
			}
			if (Str.IsEmpty())
			{
				return FString(TEXT("None"));
			}
			else
			{
				return Str;
			}
		};

		UE_LOG(LogAVEncoder, Log, TEXT("Available video encoders: %s "), *CodecsInfo(FVideoEncoderFactory::GetAllFactories()));
		UE_LOG(LogAVEncoder, Log, TEXT("Available audio encoders: %s "), *CodecsInfo(FAudioEncoderFactory::GetAllFactories()));
	}
}

} // namespace AVEncoder

