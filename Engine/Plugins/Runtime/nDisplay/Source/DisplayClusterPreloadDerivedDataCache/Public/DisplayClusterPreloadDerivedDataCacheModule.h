// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class FDisplayClusterPreloadDerivedDataCacheModule : public IModuleInterface
{
public:
	
	static FDisplayClusterPreloadDerivedDataCacheModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	void LaunchAndCommunicateWithProcess();

protected:
	
	void RegexParseForLoadingProgress(const FText& LoadingPackagesFormat, const FText& UnknownAmount, struct FScopedSlowTask& SlowTask,
									  int32& LoadingTotal, class FRegexMatcher& LoadingProgressRegex);
	void RegexParseForCompilationProgress(const FText& CompilingAssetsFormat, const FText& UnknownAmount, FScopedSlowTask& SlowTask,
	                                      int32& CompileTotal, int32& AssetsWaitingToCompile,
	                                      FRegexMatcher& CompileProgressRegex);
	void CompleteCommandletAndShowNotification(const int32 ResultCode, const bool bWasCancelled,
	const FString& CurrentExecutableName, const FString& Arguments);

	FString GetDdcCommandletParams() const;
	FString GetTargetPlatformParams() const;

	TSharedPtr<class SNotificationItem> Notification;

private:
	
	void OnFEngineLoopInitComplete();
};
