// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGContext.h"
#include "PCGData.h"

class IPCGElement;
class UPCGComponent;
class UPCGSettings;

typedef TSharedPtr<IPCGElement, ESPMode::ThreadSafe> FPCGElementPtr;

#define PCGE_LOG_C(Verbosity, CustomContext, Format, ...) \
	UE_LOG(LogPCG, \
		Verbosity, \
		TEXT("[%s - %s]: " Format), \
		*((CustomContext)->GetComponentName()), \
		*((CustomContext)->GetTaskName()), \
		##__VA_ARGS__)

#define PCGE_LOG(Verbosity, Format, ...) PCGE_LOG_C(Verbosity, Context, Format, ##__VA_ARGS__)

/**
* Base class for the processing bit of a PCG node/settings
*/
class PCG_API IPCGElement
{
public:
	virtual ~IPCGElement() = default;
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent) = 0;

	bool Execute(FPCGContext* Context) const;

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const = 0;
	virtual bool IsCancellable() const { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const { return true; }
};

/**
* Basic PCG element class for elements that do not store any intermediate data in the context
*/
class PCG_API FSimplePCGElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent) override;
};

/**
* CRTP PCG element class to facilitate settings retrieval
*/
template<typename SettingsClass>
class PCG_API FSimpleTypedPCGElement : public FSimplePCGElement
{
public:
	const SettingsClass* GetSettings();
};
