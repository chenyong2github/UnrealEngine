// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstallBundleManagerInterface.h"
#include "InstallBundleManagerModule.h"
#include "Modules/ModuleManager.h"

class FNullInstallBundleManager : public IInstallBundleManager
{
	virtual bool HasBundleSource(EInstallBundleSourceType SourceType) const override
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

	virtual TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> RequestUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags Flags, ELogVerbosity::Type LogVerbosityOverride = ELogVerbosity::NoLogging) override
	{
		return MakeValue(FInstallBundleRequestInfo());
	}

	virtual void GetContentState(TArrayView<const FName> BundleNames, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag) override
	{
		FInstallBundleCombinedContentState State;
		Callback.ExecuteIfBound(State);
	}

	virtual void CancelAllGetContentStateRequestsForTag(FName RequestTag) override
	{
	}

	virtual void GetInstallState(TArrayView<const FName> BundleNames, bool bAddDependencies, FInstallBundleGetInstallStateDelegate Callback, FName RequestTag = NAME_None) override
	{
		FInstallBundleCombinedInstallState State;
		Callback.ExecuteIfBound(State);
	}

	virtual TValueOrError<FInstallBundleCombinedInstallState, EInstallBundleResult> GetInstallStateSynchronous(TArrayView<const FName> BundleNames, bool bAddDependencies) const override
	{
		return MakeValue(FInstallBundleCombinedInstallState());
	}

	virtual void CancelAllGetInstallStateRequestsForTag(FName RequestTag) override
	{
	}

	virtual TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> RequestReleaseContent(TArrayView<const FName> ReleaseNames, EInstallBundleReleaseRequestFlags Flags, TArrayView<const FName> KeepNames = TArrayView<const FName>(), ELogVerbosity::Type LogVerbosityOverride = ELogVerbosity::NoLogging) override
	{
		return MakeValue(FInstallBundleRequestInfo());
	}

	virtual void RequestRemoveContentOnNextInit(TArrayView<const FName> RemoveNames, TArrayView<const FName> KeepNames = TArrayView<const FName>()) override
	{
	}

	virtual void CancelRequestRemoveContentOnNextInit(TArrayView<const FName> BundleNames) override
	{
	}

	virtual void CancelUpdateContent(TArrayView<const FName> BundleNames) override
	{
	}

	virtual void PauseUpdateContent(TArrayView<const FName> BundleNames) override
	{

	}

	virtual void ResumeUpdateContent(TArrayView<const FName> BundleNames) override
	{

	}

	virtual void RequestPausedBundleCallback() override
	{

	}

	virtual TOptional<FInstallBundleProgress> GetBundleProgress(FName BundleName) const override
	{
		return TOptional<FInstallBundleProgress>();
	}

	virtual EInstallBundleRequestFlags GetModifyableContentRequestFlags() const override
	{
		return EInstallBundleRequestFlags::None;
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
