// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProtoConverter.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END


void ProtoConverter::ToProto(const FString& In, std::string& Out)
{
	Out.assign(TCHAR_TO_UTF8(*In));
}

void ProtoConverter::ToProto(const FDuration& In, google::protobuf::Duration& Out)
{
	Out.set_seconds(In.Seconds);
	Out.set_nanos(In.Nanos);
}

void ProtoConverter::ToProto(const FTimestamp& In, google::protobuf::Timestamp& Out)
{
	Out.set_seconds(In.Seconds);
	Out.set_nanos(In.Nanos);
}

void ProtoConverter::ToProto(const FDigest& In, build::bazel::remote::execution::v2::Digest& Out)
{
	ToProto(In.Hash, *Out.mutable_hash());
	Out.set_size_bytes(In.SizeBytes);
}

void ProtoConverter::ToProto(const FNodeProperty& In, build::bazel::remote::execution::v2::NodeProperty& Out)
{
	ToProto(In.Name, *Out.mutable_name());
	ToProto(In.Value, *Out.mutable_value());
}

void ProtoConverter::ToProto(const FNodeProperties& In, build::bazel::remote::execution::v2::NodeProperties& Out)
{
	for (const FNodeProperty& InProperty : In.Properties)
	{
		ToProto(InProperty, *Out.add_properties());
	}
	if (In.ModifiedTime.Nanos || In.ModifiedTime.Seconds)
	{
		ToProto(In.ModifiedTime, *Out.mutable_mtime());
	}
	if (In.UnixMode)
	{
		Out.mutable_unix_mode()->set_value(In.UnixMode);
	}
}

void ProtoConverter::ToProto(const FFileNode& In, build::bazel::remote::execution::v2::FileNode& Out)
{
	ToProto(In.Name, *Out.mutable_name());
	ToProto(In.Digest, *Out.mutable_digest());
	Out.set_is_executable(In.IsExecutable);
	if (In.NodeProperties.ModifiedTime.Seconds || In.NodeProperties.ModifiedTime.Nanos || !In.NodeProperties.Properties.IsEmpty() || In.NodeProperties.UnixMode)
	{
		ToProto(In.NodeProperties, *Out.mutable_node_properties());
	}
}

void ProtoConverter::ToProto(const FDirectoryNode& In, build::bazel::remote::execution::v2::DirectoryNode& Out)
{
	ToProto(In.Name, *Out.mutable_name());
	ToProto(In.Digest, *Out.mutable_digest());
}

void ProtoConverter::ToProto(const FSymlinkNode& In, build::bazel::remote::execution::v2::SymlinkNode& Out)
{
	ToProto(In.Name, *Out.mutable_name());
	ToProto(In.Target, *Out.mutable_target());
	if (In.NodeProperties.ModifiedTime.Seconds || In.NodeProperties.ModifiedTime.Nanos || !In.NodeProperties.Properties.IsEmpty() || In.NodeProperties.UnixMode)
	{
		ToProto(In.NodeProperties, *Out.mutable_node_properties());
	}
}

void ProtoConverter::ToProto(const FDirectory& In, build::bazel::remote::execution::v2::Directory& Out)
{
	for (const FFileNode& InFile : In.Files)
	{
		ToProto(InFile, *Out.add_files());
	}

	for (const FDirectoryNode& InDirectory : In.Directories)
	{
		ToProto(InDirectory, *Out.add_directories());
	}

	for (const FSymlinkNode& InSymlink : In.Symlinks)
	{
		ToProto(InSymlink, *Out.add_symlinks());
	}
	if (In.NodeProperties.ModifiedTime.Seconds || In.NodeProperties.ModifiedTime.Nanos || !In.NodeProperties.Properties.IsEmpty() || In.NodeProperties.UnixMode)
	{
		ToProto(In.NodeProperties, *Out.mutable_node_properties());
	}
}

void ProtoConverter::ToProto(const FPlatform& In, build::bazel::remote::execution::v2::Platform& Out)
{
	for (const FPlatform::FProperty& InProperty : In.Properties)
	{
		build::bazel::remote::execution::v2::Platform_Property* OutProperty = Out.add_properties();
		ToProto(InProperty.Name, *OutProperty->mutable_name());
		ToProto(InProperty.Value, *OutProperty->mutable_value());
	}
}

void ProtoConverter::ToProto(const FAction& In, build::bazel::remote::execution::v2::Action& Out)
{
	ToProto(In.CommandDigest, *Out.mutable_command_digest());
	ToProto(In.InputRootDigest, *Out.mutable_input_root_digest());
	if (In.Timeout.Seconds || In.Timeout.Nanos)
	{
		ToProto(In.Timeout, *Out.mutable_timeout());
	}
	Out.set_do_not_cache(In.DoNotCache);
	if (!In.Salt.IsEmpty())
	{
		ToProto(In.Salt, *Out.mutable_salt());
	}
	if (!In.Platform.Properties.IsEmpty())
	{
		ToProto(In.Platform, *Out.mutable_platform());
	}
}

void ProtoConverter::ToProto(const FCommand& In, build::bazel::remote::execution::v2::Command& Out)
{
	for (const FString& InArgument : In.Arguments)
	{
		ToProto(InArgument, *Out.add_arguments());
	}

	for (const FCommand::FEnvironmentVariable& InEnvironmentVariable : In.EnvironmentVariables)
	{
		build::bazel::remote::execution::v2::Command_EnvironmentVariable* OutEnvironmentVariable = Out.add_environment_variables();
		ToProto(InEnvironmentVariable.Name, *OutEnvironmentVariable->mutable_name());
		ToProto(InEnvironmentVariable.Value, *OutEnvironmentVariable->mutable_value());
	}

	for (const FString& InOutputPath : In.OutputPaths)
	{
		ToProto(InOutputPath, *Out.add_output_paths());
	}

	ToProto(In.WorkingDirectory, *Out.mutable_working_directory());

	for (const FString& InOutputNodeProperty : In.OutputNodeProperties)
	{
		ToProto(InOutputNodeProperty, *Out.add_output_node_properties());
	}
}

void ProtoConverter::FromProto(const std::string& In, FString& Out)
{
	Out = UTF8_TO_TCHAR(In.c_str());
}

void ProtoConverter::FromProto(const std::string& In, TArray<char>& Out)
{
	Out.Empty();
	if (In.size() > 0)
	{
		Out.AddUninitialized(In.size());
		memcpy(Out.GetData(), In.c_str(), In.size());
	}
}

void ProtoConverter::FromProto(const google::protobuf::Timestamp& In, FTimestamp& Out)
{
	Out.Seconds = In.seconds();
	Out.Nanos = In.nanos();
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::Digest& In, FDigest& Out)
{
	FromProto(In.hash(), Out.Hash);
	Out.SizeBytes = In.size_bytes();
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::NodeProperty& In, FNodeProperty& Out)
{
	FromProto(In.name(), Out.Name);
	FromProto(In.value(), Out.Value);
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::NodeProperties& In, FNodeProperties& Out)
{
	for (const build::bazel::remote::execution::v2::NodeProperty& InProperty : In.properties())
	{
		FNodeProperty OutProperty;
		FromProto(InProperty, OutProperty);
		Out.Properties.Add(MoveTemp(OutProperty));
	}
	FromProto(In.mtime(), Out.ModifiedTime);
	Out.UnixMode = In.unix_mode().value();
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::LogFile& In, FLogFile& Out)
{
	FromProto(In.digest(), Out.Digest);
	Out.HumanReadable = In.human_readable();
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::OutputFile& In, FOutputFile& Out)
{
	FromProto(In.path(), Out.Path);
	FromProto(In.digest(), Out.Digest);
	Out.IsExecutable = In.is_executable();
	FromProto(In.contents(), Out.Contents);
	FromProto(In.node_properties(), Out.NodeProperties);
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::OutputSymlink& In, FOutputSymlink& Out)
{
	FromProto(In.path(), Out.Path);
	FromProto(In.target(), Out.Target);
	FromProto(In.node_properties(), Out.NodeProperties);
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::OutputDirectory& In, FOutputDirectory& Out)
{
	FromProto(In.path(), Out.Path);
	FromProto(In.tree_digest(), Out.TreeDigest);
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::ExecutedActionMetadata& In, FExecutedActionMetadata& Out)
{
	FromProto(In.worker(), Out.Worker);
	FromProto(In.queued_timestamp(), Out.QueuedTimestamp);
	FromProto(In.worker_start_timestamp(), Out.WorkerStartTimestamp);
	FromProto(In.worker_completed_timestamp(), Out.WorkerCompletedTimestamp);
	FromProto(In.input_fetch_start_timestamp(), Out.InputFetchStartTimestamp);
	FromProto(In.input_fetch_completed_timestamp(), Out.InputFetchCompletedTimestamp);
	FromProto(In.execution_start_timestamp(), Out.ExecutionStartTimestamp);
	FromProto(In.execution_completed_timestamp(), Out.ExecutionCompletedTimestamp);
	FromProto(In.output_upload_start_timestamp(), Out.OutputUploadStartTimestamp);
	FromProto(In.output_upload_completed_timestamp(), Out.OutputUploadCompletedTimestamp);
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::ActionResult& In, FActionResult& Out)
{
	for (const build::bazel::remote::execution::v2::OutputFile& InOutputFile : In.output_files())
	{
		FOutputFile OutOutputFile;
		FromProto(InOutputFile, OutOutputFile);
		Out.OutputFiles.Add(MoveTemp(OutOutputFile));
	}

	for (const build::bazel::remote::execution::v2::OutputSymlink& InOutputSymlink : In.output_symlinks())
	{
		FOutputSymlink OutOutputSymlink;
		FromProto(InOutputSymlink, OutOutputSymlink);
		Out.OutputSymlinks.Add(MoveTemp(OutOutputSymlink));
	}

	for (const build::bazel::remote::execution::v2::OutputDirectory& InOutputDirectory : In.output_directories())
	{
		FOutputDirectory OutOutputDirectory;
		FromProto(InOutputDirectory, OutOutputDirectory);
		Out.OutputDirectories.Add(MoveTemp(OutOutputDirectory));
	}

	Out.ExitCode = In.exit_code();
	FromProto(In.stdout_raw(), Out.StdoutRaw);
	FromProto(In.stdout_digest(), Out.StdoutDigest);
	FromProto(In.stderr_raw(), Out.StderrRaw);
	FromProto(In.stderr_digest(), Out.StderrDigest);
	FromProto(In.execution_metadata(), Out.ExecutionMetadata);
}

void ProtoConverter::ToProto(const FExecuteRequest& In, build::bazel::remote::execution::v2::ExecuteRequest& Out)
{
	if (!In.InstanceName.IsEmpty())
	{
		ToProto(In.InstanceName, *Out.mutable_instance_name());
	}
	Out.set_skip_cache_lookup(In.SkipCacheLookup);
	ToProto(In.ActionDigest, *Out.mutable_action_digest());
	if (In.ExecutionPolicy.Priority)
	{
		Out.mutable_execution_policy()->set_priority(In.ExecutionPolicy.Priority);
	}
	if (In.ResultsCachePolicy.Priority)
	{
		Out.mutable_results_cache_policy()->set_priority(In.ResultsCachePolicy.Priority);
	}
}

void ProtoConverter::ToProto(const FFindMissingBlobsRequest& In, build::bazel::remote::execution::v2::FindMissingBlobsRequest& Out)
{
	if (!In.InstanceName.IsEmpty())
	{
		ToProto(In.InstanceName, *Out.mutable_instance_name());
	}
	for	(const FDigest& InDigest : In.BlobDigests)
	{
		ToProto(InDigest, *Out.add_blob_digests());
	}
}

void ProtoConverter::ToProto(const FBatchUpdateBlobsRequest& In, build::bazel::remote::execution::v2::BatchUpdateBlobsRequest& Out)
{
	if (!In.InstanceName.IsEmpty())
	{
		ToProto(In.InstanceName, *Out.mutable_instance_name());
	}
	for (const FBatchUpdateBlobsRequest::FRequest& InRequest : In.Requests)
	{
		build::bazel::remote::execution::v2::BatchUpdateBlobsRequest_Request* OutRequest = Out.add_requests();
		ToProto(InRequest.Digest, *OutRequest->mutable_digest());
		OutRequest->mutable_data()->assign(InRequest.Data.GetData(), InRequest.Data.Num());
	}
}

void ProtoConverter::ToProto(const FBatchReadBlobsRequest& In, build::bazel::remote::execution::v2::BatchReadBlobsRequest& Out)
{
	if (!In.InstanceName.IsEmpty())
	{
		ToProto(In.InstanceName, *Out.mutable_instance_name());
	}
	for (const FDigest& InDigest : In.Digests)
	{
		ToProto(InDigest, *Out.add_digests());
	}
}

void ProtoConverter::FromProto(const google::rpc::Status& In, FStatus& Out)
{
	Out.Code = (EStatusCode)In.code();
	FromProto(In.message(), Out.Message);
	Out.Details.Empty();
}

void ProtoConverter::FromProto(const grpc::Status& In, FStatus& Out)
{
	Out.Code = (EStatusCode)In.error_code();
	FromProto(In.error_message(), Out.Message);
	FromProto(In.error_details(), Out.Details);
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::FindMissingBlobsResponse& In, FFindMissingBlobsResponse& Out)
{
	for (const build::bazel::remote::execution::v2::Digest& InDigest : In.missing_blob_digests())
	{
		FDigest OutDigest;
		FromProto(InDigest, OutDigest);
		Out.MissingBlobDigests.Add(MoveTemp(OutDigest));
	}
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::BatchUpdateBlobsResponse& In, FBatchUpdateBlobsResponse& Out)
{
	for (const build::bazel::remote::execution::v2::BatchUpdateBlobsResponse_Response& InResponse : In.responses())
	{
		FBatchUpdateBlobsResponse::FResponse OutResponse;
		FromProto(InResponse.digest(), OutResponse.Digest);
		FromProto(InResponse.status(), OutResponse.Status);
	}
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::BatchReadBlobsResponse& In, FBatchReadBlobsResponse& Out)
{
	for (const build::bazel::remote::execution::v2::BatchReadBlobsResponse_Response& InResponse : In.responses())
	{
		FBatchReadBlobsResponse::FResponse OutResponse;
		FromProto(InResponse.digest(), OutResponse.Digest);
		OutResponse.Data.Empty();
		OutResponse.Data.Append(InResponse.data().c_str(), InResponse.data().size());
		FromProto(InResponse.status(), OutResponse.Status);
		Out.Responses.Add(MoveTemp(OutResponse));
	}
}

void ProtoConverter::FromProto(const build::bazel::remote::execution::v2::ExecuteResponse& In, FExecuteResponse& Out)
{
	FromProto(In.result(), Out.Result);
	Out.CachedResult = In.cached_result();
	FromProto(In.status(), Out.Status);

	for (const google::protobuf::MapPair<std::string, build::bazel::remote::execution::v2::LogFile>& InServerLog : In.server_logs())
	{
		FString Key;
		FLogFile Value;
		FromProto(InServerLog.first, Key);
		FromProto(InServerLog.second, Value);
		Out.ServerLogs.Add(MoveTemp(Key), MoveTemp(Value));
	}

	FromProto(In.message(), Out.Message);
}

bool ProtoConverter::ToDigest(const TArray<char>& Data, FDigest& OutDigest)
{
	unsigned char Sha256Buffer[SHA256_DIGEST_LENGTH];

	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, Data.GetData(), Data.Num());
	SHA256_Final(Sha256Buffer, &sha256);

	// GRPC requires lowercase hex
	OutDigest.Hash = FString::FromHexBlob(Sha256Buffer, SHA256_DIGEST_LENGTH).ToLower();
	OutDigest.SizeBytes = Data.Num();
	return true;
}

bool ProtoConverter::ToBlob(const google::protobuf::Message& InMessage, TArray<char>& OutData, FDigest& OutDigest)
{
	OutData.Empty();
	OutData.AddUninitialized(InMessage.ByteSizeLong());
	if (!InMessage.SerializeToArray(OutData.GetData(), OutData.Num()))
	{
		OutData.Empty();
		return false;
	}
	return ToDigest(OutData, OutDigest);
}

bool ProtoConverter::ToBlob(const FDirectory& InDirectory, TArray<char>& OutData, FDigest& OutDigest)
{
	// Convert to temporary proto Directory
	build::bazel::remote::execution::v2::Directory OutDirectory;
	ToProto(InDirectory, OutDirectory);
	return ToBlob(OutDirectory, OutData, OutDigest);
}

bool ProtoConverter::ToBlob(const FCommand& InCommand, TArray<char>& OutData, FDigest& OutDigest)
{
	// Convert to temporary proto Action
	build::bazel::remote::execution::v2::Command OutCommand;
	ToProto(InCommand, OutCommand);
	return ToBlob(OutCommand, OutData, OutDigest);
}

bool ProtoConverter::ToBlob(const FAction& InAction, TArray<char>& OutData, FDigest& OutDigest)
{
	// Convert to temporary proto Action
	build::bazel::remote::execution::v2::Action OutAction;
	ToProto(InAction, OutAction);
	return ToBlob(OutAction, OutData, OutDigest);
}

