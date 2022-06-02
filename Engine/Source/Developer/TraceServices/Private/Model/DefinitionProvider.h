// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Common/PagedArray.h"
#include "Templates/UniquePtr.h"
#include "TraceServices/Model/Definitions.h"
#include "Trace/Analyzer.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"

namespace TraceServices
{

class FDefinitionProvider : public IDefinitionProvider
{
public:
	FDefinitionProvider(IAnalysisSession* InSession);

private:
	virtual void AddEntry(uint64 Hash, void* Ptr) override;
	virtual const void* FindEntry(uint64 Hash) const override;
	virtual void* Allocate(uint32 Size, uint32 Alignment) override;

	TArray<TUniquePtr<uint8>> Pages;
	static constexpr uint32 PageSize = 1024;
	uint32 PageRemain;

	mutable FRWLock DefinitionsLock;
	TMap<uint64, void*> Definitions;
};

} // namespace TraceServices
