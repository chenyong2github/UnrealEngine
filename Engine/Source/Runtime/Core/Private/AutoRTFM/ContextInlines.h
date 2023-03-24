// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "TransactionInlines.h"

namespace AutoRTFM
{

inline void FContext::RecordWrite(void* LogicalAddress, size_t Size, bool bIsClosed)
{
    CurrentTransaction->RecordWrite(LogicalAddress, Size, bIsClosed);
}

inline void FContext::DidAllocate(void* LogicalAddress, size_t Size)
{
    CurrentTransaction->DidAllocate(LogicalAddress, Size);
}

} // namespace AutoRTFM
