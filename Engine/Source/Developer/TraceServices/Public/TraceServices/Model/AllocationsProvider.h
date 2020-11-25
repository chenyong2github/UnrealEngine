// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnalysisSession.h"
#include "CoreTypes.h"
#include "Templates/UniquePtr.h"

#include <new>

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class IAllocationsProvider : public IProvider
{
public:
	// Allocation query rules.
	// The enum uses the following naming convention:
	//     A, B, C, D = time markers
	//     a = time when "alloc" event occurs
	//     f = time when "free" event occurs (can be infinite)
	// Ex.: "AaBf" means "all memory allocations allocated between time A and time B and freed after time B".
	enum class EQueryRule
	{
		aAf,     // active allocs at A
		afA,     // before
		Aaf,     // after
		aAfB,    // decline
		AaBf,    // growth
		AafB,    // short living allocs
		aABf,    // long living allocs
		AaBCf,   // memory leaks
		AaBfC,   // limited lifetime
		aABfC,   // decline of long living allocs
		AaBCfD,  // specific lifetime
		//A_vs_B,  // compare A vs. B; {aAf} vs. {aBf}
		//A_or_B,  // live at A or at B; {aAf} U {aBf}
		//A_xor_B, // live either at A or at B; ({aAf} U {aBf}) \ {aABf}
	};

	struct TRACESERVICES_API FQueryParams
	{
		EQueryRule Rule;
		double TimeA;
		double TimeB;
		double TimeC;
		double TimeD;
	};

	struct TRACESERVICES_API FAllocation
	{
		double GetStartTime() const;
		double GetEndTime() const;
		uint64 GetAddress() const;
		uint32 GetSize() const;
		uint8 GetAlignment() const;
		uint8 GetWaste() const;
		uint64 GetBacktraceId() const;
		uint32 GetTag() const;
	};

	class TRACESERVICES_API FAllocations
	{
	public:
		void operator delete (void* Address);
		uint32 Num() const;
		const FAllocation* Get(uint32 Index) const;
	};

	typedef TUniquePtr<const FAllocations> FQueryResult;

	enum class EQueryStatus
	{
		Unknown,
		Done,
		Working,
		Available,
	};

	struct TRACESERVICES_API FQueryStatus
	{
		FQueryResult NextResult() const;

		EQueryStatus Status;
		mutable UPTRINT Handle;
	};

	typedef UPTRINT FQueryHandle;

public:
	virtual bool IsInitialized() const = 0;

	virtual FQueryHandle StartQuery(const FQueryParams& Params) const = 0;
	virtual void CancelQuery(FQueryHandle Query) const = 0;
	virtual const FQueryStatus PollQuery(FQueryHandle Query) const = 0;
};

TRACESERVICES_API const IAllocationsProvider* ReadAllocationsProvider(const IAnalysisSession& Session);

} // namespace TraceServices
