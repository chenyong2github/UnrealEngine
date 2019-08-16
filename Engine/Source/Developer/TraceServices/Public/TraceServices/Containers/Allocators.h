// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{
	
class ILinearAllocator
{
public:
	virtual ~ILinearAllocator() = default;
	virtual void* Allocate(uint64 Size) = 0;
};

}