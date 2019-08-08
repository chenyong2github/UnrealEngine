// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformInstallBundleManager.h"
#include "Modules/ModuleManager.h"

class FNullInstallBundleManager : public IPlatformInstallBundleManager
{
	virtual bool HasBuildMetaData() const override
	{
		return false;
	}

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

	virtual void GetContentState(FName BundleName, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag) override
	{
		FInstallBundleContentState State;
		State.State = EInstallBundleContentState::UpToDate;
		Callback.ExecuteIfBound(State);
	}

	virtual void GetContentState(TArrayView<FName> BundleNames, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag) override
	{
		FInstallBundleContentState State;
		State.State = EInstallBundleContentState::UpToDate;
		Callback.ExecuteIfBound(State);
	}

    virtual void CancelAllGetContentStateRequestsForTag(FName RequestTag) override
    {
        
    }
    
	virtual void RequestRemoveBundleOnNextInit(FName BundleName) override
	{

	}

	virtual void CancelRequestRemoveBundleOnNextInit(FName BundleName) override
	{

	}

	virtual void CancelBundle(FName BundleName, EInstallBundleCancelFlags Flags) override
	{

	}

	virtual void CancelAllBundles(EInstallBundleCancelFlags Flags) override
	{

	}

	virtual bool PauseBundle(FName BundleName) override
	{
		return false;
	}

	virtual void ResumeBundle(FName BundleName) override
	{

	}

	virtual void RequestPausedBundleCallback() const override
	{

	}

	virtual TOptional<FInstallBundleStatus> GetBundleProgress(FName BundleName) const override
	{
		return TOptional<FInstallBundleStatus>();
	}

	virtual void UpdateContentRequestFlags(FName BundleName, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) override
	{

	}

	virtual bool IsNullInterface() const override
	{
		return true;
	}

private:
	
};

class FNullInstallBundleManagerModule : public TPlatformInstallBundleManagerModule<FNullInstallBundleManager>
{	
};

IMPLEMENT_MODULE(FNullInstallBundleManagerModule, NullInstallBundleManager);
