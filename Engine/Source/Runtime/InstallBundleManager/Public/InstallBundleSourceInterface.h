// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"

class IInstallBundleSource;
class IAnalyticsProviderET;

DECLARE_DELEGATE_ThreeParams(FInstallBundleSourceInitDelegate, TSharedRef<IInstallBundleSource> /*Source*/, EInstallBundleManagerInitResult /*Result*/, bool /*bShouldUseFallbackSource*/)

class IInstallBundleSource : public TSharedFromThis<IInstallBundleSource>
{
public:
	virtual ~IInstallBundleSource() {}

	virtual EInstallBundleSourceType GetSourceType() const = 0;
	virtual float GetSourceWeight() const { return 1.0f; }

	virtual void Init(
		TSharedRef<FConfigFile> InstallBundleConfig, 
		TSharedPtr<IAnalyticsProviderET> AnalyticsProvider) = 0;
	virtual void AsyncInit(FInstallBundleSourceInitDelegate Callback) = 0;

	virtual EInstallBundleManagerInitState GetInitState() const = 0;
	// Only valid after AsyncInit completes
	virtual EInstallBundleManagerInitResult GetLastInitResult() const = 0;

	virtual void GetContentState(TArrayView<FName> BundleNames, EInstallBundleGetContentStateFlags Flags, FInstallBundleGetContentStateDelegate Callback) = 0;

	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}
};
