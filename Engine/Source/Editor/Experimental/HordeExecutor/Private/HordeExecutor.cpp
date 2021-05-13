// Copyright Epic Games, Inc. All Rights Reserved.

#include "HordeExecutor.h"

#include "Execution.h"
#include "ContentAddressableStorage.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
#include "build\bazel\remote\execution\v2\remote_execution.pb.h"
#include "build\bazel\remote\execution\v2\remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END

#include <memory>

#define LOCTEXT_NAMESPACE "HordeExecutor"


void FHordeExecutor::Initialize(const FString& Target, const FSslCredentialsOptions& SslCredentialsOptions)
{
	ContentAddressableStorage.Reset();
	Execution.Reset();

	grpc::SslCredentialsOptions SslOptions;
	SslOptions.pem_cert_chain.assign(TCHAR_TO_UTF8(*SslCredentialsOptions.PemCertChain));
	SslOptions.pem_private_key.assign(TCHAR_TO_UTF8(*SslCredentialsOptions.PemPrivateKey));
	SslOptions.pem_root_certs.assign(TCHAR_TO_UTF8(*SslCredentialsOptions.PemRootCerts));

	// Additional configuration could be set with grpc::CreateCustomChannel()
	std::shared_ptr<grpc::Channel> Channel = grpc::CreateChannel(TCHAR_TO_UTF8(*Target), grpc::SslCredentials(SslOptions));

	ContentAddressableStorage.Reset(new FContentAddressableStorage(Channel));
	Execution.Reset(new FExecution(Channel));
}

FName FHordeExecutor::GetFName() const
{
	return FName("Horde");
}

FText FHordeExecutor::GetNameText() const
{
	return LOCTEXT("DefaultDisplayName", "Horde");
}

FText FHordeExecutor::GetDescriptionText() const
{
	return LOCTEXT("DefaultDisplayDesc", "Horde remote execution.");
}

bool FHordeExecutor::CanRemoteExecute() const
{
	return ContentAddressableStorage.IsValid() && Execution.IsValid();
}

#undef LOCTEXT_NAMESPACE
