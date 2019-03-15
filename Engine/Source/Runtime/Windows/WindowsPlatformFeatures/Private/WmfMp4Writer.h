// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WmfPrivate.h"

#include "Math/NumericLimits.h"
#include "GameplayMediaEncoderSample.h"

class FWmfMp4Writer final
{
public:
	bool Initialize(const TCHAR* Filename);
	bool CreateStream(IMFMediaType* StreamType, DWORD& StreamIndex);
	bool Start();
	bool Write(const FGameplayMediaEncoderSample& Sample);
	bool Finalize();

private:
	TRefCountPtr<IMFSinkWriter> Writer;
};

