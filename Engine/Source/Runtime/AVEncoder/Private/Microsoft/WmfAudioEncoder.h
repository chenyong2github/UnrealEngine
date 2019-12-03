// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVEncoder.h"
#include "AVEncoderCommon.h"

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE

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

#endif

