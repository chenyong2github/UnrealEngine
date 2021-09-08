// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Containers/Array.h"
#include "Containers/StringView.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "IO/IoDispatcher.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Templates/UniquePtr.h"

class FCbPackage;
class FCbObject;

namespace UE {
	namespace Zen {
		struct FZenHttpRequestPool;
	}

/**
 * HTTP protocol implementation of Zen Store client interface
 */
class IOSTOREUTILITIES_API FZenStoreHttpClient
{
public:
	FZenStoreHttpClient(const FStringView InHostName, uint16 InPort);
	~FZenStoreHttpClient();

	void Initialize(FStringView InProjectId, 
					FStringView InOplogId, 
					FStringView ServerRoot, 
					FStringView EngineRoot, 
					FStringView ProjectRoot);
	void EstablishWritableOpLog(FStringView InProjectId, FStringView InOplogId, bool bFullBuild);

	void InitializeReadOnly(FStringView InProjectId, FStringView InOplogId);

	bool IsConnected() const;

	void StartBuildPass();
	TIoStatusOr<uint64> EndBuildPass(FCbPackage OpEntry);

	TFuture<TIoStatusOr<uint64>> AppendOp(FCbPackage OpEntry);

	TIoStatusOr<uint64> GetChunkSize(const FIoChunkId& Id);
	TIoStatusOr<FIoBuffer> ReadChunk(const FIoChunkId& Id, uint64 Offset = 0, uint64 Size = ~0ull);
	TIoStatusOr<FIoBuffer> ReadOpLogAttachment(FStringView Id, uint64 Offset = 0, uint64 Size = ~0ull);

	TFuture<TIoStatusOr<FCbObject>> GetOplog();
	TFuture<TIoStatusOr<FCbObject>> GetFiles();

	static const UTF8CHAR* FindOrAddAttachmentId(FUtf8StringView AttachmentText);
	static const UTF8CHAR* FindAttachmentId(FUtf8StringView AttachmentText);

private:
	TIoStatusOr<FIoBuffer> ReadOpLogUri(FStringBuilderBase& ChunkUri, uint64 Offset, uint64 Size);

	FString	HostName;
	uint16 Port;
	TUniquePtr<Zen::FZenHttpRequestPool> RequestPool;
	FString OplogPath;
	FString OplogNewEntryPath;
	FString OplogPrepNewEntryPath;
	FString TempDirPath;
	uint64 StandaloneThresholdBytes = 1 * 1024 * 1024;
	bool bAllowRead = false;
	bool bAllowEdit = false;
	bool bConnectionSucceeded = false;
};

}
