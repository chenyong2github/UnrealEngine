// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformInstallBundleManager.h"
#include "Modules/ModuleManager.h"

class FNullInstallBundleManager : public IPlatformInstallBundleManager
{
	virtual void PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback) override
	{
	}

	virtual void PopInitErrorCallback() override
	{
	}

	virtual bool IsInitializing() const override
	{
		return false;
	}
	virtual bool IsInitialized() const override
	{
		return true;
	}

	virtual bool IsActive() const override
	{
		return false;
	}

	virtual FInstallBundleRequestInfo RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags) override
	{
		FInstallBundleRequestInfo RetInfo;
		return RetInfo;
	}

	virtual FInstallBundleRequestInfo RequestUpdateContent(TArrayView<FName> BundleNames, EInstallBundleRequestFlags Flags) override
	{
		FInstallBundleRequestInfo RetInfo;
		return RetInfo;
	}

	virtual FInstallBundleRequestInfo RequestRemoveBundle(FName BundleName) override
	{
		FInstallBundleRequestInfo RetInfo;
		return RetInfo;
	}

	virtual void RequestRemoveBundleOnNextInit(FName BundleName) override
	{

	}

	virtual void CancelBundle(FName BundleName) override
	{

	}

	virtual TOptional<FInstallBundleStatus> GetBundleProgress(FName BundleName) const override
	{
		return TOptional<FInstallBundleStatus>();
	}

private:
	
};

class FNullInstallBundleManagerModule : public IPlatformInstallBundleManagerModule
{
public:
	virtual void StartupModule() override
	{
		InstallBundleManager = MakeUnique<FNullInstallBundleManager>();
	}

	virtual void PreUnloadCallback() override
	{
		InstallBundleManager.Reset();
	}

	virtual IPlatformInstallBundleManager* GetInstallBundleManager() override
	{
		return InstallBundleManager.Get();
	}

private:
	TUniquePtr<FNullInstallBundleManager> InstallBundleManager;
};

IMPLEMENT_MODULE(FNullInstallBundleManagerModule, NullInstallBundleManager);
