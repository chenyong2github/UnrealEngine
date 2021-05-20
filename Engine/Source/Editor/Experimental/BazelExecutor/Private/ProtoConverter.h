// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Messages.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
#include "build\bazel\remote\execution\v2\remote_execution.pb.h"
#include "build\bazel\remote\execution\v2\remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


class ProtoConverter
{
private:
	static void ToProto(const FString& In, std::string& Out);

	static void ToProto(const FDuration& In, google::protobuf::Duration& Out);
	static void ToProto(const FTimestamp& In, google::protobuf::Timestamp& Out);

	static void ToProto(const FDigest& In, build::bazel::remote::execution::v2::Digest& Out);

	static void ToProto(const FNodeProperty& In, build::bazel::remote::execution::v2::NodeProperty& Out);
	static void ToProto(const FNodeProperties& In, build::bazel::remote::execution::v2::NodeProperties& Out);

	static void ToProto(const FFileNode& In, build::bazel::remote::execution::v2::FileNode& Out);
	static void ToProto(const FDirectoryNode& In, build::bazel::remote::execution::v2::DirectoryNode& Out);
	static void ToProto(const FSymlinkNode& In, build::bazel::remote::execution::v2::SymlinkNode& Out);
	static void ToProto(const FDirectory& In, build::bazel::remote::execution::v2::Directory& Out);

	static void ToProto(const FPlatform& In, build::bazel::remote::execution::v2::Platform& Out);
	static void ToProto(const FAction& In, build::bazel::remote::execution::v2::Action& Out);
	static void ToProto(const FCommand& In, build::bazel::remote::execution::v2::Command& Out);

	static void FromProto(const std::string& In, FString& Out);
	static void FromProto(const std::string& In, TArray<char>& Out);

	static void FromProto(const google::protobuf::Timestamp& In, FTimestamp& Out);

	static void FromProto(const build::bazel::remote::execution::v2::Digest& In, FDigest& Out);

	static void FromProto(const build::bazel::remote::execution::v2::NodeProperty& In, FNodeProperty& Out);
	static void FromProto(const build::bazel::remote::execution::v2::NodeProperties& In, FNodeProperties& Out);

	static void FromProto(const build::bazel::remote::execution::v2::LogFile& In, FLogFile& Out);
	static void FromProto(const build::bazel::remote::execution::v2::OutputFile& In, FOutputFile& Out);
	static void FromProto(const build::bazel::remote::execution::v2::OutputSymlink& In, FOutputSymlink& Out);
	static void FromProto(const build::bazel::remote::execution::v2::OutputDirectory& In, FOutputDirectory& Out);
	static void FromProto(const build::bazel::remote::execution::v2::ExecutedActionMetadata& In, FExecutedActionMetadata& Out);
	static void FromProto(const build::bazel::remote::execution::v2::ActionResult& In, FActionResult& Out);

public:
	static void ToProto(const FExecuteRequest& In, build::bazel::remote::execution::v2::ExecuteRequest& Out);
	static void ToProto(const FFindMissingBlobsRequest& In, build::bazel::remote::execution::v2::FindMissingBlobsRequest& Out);
	static void ToProto(const FBatchUpdateBlobsRequest& In, build::bazel::remote::execution::v2::BatchUpdateBlobsRequest& Out);
	static void ToProto(const FBatchReadBlobsRequest& In, build::bazel::remote::execution::v2::BatchReadBlobsRequest& Out);

	static void FromProto(const google::rpc::Status& In, FStatus& Out);
	static void FromProto(const grpc::Status& In, FStatus& Out);
	static void FromProto(const build::bazel::remote::execution::v2::FindMissingBlobsResponse& In, FFindMissingBlobsResponse& Out);
	static void FromProto(const build::bazel::remote::execution::v2::BatchUpdateBlobsResponse& In, FBatchUpdateBlobsResponse& Out);
	static void FromProto(const build::bazel::remote::execution::v2::BatchReadBlobsResponse& In, FBatchReadBlobsResponse& Out);
	static void FromProto(const build::bazel::remote::execution::v2::ExecuteResponse& In, FExecuteResponse& Out);

	static bool ToDigest(const TArray<char>& Data, FDigest& OutDigest);

	static bool ToBlob(const google::protobuf::Message& InMessage, TArray<char>& OutData, FDigest& OutDigest);

	static bool ToBlob(const FDirectory& InDirectory, TArray<char>& OutData, FDigest& OutDigest);
	static bool ToBlob(const FCommand& InCommand, TArray<char>& OutData, FDigest& OutDigest);
	static bool ToBlob(const FAction& InAction, TArray<char>& OutData, FDigest& OutDigest);
};

