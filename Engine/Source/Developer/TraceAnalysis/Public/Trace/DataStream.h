// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace Trace
{

class IInDataStream
{
public:
	virtual			~IInDataStream() = default;
	virtual int32	Read(void* Data, uint32 Size) = 0;
};

class IOutDataStream
{
public:
	virtual			~IOutDataStream() = default;
	virtual bool	Write(const void* Data, uint32 Size) = 0;
};

TRACEANALYSIS_API IInDataStream* DataStream_ReadFile(const TCHAR* FilePath);

}
