// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderFactorySimulcast.h"
#include "Settings.h"
#include "absl/strings/match.h"
#include "WebRTCIncludes.h"
#include "VideoEncoderAdapterSimulcast.h"
#include "VideoEncoderFactorySimple.h"

namespace UE::PixelStreaming
{
	FVideoEncoderFactorySimulcast::FVideoEncoderFactorySimulcast()
		: PrimaryEncoderFactory(MakeUnique<FVideoEncoderFactorySimple>())
	{
		// Make a copy of simulcast settings and sort them based on scaling.
		for (FEncoderFactoryId i = 0; i < Settings::SimulcastParameters.Layers.Num(); i++)
		{
			GetOrCreateEncoderFactory(i);
		}
	}

	FVideoEncoderFactorySimulcast::~FVideoEncoderFactorySimulcast()
	{
	}

	std::unique_ptr<webrtc::VideoEncoder> FVideoEncoderFactorySimulcast::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
	{
		return std::make_unique<FVideoEncoderAdapterSimulcast>(*this, format);
	}

	FVideoEncoderFactorySimple* FVideoEncoderFactorySimulcast::GetEncoderFactory(FEncoderFactoryId Id)
	{
		FScopeLock Lock(&EncoderFactoriesGuard);
		if (auto&& Existing = EncoderFactories.Find(Id))
		{
			return Existing->Get();
		}
		return nullptr;
	}

	FVideoEncoderFactorySimple* FVideoEncoderFactorySimulcast::GetOrCreateEncoderFactory(FEncoderFactoryId Id)
	{
		FVideoEncoderFactorySimple* Existing = GetEncoderFactory(Id);
		if (Existing == nullptr)
		{
			FScopeLock Lock(&EncoderFactoriesGuard);
			TUniquePtr<FVideoEncoderFactorySimple> EncoderFactory = MakeUnique<FVideoEncoderFactorySimple>();
			EncoderFactories.Add(Id, MoveTemp(EncoderFactory));
			return EncoderFactory.Get();
		}
		else
		{
			return Existing;
		}
	}

	std::vector<webrtc::SdpVideoFormat> FVideoEncoderFactorySimulcast::GetSupportedFormats() const
	{
		return PrimaryEncoderFactory->GetSupportedFormats();
	}

	FVideoEncoderFactorySimulcast::CodecInfo FVideoEncoderFactorySimulcast::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
	{
		return PrimaryEncoderFactory->QueryVideoEncoder(format);
	}
} // namespace UE::PixelStreaming
