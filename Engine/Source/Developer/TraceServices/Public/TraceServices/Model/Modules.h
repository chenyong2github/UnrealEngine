// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "TraceServices/Model/AnalysisSession.h"
#include <atomic>

namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
/**
  * Result of a query. Since symbol resolving can be deferred this signals if a
  * symbol has been resolved, waiting to be resolved or wasn't found at all.
  */
enum QueryResult 
{
	QR_OK,
	QR_NotFound,
	QR_NotLoaded,
};

////////////////////////////////////////////////////////////////////////////////
/**
  * Represent a resolved symbol. The resolve status and string values may change
  * over time, but string pointers returned from the methods are guaranteed to live 
  * during the entire analysis session.
  */
struct FResolvedSymbol
{
	std::atomic<QueryResult> Result;
	const TCHAR* Name;
	const TCHAR* FileAndLine;
};

////////////////////////////////////////////////////////////////////////////////
class IModuleProvider : public IProvider
{
public:
	struct FStats
	{
		uint32 ModulesDiscovered;
		uint32 ModulesLoaded;
		uint32 ModulesFailed;
		uint32 SymbolsDiscovered;
		uint32 SymbolsResolved;
		uint32 SymbolsFailed;
	};
	
	/** Queries the name of the symbol at address */
	virtual const FResolvedSymbol* GetSymbol(uint64 Address) = 0;

	/** Gets statistics from provider */
	virtual void GetStats(FStats* OutStats) const = 0;
};

////////////////////////////////////////////////////////////////////////////////
} // namespace TraceServices
