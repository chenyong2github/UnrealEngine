// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Trace/Store.h"
#include "Modules/ModuleInterface.h"

namespace Trace
{

class IAnalysisService;
class ISessionService;
class IModuleService;

}

class ITraceServicesModule
	: public IModuleInterface
{
public:
	virtual TSharedPtr<Trace::ISessionService> GetSessionService() = 0;
	virtual TSharedPtr<Trace::IAnalysisService> GetAnalysisService() = 0;
	virtual TSharedPtr<Trace::IModuleService> GetModuleService() = 0;
	virtual TSharedPtr<Trace::ISessionService> CreateSessionService(const TCHAR* SessionDirectory) = 0;
	virtual TSharedPtr<Trace::IAnalysisService> CreateAnalysisService() = 0;
	virtual TSharedPtr<Trace::IModuleService> CreateModuleService() = 0;

	virtual ~ITraceServicesModule() = default;
};
