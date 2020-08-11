// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstallBundleManagerInterface.h"
#include "InstallBundleManagerModule.h"

FInstallBundleCompleteMultiDelegate IInstallBundleManager::InstallBundleUpdatedDelegate;
FInstallBundleCompleteMultiDelegate IInstallBundleManager::InstallBundleCompleteDelegate;

FInstallBundlePausedMultiDelegate IInstallBundleManager::PausedBundleDelegate;

FInstallBundleReleasedMultiDelegate IInstallBundleManager::ReleasedDelegate;
FInstallBundleReleasedMultiDelegate IInstallBundleManager::RemovedDelegate;

FInstallBundleManagerOnPatchCheckComplete IInstallBundleManager::PatchCheckCompleteDelegate;

IInstallBundleManager* IInstallBundleManager::GetPlatformInstallBundleManager()
{
	static IInstallBundleManager* Manager = nullptr;
	static bool bCheckedIni = false;

	if (Manager)
		return Manager;

	if (!bCheckedIni && !GEngineIni.IsEmpty())
	{
		FString ModuleName;
		IInstallBundleManagerModule* Module = nullptr;
#if WITH_EDITOR
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("EditorModuleName"), ModuleName, GEngineIni);
#else
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("ModuleName"), ModuleName, GEngineIni);
#endif // WITH_EDITOR

		if (FModuleManager::Get().ModuleExists(*ModuleName))
		{
			Module = FModuleManager::LoadModulePtr<IInstallBundleManagerModule>(*ModuleName);
			if (Module)
			{
				Manager = Module->GetInstallBundleManager();
			}
		}

		bCheckedIni = true;
	}

	return Manager;
}

TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> IInstallBundleManager::RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags)
{
	return RequestUpdateContent(MakeArrayView(&BundleName, 1), Flags);
}

void IInstallBundleManager::GetContentState(FName BundleName, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag /*= NAME_None*/)
{
	return GetContentState(MakeArrayView(&BundleName, 1), Flags, bAddDependencies, MoveTemp(Callback), RequestTag);
}

void IInstallBundleManager::GetInstallState(FName BundleName, bool bAddDependencies, FInstallBundleGetInstallStateDelegate Callback, FName RequestTag /*= NAME_None*/)
{
	return GetInstallState(MakeArrayView(&BundleName, 1), bAddDependencies, MoveTemp(Callback), RequestTag);
}

TValueOrError<FInstallBundleCombinedInstallState, EInstallBundleResult> IInstallBundleManager::GetInstallStateSynchronous(FName BundleName, bool bAddDependencies) const
{
	return GetInstallStateSynchronous(MakeArrayView(&BundleName, 1), bAddDependencies);
}

TValueOrError<FInstallBundleRequestInfo, EInstallBundleResult> IInstallBundleManager::RequestReleaseContent(FName ReleaseName, EInstallBundleReleaseRequestFlags Flags, TArrayView<const FName> KeepNames /*= TArrayView<const FName>()*/)
{
	return RequestReleaseContent(MakeArrayView(&ReleaseName, 1), Flags, KeepNames);
}

void IInstallBundleManager::RequestRemoveContentOnNextInit(FName RemoveName, TArrayView<const FName> KeepNames /*= TArrayView<const FName>()*/)
{
	RequestRemoveContentOnNextInit(MakeArrayView(&RemoveName, 1), KeepNames);
}

void IInstallBundleManager::CancelRequestRemoveContentOnNextInit(FName BundleName)
{
	CancelRequestRemoveContentOnNextInit(MakeArrayView(&BundleName, 1));
}

void IInstallBundleManager::CancelUpdateContent(FName BundleName, EInstallBundleCancelFlags Flags)
{
	CancelUpdateContent(MakeArrayView(&BundleName, 1), Flags);
}

void IInstallBundleManager::PauseUpdateContent(FName BundleName)
{
	PauseUpdateContent(MakeArrayView(&BundleName, 1));
}

void IInstallBundleManager::ResumeUpdateContent(FName BundleName)
{
	ResumeUpdateContent(MakeArrayView(&BundleName, 1));
}

void IInstallBundleManager::UpdateContentRequestFlags(FName BundleName, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags)
{
	UpdateContentRequestFlags(MakeArrayView(&BundleName, 1), AddFlags, RemoveFlags);
}

void IInstallBundleManager::StartPatchCheck()
{
	PatchCheckCompleteDelegate.Broadcast(EInstallBundleManagerPatchCheckResult::NoPatchRequired);
}

