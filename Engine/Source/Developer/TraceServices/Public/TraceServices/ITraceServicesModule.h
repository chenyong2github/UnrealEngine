// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

namespace Trace
{

class IAnalysisService;
class IModuleService;

}

class ITraceServicesModule
	: public IModuleInterface
{
public:
	virtual TSharedPtr<Trace::IAnalysisService> GetAnalysisService() = 0;
	virtual TSharedPtr<Trace::IModuleService> GetModuleService() = 0;
	virtual TSharedPtr<Trace::IAnalysisService> CreateAnalysisService() = 0;
	virtual TSharedPtr<Trace::IModuleService> CreateModuleService() = 0;

	virtual ~ITraceServicesModule() = default;
};
