// Copyright Epic Games, Inc. All Rights Reserved.

#include "HordeExecutorModule.h"
#include "ISettingsModule.h"
#include "HordeExecutorSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Messages.h"

IMPLEMENT_MODULE(FHordeExecutorModule, EditorConfig);

#define LOCTEXT_NAMESPACE "HordeExecutorModule"

DEFINE_LOG_CATEGORY(LogHordeExecutor);


void FHordeExecutorModule::StartupModule()
{
	GetMutableDefault<UHordeExecutorSettings>()->LoadConfig();

	const FString Target = GetDefault<UHordeExecutorSettings>()->Target;
	const FString PemCertificateChain = GetDefault<UHordeExecutorSettings>()->PemCertificateChain;
	const FString PemPrivateKey = GetDefault<UHordeExecutorSettings>()->PemPrivateKey;
	const FString PemRootCertificates = GetDefault<UHordeExecutorSettings>()->PemRootCertificates;

	FHordeExecutor::FSslCredentialsOptions SslCredentialsOptions;
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

void FHordeExecutorModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(TEXT("RemoteExecution"), &HordeExecution);
}

bool FHordeExecutorModule::SupportsDynamicReloading()
{
	return true;
}

#undef LOCTEXT_NAMESPACE
