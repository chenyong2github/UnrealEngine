// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVEncoder.h"
#include "AVEncoderCommon.h"

namespace AVEncoder
{

/**
 * Factory for AMD's Amf
 */
class FAmfVideoEncoderFactory : public FVideoEncoderFactory
{
public:
	FAmfVideoEncoderFactory();
	~FAmfVideoEncoderFactory() override;
	const TCHAR* GetName() const override;
	TArray<FString> GetSupportedCodecs() const override;
	TUniquePtr<FVideoEncoder> CreateEncoder(const FString& Codec) override;
private:
};

}

