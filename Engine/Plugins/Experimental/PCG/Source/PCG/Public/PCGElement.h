// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGContext.h"
#include "PCGData.h"

class IPCGElement;
class UPCGComponent;

typedef TSharedPtr<IPCGElement, ESPMode::ThreadSafe> FPCGElementPtr;

/**
* Base class for the processing bit of a PCG node/settings
*/
class IPCGElement
{
public:
	virtual ~IPCGElement() = default;
	virtual FPCGContextPtr Initialize(const FPCGDataCollection& InputData, const UPCGComponent* SourceComponent) = 0;
	virtual bool Execute(FPCGContextPtr Context) const = 0;
};

/**
* Basic PCG element class for elements that do not store any intermediate data in the context
*/
class FSimplePCGElement : public IPCGElement
{
public:
	virtual FPCGContextPtr Initialize(const FPCGDataCollection& InputData, const UPCGComponent* SourceComponent) override;
};

/**
* CRTP PCG element class to facilitate settings retrieval
*/
template<typename SettingsClass>
class FSimpleTypedPCGElement : public FSimplePCGElement
{
public:
	const SettingsClass* GetSettings();
};