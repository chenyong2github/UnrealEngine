// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVEncoder.h"
#include "AVEncoderCommon.h"

namespace AVEncoder
{

/**
 * Factory for Nvidia's NvEnc
 */
class FNvVideoEncoderFactory : public FVideoEncoderFactory
{
public:
	FNvVideoEncoderFactory();
	~FNvVideoEncoderFactory() override;
	const TCHAR* GetName() const override;
	TArray<FString> GetSupportedCodecs() const override;
	TUniquePtr<FVideoEncoder> CreateEncoder(const FString& Codec) override;
private:
};

}

