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

	virtual EInstallBundleManagerInitState GetInitState() const override
	{
		return EInstallBundleManagerInitState::Succeeded;
	}

	virtual FInstallBundleRequestInfo RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags) override
	{
		FInstallBundleRequestInfo RetInfo;
		return RetInfo;
	}
	virtual FInstallBundleRequestInfo RequestUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags Flags) override
	{
		FInstallBundleRequestInfo RetInfo;
		return RetInfo;
	}

	virtual void GetContentState(FName BundleName, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag) override
	{
		FInstallBundleCombinedContentState State;
		State.State = EInstallBundleContentState::UpToDate;
		Callback.ExecuteIfBound(State);
	}

	virtual void GetContentState(TArrayView<const FName> BundleNames, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag) override
	{
		FInstallBundleCombinedContentState State;
		State.State = EInstallBundleContentState::UpToDate;
		Callback.ExecuteIfBound(State);
	}

	virtual void CancelAllGetContentStateRequestsForTag(FName RequestTag) override
	{
	}

	virtual void RequestRemoveContentOnNextInit(FName RemoveName, TArrayView<const FName> KeepNames = TArrayView<const FName>()) override
	{
	}
	virtual void RequestRemoveContentOnNextInit(TArrayView<const FName> RemoveNames, TArrayView<const FName> KeepNames = TArrayView<const FName>()) override
	{
	}

	virtual void CancelRequestRemoveContentOnNextInit(FName BundleName) override
	{

	}
	virtual void CancelRequestRemoveContentOnNextInit(TArrayView<const FName> BundleNames) override
	{
	}

	virtual void CancelUpdateContent(FName BundleName, EInstallBundleCancelFlags Flags) override
	{

	}
	virtual void CancelUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleCancelFlags Flags) override
	{
	}

	virtual void PauseUpdateContent(FName BundleName) override
	{

	}
	virtual void PauseUpdateContent(TArrayView<const FName> BundleNames) override
	{

	}

	virtual void ResumeUpdateContent(FName BundleName) override
	{

	}
	virtual void ResumeUpdateContent(TArrayView<const FName> BundleNames) override
	{

	}

	virtual void RequestPausedBundleCallback() override
	{

	}

	virtual TOptional<FInstallBundleStatus> GetBundleProgress(FName BundleName) const override
	{
		return TOptional<FInstallBundleStatus>();
	}

	virtual EInstallBundleRequestFlags GetModifyableContentRequestFlags() const override
	{
		return EInstallBundleRequestFlags::None;
	}
	virtual void UpdateContentRequestFlags(FName BundleName, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) override
	{

	}
	virtual void UpdateContentRequestFlags(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) override
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
