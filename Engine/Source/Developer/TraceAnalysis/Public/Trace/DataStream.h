// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace Trace
{

class IInDataStream
{
public:
	virtual			~IInDataStream() = default;
	virtual int32	Read(void* Data, uint32 Size) = 0;
	virtual void	Close() {}
};

}
