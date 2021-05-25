// Copyright Epic Games, Inc. All Rights Reserved.

#include "BazelExecutor.h"

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

#define LOCTEXT_NAMESPACE "BazelExecutor"


void FBazelExecutor::Initialize(const FSettings& Settings)
{
	ContentAddressableStorage.Reset();
	Execution.Reset();

	grpc::ChannelArguments ChannelArguments;
	ChannelArguments.SetMaxSendMessageSize(Settings.MaxSendMessageSize);
	ChannelArguments.SetMaxReceiveMessageSize(Settings.MaxReceiveMessageSize);

	grpc::SslCredentialsOptions ContentAddressableStorageSslOptions;
	ContentAddressableStorageSslOptions.pem_cert_chain.assign(TCHAR_TO_UTF8(*Settings.ContentAddressableStoragePemCertificateChain));
	ContentAddressableStorageSslOptions.pem_private_key.assign(TCHAR_TO_UTF8(*Settings.ContentAddressableStoragePemPrivateKey));
	ContentAddressableStorageSslOptions.pem_root_certs.assign(TCHAR_TO_UTF8(*Settings.ContentAddressableStoragePemRootCertificates));

	std::shared_ptr<grpc::ChannelCredentials> ContentAddressableStorageChannelCredentials = grpc::SslCredentials(grpc::SslCredentialsOptions(ContentAddressableStorageSslOptions));

	std::shared_ptr<grpc::Channel> ContentAddressableStorageChannel = grpc::CreateCustomChannel(TCHAR_TO_UTF8(*Settings.ContentAddressableStorageTarget), ContentAddressableStorageChannelCredentials, ChannelArguments);
	ContentAddressableStorage.Reset(new FContentAddressableStorage(ContentAddressableStorageChannel, Settings.ContentAddressableStorageHeaders));

	grpc::SslCredentialsOptions ExecutionSslOptions;
	ExecutionSslOptions.pem_cert_chain.assign(TCHAR_TO_UTF8(*Settings.ExecutionPemCertificateChain));
	ExecutionSslOptions.pem_private_key.assign(TCHAR_TO_UTF8(*Settings.ExecutionPemPrivateKey));
	ExecutionSslOptions.pem_root_certs.assign(TCHAR_TO_UTF8(*Settings.ExecutionPemRootCertificates));

	std::shared_ptr<grpc::ChannelCredentials> ExecutionChannelCredentials = grpc::SslCredentials(grpc::SslCredentialsOptions(ExecutionSslOptions));

	std::shared_ptr<grpc::Channel> ExecutorChannel = grpc::CreateCustomChannel(TCHAR_TO_UTF8(*Settings.ExecutionTarget), ExecutionChannelCredentials, ChannelArguments);
	Execution.Reset(new FExecution(ExecutorChannel, Settings.ExecutionHeaders));
}

FName FBazelExecutor::GetFName() const
{
	return FName("Bazel");
}

FText FBazelExecutor::GetNameText() const
{
	return LOCTEXT("DefaultDisplayName", "Bazel");
}

FText FBazelExecutor::GetDescriptionText() const
{
	return LOCTEXT("DefaultDisplayDesc", "Bazel remote execution.");
}

bool FBazelExecutor::CanRemoteExecute() const
{
	return ContentAddressableStorage.IsValid() && Execution.IsValid();
}

#undef LOCTEXT_NAMESPACE
