// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "Misc/MTAccessDetector.h"
#include "Misc/ScopeLock.h"

//#define UE_DETECT_DELEGATES_RACE_CONDITIONS 1

#if !defined(UE_DETECT_DELEGATES_RACE_CONDITIONS)
	#define UE_DETECT_DELEGATES_RACE_CONDITIONS 0
#endif

/**
 * non thread-safe version that does not do any race detection. supposed to be used in a controlled environment that provides own
 * detection or synchronization.
 */
class FDelegateAccessHandlerBaseUnchecked
{
protected:
	struct FReadAccessScope {};
	struct FWriteAccessScope {};

	[[nodiscard]] FReadAccessScope GetReadAccessScope() const
	{
		return {};
	}

	[[nodiscard]] FWriteAccessScope GetWriteAccessScope()
	{
		return {};
	}
};

/**
 * non thread-safe version that detects not thread-safe delegates used concurrently (dev builds only)
 */
class FDelegateAccessHandlerBaseChecked
#if !UE_DETECT_DELEGATES_RACE_CONDITIONS
	: public FDelegateAccessHandlerBaseUnchecked
#endif // !UE_DETECT_DELEGATES_RACE_CONDITIONS
{
#if UE_DETECT_DELEGATES_RACE_CONDITIONS
protected:
	class FReadAccessScope
	{
	public:
		UE_NONCOPYABLE(FReadAccessScope);

		explicit FReadAccessScope(FRWFullyRecursiveAccessDetector& InAccessDetector)
			: Accessor(MakeScopedReaderAccessDetector(InAccessDetector))
		{
		}

	private:
		TScopedReaderAccessDetector<FRWFullyRecursiveAccessDetector> Accessor;
	};

	[[nodiscard]] FReadAccessScope GetReadAccessScope() const
	{
		return FReadAccessScope(Accessor);
	}

	class FWriteAccessScope
	{
	public:
		UE_NONCOPYABLE(FWriteAccessScope);

		explicit FWriteAccessScope(FRWFullyRecursiveAccessDetector& InAccessDetector)
			: Accessor(MakeScopedWriterAccessDetector(InAccessDetector))
		{
		}

	private:
		TScopedWriterDetector<FRWFullyRecursiveAccessDetector> Accessor;
	};

	[[nodiscard]] FWriteAccessScope GetWriteAccessScope()
	{
		return FWriteAccessScope(Accessor);
	}

private:
	mutable FRWFullyRecursiveAccessDetector Accessor;
#endif // UE_DETECT_DELEGATES_RACE_CONDITIONS
};
