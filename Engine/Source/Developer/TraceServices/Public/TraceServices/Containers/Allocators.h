// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace TraceServices
{
	
class ILinearAllocator
{
public:
	virtual ~ILinearAllocator() = default;
	virtual void* Allocate(uint64 Size) = 0;
};

} // namespace TraceServices
