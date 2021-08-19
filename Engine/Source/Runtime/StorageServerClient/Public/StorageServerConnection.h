// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/IoContainerId.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinarySerialization.h"

#if !UE_BUILD_SHIPPING

class ISocketSubsystem;
class FInternetAddr;
class FIoChunkId;
class FSocket;
class FStorageServerConnection;

class FStorageServerRequest
	: public FArchive
{
protected:
	friend class FStorageServerConnection;

	FStorageServerRequest(FAnsiStringView Verb, FAnsiStringView Resource, FAnsiStringView Hostname);
	FSocket* Send(FStorageServerConnection& Owner);
	virtual void Serialize(void* V, int64 Length) override;

	TAnsiStringBuilder<512> HeaderBuffer;
	TArray<uint8, TInlineAllocator<1024>> BodyBuffer;
};

class FStorageServerResponse
	: public FArchive
{
public:
	~FStorageServerResponse()
	{
		if (Socket)
		{
			ReleaseSocket(false);
		}
	}

	bool IsOk() const
	{
		return bIsOk;
	}

	const int32 GetErrorCode() const
	{
		return ErrorCode;
	}

	const FString& GetErrorMessage() const
	{
		return ErrorMessage;
	}

	int64 TotalSize() override
	{
		return ContentLength;
	}

	int64 Tell() override
	{
		return Position;
	}

	void Serialize(void* V, int64 Length) override;

	FCbObject GetResponseObject()
	{
		FCbField Payload = LoadCompactBinary(*this);
		return Payload.AsObject();
	}

private:
	friend class FStorageServerConnection;
	friend class FStorageServerChunkBatchRequest;

	FStorageServerResponse(class FStorageServerConnection& Owner, FSocket& Socket);
	void ReleaseSocket(bool bKeepAlive);

	FStorageServerConnection& Owner;
	FSocket* Socket = nullptr;
	uint64 ContentLength = 0;
	uint64 Position = 0;
	int32 ErrorCode;
	FString ErrorMessage;
	bool bIsOk = false;
};

class FStorageServerChunkBatchRequest
	: private FStorageServerRequest
{
public:
	STORAGESERVERCLIENT_API FStorageServerChunkBatchRequest& AddChunk(const FIoChunkId& ChunkId, int64 Offset, int64 Size);
	STORAGESERVERCLIENT_API bool Issue(TFunctionRef<void(uint32 ChunkCount, uint32* ChunkIndices, uint64* ChunkSizes, FStorageServerResponse& ChunkDataStream)> OnResponse);

private:
	friend class FStorageServerConnection;

	FStorageServerChunkBatchRequest(FStorageServerConnection& Owner, FAnsiStringView Resource, FAnsiStringView Hostname);

	FStorageServerConnection& Owner;
	int32 ChunkCountOffset = 0;
};

class FStorageServerConnection
{
public:
	STORAGESERVERCLIENT_API FStorageServerConnection();
	STORAGESERVERCLIENT_API ~FStorageServerConnection();

	STORAGESERVERCLIENT_API bool Initialize(TArrayView<const FString> HostAddresses, int32 Port, const TCHAR* ProjectNameOverride = nullptr, const TCHAR* PlatformNameOverride = nullptr);

	STORAGESERVERCLIENT_API void FileManifestRequest(TFunctionRef<void(FIoChunkId Id, FStringView Path)> Callback);
	STORAGESERVERCLIENT_API int64 ChunkSizeRequest(const FIoChunkId& ChunkId);
	STORAGESERVERCLIENT_API bool ReadChunkRequest(const FIoChunkId& ChunkId, uint64 Offset, uint64 Size, TFunctionRef<void(FStorageServerResponse&)> OnResponse);
	STORAGESERVERCLIENT_API FStorageServerChunkBatchRequest NewChunkBatchRequest();

	STORAGESERVERCLIENT_API FString GetHostAddr() const;

private:
	friend class FStorageServerRequest;
	friend class FStorageServerResponse;
	friend class FStorageServerChunkBatchRequest;

	int32 HandshakeRequest(TArrayView<const TSharedPtr<FInternetAddr>> HostAddresses);
	FSocket* AcquireSocket();
	void ReleaseSocket(FSocket* Socket, bool bKeepAlive);

	ISocketSubsystem& SocketSubsystem;
	TAnsiStringBuilder<1024> OplogPath;
	TSharedPtr<FInternetAddr> ServerAddr;
	TAnsiStringBuilder<1024> Hostname;
	FCriticalSection SocketPoolCritical;
	TArray<FSocket*> SocketPool;
};

#endif
