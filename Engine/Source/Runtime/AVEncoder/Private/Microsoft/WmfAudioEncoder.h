// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVEncoder.h"
#include "AVEncoderCommon.h"

#if AVENCODER_SUPPORTED_MICROSOFT_PLATFORM

namespace AVEncoder
{

class FWmfAudioEncoderFactory : public FAudioEncoderFactory
{
public:
	FWmfAudioEncoderFactory();
	~FWmfAudioEncoderFactory() override;
	const TCHAR* GetName() const override;
	TArray<FString> GetSupportedCodecs() const override;
	TUniquePtr<FAudioEncoder> CreateEncoder(const FString& Codec) override;
private:
};

}

#endif //AVENCODER_SUPPORTED_MICROSOFT_PLATFORM

