// Copyright Epic Games, Inc. All Rights Reserved.

#include "BazelExecutorModule.h"
#include "ISettingsModule.h"
#include "BazelExecutorSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Messages.h"

IMPLEMENT_MODULE(FBazelExecutorModule, EditorConfig);

#define LOCTEXT_NAMESPACE "BazelExecutorModule"

DEFINE_LOG_CATEGORY(LogBazelExecutor);


void FBazelExecutorModule::StartupModule()
{
	GetMutableDefault<UBazelExecutorSettings>()->LoadConfig();

	const FString Target = GetDefault<UBazelExecutorSettings>()->Target;
	const FString PemCertificateChain = GetDefault<UBazelExecutorSettings>()->PemCertificateChain;
	const FString PemPrivateKey = GetDefault<UBazelExecutorSettings>()->PemPrivateKey;
	const FString PemRootCertificates = GetDefault<UBazelExecutorSettings>()->PemRootCertificates;

	FBazelExecutor::FSslCredentialsOptions SslCredentialsOptions;
	if (!PemCertificateChain.IsEmpty() && FPaths::FileExists(PemCertificateChain))
	{
		FFileHelper::LoadFileToString(SslCredentialsOptions.PemCertChain, *PemCertificateChain);
	}
	if (!PemPrivateKey.IsEmpty() && FPaths::FileExists(PemPrivateKey))
	{
		FFileHelper::LoadFileToString(SslCredentialsOptions.PemPrivateKey, *PemPrivateKey);
	}
	if (!PemRootCertificates.IsEmpty() && FPaths::FileExists(PemRootCertificates))
	{
		FFileHelper::LoadFileToString(SslCredentialsOptions.PemRootCerts, *PemRootCertificates);
	}

	HordeExecution.Initialize(Target, SslCredentialsOptions);

	IModularFeatures::Get().RegisterModularFeature(TEXT("RemoteExecution"), &HordeExecution);
}

void FBazelExecutorModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(TEXT("RemoteExecution"), &HordeExecution);
}

bool FBazelExecutorModule::SupportsDynamicReloading()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
