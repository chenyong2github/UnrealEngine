// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InstallBundleManagerInterface.h"
#include "InstallBundleManagerModule.h"
#include "Modules/ModuleManager.h"

class FNullInstallBundleManager : public IInstallBundleManager
{
	virtual bool HasBuildMetaData() const override
	{
		return false;
	}

	virtual FDelegateHandle PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback) override
	{
		return FDelegateHandle();
	}

	virtual void PopInitErrorCallback() override
	{
	}

	void PopInitErrorCallback(FDelegateHandle Handle) override
	{
	}

	virtual void PopInitErrorCallback(const void* InUserObject) override
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

	virtual FInstallBundleTestInfo TestUpdateContent(FName BundleName) override
	{
		return FInstallBundleTestInfo();
	}
	virtual FInstallBundleTestInfo TestUpdateContent(TArrayView<FName> BundleNames) override
	{
		return FInstallBundleTestInfo();
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

	virtual FInstallBundleRequestInfo RequestRemoveContent(FName BundleName) override
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

	virtual void RequestRemoveContentOnNextInit(FName RemoveName, TArrayView<FName> KeepNames = TArrayView<FName>()) override
	{
	}
	virtual void RequestRemoveContentOnNextInit(TArrayView<FName> RemoveNames, TArrayView<FName> KeepNames = TArrayView<FName>()) override
	{
	}

	virtual void CancelRequestRemoveContentOnNextInit(FName BundleName) override
	{

	}
	virtual void CancelRequestRemoveContentOnNextInit(TArrayView<FName> BundleNames) override
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

class FNullInstallBundleManagerModule : public TInstallBundleManagerModule<FNullInstallBundleManager>
{	
};

IMPLEMENT_MODULE(FNullInstallBundleManagerModule, NullInstallBundleManager);
