// Copyright Epic Games, Inc. All Rights Reserved.

#include "BazelExecutorModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "BazelExecutorSettings.h"
#include "Messages.h"

IMPLEMENT_MODULE(FBazelExecutorModule, EditorConfig);

#define LOCTEXT_NAMESPACE "BazelExecutorModule"

DEFINE_LOG_CATEGORY(LogBazelExecutor);

static FName RemoteExecutionFeatureName(TEXT("RemoteExecution"));


void FBazelExecutorModule::StartupModule()
{
	GetMutableDefault<UBazelExecutorSettings>()->LoadConfig();

	const UBazelExecutorSettings* BazelExecutorSettings = GetDefault<UBazelExecutorSettings>();

	FBazelExecutor::FSettings Settings;
	Settings.ContentAddressableStorageTarget = BazelExecutorSettings->ContentAddressableStorageTarget;
	Settings.ExecutionTarget = BazelExecutorSettings->ExecutionTarget;
	Settings.ContentAddressableStorageHeaders = BazelExecutorSettings->ContentAddressableStorageHeaders;
	Settings.ExecutionHeaders = BazelExecutorSettings->ExecutionHeaders;
	Settings.MaxSendMessageSize = BazelExecutorSettings->MaxSendMessageSize;
	Settings.MaxReceiveMessageSize = BazelExecutorSettings->MaxReceiveMessageSize;

	if (!BazelExecutorSettings->ContentAddressableStoragePemCertificateChain.IsEmpty())
	{
		if (FPaths::FileExists(BazelExecutorSettings->ContentAddressableStoragePemCertificateChain))
		{
			FFileHelper::LoadFileToString(Settings.ContentAddressableStoragePemCertificateChain, *BazelExecutorSettings->ContentAddressableStoragePemCertificateChain);
		}
		else
		{
			Settings.ContentAddressableStoragePemCertificateChain = BazelExecutorSettings->ContentAddressableStoragePemCertificateChain;
		}
	}
	if (!BazelExecutorSettings->ContentAddressableStoragePemPrivateKey.IsEmpty())
	{
		if (FPaths::FileExists(BazelExecutorSettings->ContentAddressableStoragePemPrivateKey))
		{
			FFileHelper::LoadFileToString(Settings.ContentAddressableStoragePemPrivateKey, *BazelExecutorSettings->ContentAddressableStoragePemPrivateKey);
		}
		else
		{
			Settings.ContentAddressableStoragePemPrivateKey = BazelExecutorSettings->ContentAddressableStoragePemPrivateKey;
		}
	}
	if (!BazelExecutorSettings->ContentAddressableStoragePemRootCertificates.IsEmpty())
	{
		if (FPaths::FileExists(BazelExecutorSettings->ContentAddressableStoragePemRootCertificates))
		{
			FFileHelper::LoadFileToString(Settings.ContentAddressableStoragePemRootCertificates, *BazelExecutorSettings->ContentAddressableStoragePemRootCertificates);
		}
		else
		{
			Settings.ContentAddressableStoragePemRootCertificates = BazelExecutorSettings->ContentAddressableStoragePemRootCertificates;
		}
	}

	if (!BazelExecutorSettings->ExecutionPemCertificateChain.IsEmpty())
	{
		if (FPaths::FileExists(BazelExecutorSettings->ExecutionPemCertificateChain))
		{
			FFileHelper::LoadFileToString(Settings.ExecutionPemCertificateChain, *BazelExecutorSettings->ExecutionPemCertificateChain);
		}
		else
		{
			Settings.ExecutionPemCertificateChain = BazelExecutorSettings->ExecutionPemCertificateChain;
		}
	}
	if (!BazelExecutorSettings->ExecutionPemPrivateKey.IsEmpty())
	{
		if (FPaths::FileExists(BazelExecutorSettings->ExecutionPemPrivateKey))
		{
			FFileHelper::LoadFileToString(Settings.ExecutionPemPrivateKey, *BazelExecutorSettings->ExecutionPemPrivateKey);
		}
		else
		{
			Settings.ExecutionPemPrivateKey = BazelExecutorSettings->ExecutionPemPrivateKey;
		}
	}
	if (!BazelExecutorSettings->ExecutionPemRootCertificates.IsEmpty())
	{
		if (FPaths::FileExists(BazelExecutorSettings->ExecutionPemRootCertificates))
		{
			FFileHelper::LoadFileToString(Settings.ExecutionPemRootCertificates, *BazelExecutorSettings->ExecutionPemRootCertificates);
		}
		else
		{
			Settings.ExecutionPemRootCertificates = BazelExecutorSettings->ExecutionPemRootCertificates;
		}
	}

	BazelExecution.Initialize(Settings);

	IModularFeatures::Get().RegisterModularFeature(RemoteExecutionFeatureName, &BazelExecution);
}

void FBazelExecutorModule::ShutdownModule()
{
	BazelExecution.Shutdown();
	IModularFeatures::Get().UnregisterModularFeature(RemoteExecutionFeatureName, &BazelExecution);
}

bool FBazelExecutorModule::SupportsDynamicReloading()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
