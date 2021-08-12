// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <string>

namespace google
{
	namespace protobuf
	{
		class Duration;
		class Message;
		class Timestamp;
	}
	namespace rpc
	{
		class Status;
	}
}

namespace grpc
{
	class Status;
}

namespace build
{
	namespace bazel
	{
		namespace remote
		{
			namespace execution
			{
				namespace v2
				{
					class Action;
					class ActionResult;
					class BatchReadBlobsRequest;
					class BatchReadBlobsResponse;
					class BatchUpdateBlobsRequest;
					class BatchUpdateBlobsResponse;
					class Command;
					class Digest;
					class Directory;
					class DirectoryNode;
					class ExecutedActionMetadata;
					class ExecuteRequest;
					class ExecuteResponse;
					class FileNode;
					class FindMissingBlobsRequest;
					class FindMissingBlobsResponse;
					class LogFile;
					class NodeProperties;
					class NodeProperty;
					class OutputDirectory;
					class OutputFile;
					class OutputSymlink;
					class Platform;
					class SymlinkNode;
				}
			}
		}
	}
}

struct FAction;
struct FActionResult;
struct FBatchReadBlobsRequest;
struct FBatchReadBlobsResponse;
struct FBatchUpdateBlobsRequest;
struct FBatchUpdateBlobsResponse;
struct FCommand;
struct FDigest;
struct FDirectory;
struct FDirectoryNode;
struct FExecutedActionMetadata;
struct FExecuteRequest;
struct FExecuteResponse;
struct FFileNode;
struct FFindMissingBlobsRequest;
struct FFindMissingBlobsResponse;
struct FLogFile;
struct FNodeProperties;
struct FNodeProperty;
struct FOutputDirectory;
struct FOutputFile;
struct FOutputSymlink;
struct FPlatform;
struct FSymlinkNode;
struct FStatus;
struct FIoHash;


class ProtoConverter
{
private:
	static void ToProto(const FString& In, std::string& Out);
	static void ToProto(const FIoHash& In, std::string& Out);

	static void ToProto(const FTimespan& In, google::protobuf::Duration& Out);
	static void ToProto(const FDateTime& In, google::protobuf::Timestamp& Out);

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
	static void FromProto(const std::string& In, FIoHash& Out);
	static void FromProto(const std::string& In, TArray<uint8>& Out);

	static void FromProto(const google::protobuf::Timestamp& In, FDateTime& Out);

	static void FromProto(const build::bazel::remote::execution::v2::Digest& In, FDigest& Out);

	static void FromProto(const build::bazel::remote::execution::v2::NodeProperty& In, FNodeProperty& Out);
	static void FromProto(const build::bazel::remote::execution::v2::NodeProperties& In, FNodeProperties& Out);

	static void FromProto(const build::bazel::remote::execution::v2::LogFile& In, FLogFile& Out);
	static void FromProto(const build::bazel::remote::execution::v2::OutputFile& In, FOutputFile& Out);
	static void FromProto(const build::bazel::remote::execution::v2::OutputSymlink& In, FOutputSymlink& Out);
	static void FromProto(const build::bazel::remote::execution::v2::OutputDirectory& In, FOutputDirectory& Out);
	static void FromProto(const build::bazel::remote::execution::v2::ExecutedActionMetadata& In, FExecutedActionMetadata& Out);
	static void FromProto(const build::bazel::remote::execution::v2::ActionResult& In, FActionResult& Out);

	static bool ToBlob(const google::protobuf::Message& InMessage, TArray<uint8>& OutData, FDigest& OutDigest);

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

	static bool ToDigest(const TArray<uint8>& InData, FDigest& OutDigest);

	static bool ToBlob(const FDirectory& InDirectory, TArray<uint8>& OutData, FDigest& OutDigest);
	static bool ToBlob(const FCommand& InCommand, TArray<uint8>& OutData, FDigest& OutDigest);
	static bool ToBlob(const FAction& InAction, TArray<uint8>& OutData, FDigest& OutDigest);
};
