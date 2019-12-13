// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InstallBundleManagerInterface.h"
#include "InstallBundleManagerModule.h"

FInstallBundleCompleteMultiDelegate IInstallBundleManager::InstallBundleCompleteDelegate;

FInstallBundleCompleteMultiDelegate IInstallBundleManager::RemoveBundleCompleteDelegate;

FInstallBundlePausedMultiDelegate IInstallBundleManager::PausedBundleDelegate;

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
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("ModuleName"), ModuleName, GEngineIni);

		if (FModuleManager::Get().ModuleExists(*ModuleName))
		{
			FModuleStatus Status;
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

FInstallBundleRequestInfo IInstallBundleManager::RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags)
{
	return RequestUpdateContent(MakeArrayView(&BundleName, 1), Flags);
}

void IInstallBundleManager::GetContentState(FName BundleName, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag /*= TEXT("None")*/)
{
	return GetContentState(MakeArrayView(&BundleName, 1), Flags, bAddDependencies, MoveTemp(Callback), RequestTag);
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

