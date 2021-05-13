// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


// namespace google.protobuf
// https://developers.google.com/protocol-buffers/docs/reference/google.protobuf
//

struct FDuration
{
	int64 Seconds = 0;

	int32 Nanos = 0;
};

struct FTimestamp
{
	int64 Seconds = 0;

	int32 Nanos = 0;
};

// namespace grpc
// https://grpc.github.io/grpc/cpp/namespacegrpc.html
//

enum class EStatusCode
{
	OK = 0,
	CANCELLED = 1,
	UNKNOWN = 2,
	INVALID_ARGUMENT = 3,
	DEADLINE_EXCEEDED = 4,
	NOT_FOUND = 5,
	ALREADY_EXISTS = 6,
	PERMISSION_DENIED = 7,
	RESOURCE_EXHAUSTED = 8,
	FAILED_PRECONDITION = 9,
	ABORTED = 10,
	OUT_OF_RANGE = 11,
	UNIMPLEMENTED = 12,
	INTERNAL = 13,
	UNAVAILABLE = 14,
	DATA_LOSS = 15,
	UNAUTHENTICATED = 16
};

// Merged with google.rpc.Status
struct FStatus
{
	EStatusCode Code = EStatusCode::OK;
	FString Message;
	FString Details;

	inline bool Ok() const {
		return Code == EStatusCode::OK;
	}
};

// namespace build.bazel.remote.execution.v2
// https://github.com/bazelbuild/remote-apis/blob/master/build/bazel/remote/execution/v2/remote_execution.proto
//

struct FDigest
{
	FString Hash;

	int64 SizeBytes = 0;
};

struct FNodeProperty
{
	FString Name;

	FString Value;
};

struct FNodeProperties
{
	TArray<FNodeProperty> Properties;

	FTimestamp ModifiedTime;

	uint32 UnixMode = 0;
};

struct FFileNode
{
	FString Name;

	FDigest Digest;

	bool IsExecutable = false;

	FNodeProperties NodeProperties;
};

struct FDirectoryNode
{
	FString Name;

	FDigest Digest;
};

struct FSymlinkNode
{
	FString Name;
	FString Target;

	FNodeProperties NodeProperties;
};

struct FDirectory
{
	TArray<FFileNode> Files;

	TArray<FDirectoryNode> Directories;

	TArray<FSymlinkNode> Symlinks;

	FNodeProperties NodeProperties;
};

struct FPlatform
{
	struct FProperty
	{
		FString Name;
		FString Value;
	};
	TArray<FProperty> Properties;
};

struct FAction
{
	FDigest CommandDigest;

	FDigest InputRootDigest;

	FDuration Timeout;

	bool DoNotCache = false;

	FString Salt;

	FPlatform Platform;
};

struct FCommand
{
	struct FEnvironmentVariable
	{
		FString Name;
		FString Value;
	};

	TArray<FString> Arguments;

	TArray<FEnvironmentVariable> EnvironmentVariables;

	TArray<FString> OutputPaths;

	FString WorkingDirectory;

	TArray<FString> OutputNodeProperties;
};

struct FExecutionPolicy
{
	int32 Priority = 0;
};

struct FResultsCachePolicy
{
	int32 Priority = 0;
};

struct FExecuteRequest
{
	FString InstanceName;

	bool SkipCacheLookup = false;

	FDigest ActionDigest;

	FExecutionPolicy ExecutionPolicy;

	FResultsCachePolicy ResultsCachePolicy;
};

struct FFindMissingBlobsRequest
{
	FString InstanceName;

	TArray<FDigest> BlobDigests;
};

struct FFindMissingBlobsResponse
{
	TArray<FDigest> MissingBlobDigests;
};

struct FBatchUpdateBlobsRequest
{
	struct FRequest
	{
		FDigest Digest;
		TArray<char> Data;
	};

	FString InstanceName;

	TArray<FRequest> Requests;
};

struct FBatchUpdateBlobsResponse
{
	struct FResponse
	{
		FDigest Digest;
		FStatus Status;
	};

	TArray<FResponse> Responses;
};

struct FBatchReadBlobsRequest
{
	FString InstanceName;

	TArray<FDigest> Digests;
};

struct FBatchReadBlobsResponse
{
	struct FResponse
	{
		FDigest Digest;
		TArray<char> Data;
		FStatus Status;
	};

	TArray<FResponse> Responses;
};

struct FOutputFile
{
	FString Path;
	FDigest Digest;
	bool IsExecutable;
	TArray<char> Contents;
	FNodeProperties NodeProperties;
};

struct FOutputSymlink
{
	FString Path;
	FString Target;
	FNodeProperties NodeProperties;
};

struct FOutputDirectory
{
	FString Path;
	FDigest TreeDigest;
};

struct FTree
{
	FDirectory Root;
	TArray<FDirectory> Children;
};

struct FExecutedActionMetadata
{
	FString Worker;
	FTimestamp QueuedTimestamp;
	FTimestamp WorkerStartTimestamp;
	FTimestamp WorkerCompletedTimestamp;
	FTimestamp InputFetchStartTimestamp;
	FTimestamp InputFetchCompletedTimestamp;
	FTimestamp ExecutionStartTimestamp;
	FTimestamp ExecutionCompletedTimestamp;
	FTimestamp OutputUploadStartTimestamp;
	FTimestamp OutputUploadCompletedTimestamp;
	//repeated google.protobuf.Any AuxiliaryMetadata = 11;
};

struct FActionResult
{
	TArray<FOutputFile> OutputFiles;
	TArray<FOutputSymlink> OutputSymlinks;
	TArray<FOutputDirectory> OutputDirectories;
	int32 ExitCode = 0;
	TArray<char> StdoutRaw;
	FDigest StdoutDigest;
	TArray<char> StderrRaw;
	FDigest StderrDigest;
	FExecutedActionMetadata ExecutionMetadata;
};

struct FLogFile
{
	FDigest Digest;
	bool HumanReadable;
};

struct FExecuteResponse
{
	FActionResult Result;
	bool CachedResult = false;
	FStatus Status;
	TMap<FString, FLogFile> ServerLogs;
	FString Message;
};
