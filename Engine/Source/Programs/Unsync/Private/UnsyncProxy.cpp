// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnsyncProxy.h"
#include "UnsyncCompression.h"
#include "UnsyncFile.h"
#include "UnsyncJupiter.h"

namespace unsync {

struct FUnsyncProtocolImpl : FRemoteProtocolBase
{
	FUnsyncProtocolImpl(const FRemoteDesc& InRemoteDesc, const FBlockRequestMap* InRequestMap, const FTlsClientSettings* TlsSettings);
	virtual ~FUnsyncProtocolImpl() override;
	virtual bool			 IsValid() const override;
	virtual FDownloadResult	 Download(const TArrayView<FNeedBlock> NeedBlocks, const FBlockDownloadCallback& CompletionCallback) override;
	virtual TResult<FBuffer> DownloadManifest(std::string_view ManifestName) override
	{
		return AppError(L"Manifests can't be downloaded from UNSYNC proxy.");
	};
	virtual void Invalidate() override;
	virtual bool Contains(const FDirectoryManifest& Manifest) override { return true; }	 // TODO: check files on the unsync proxy

	ESocketSecurity GetSocketSecurity() const;

	bool						 bIsConnetedToHost = false;
	std::unique_ptr<FSocketBase> SocketHandle;
};

FProxy::FProxy(const FRemoteDesc& RemoteDesc, const FBlockRequestMap* InRequestMap)
{
	UNSYNC_ASSERT(InRequestMap);

	FTlsClientSettings TlsSettings;
	TlsSettings.bVerifyCertificate = RemoteDesc.bTlsVerifyCertificate;
	TlsSettings.Subject			   = RemoteDesc.TlsSubject.empty() ? nullptr : RemoteDesc.TlsSubject.c_str();
	if (RemoteDesc.TlsCacert)
	{
		TlsSettings.CacertData = RemoteDesc.TlsCacert->Data();
		TlsSettings.CacertSize = RemoteDesc.TlsCacert->Size();
	}

	if (RemoteDesc.Protocol == EProtocolFlavor::Jupiter)
	{
		ProtocolImpl =
			std::unique_ptr<FRemoteProtocolBase>(new FJupiterProtocolImpl(RemoteDesc, InRequestMap, &TlsSettings, RemoteDesc.HttpHeaders));
	}
	else if (RemoteDesc.Protocol == EProtocolFlavor::Unsync)
	{
		ProtocolImpl = std::unique_ptr<FRemoteProtocolBase>(new FUnsyncProtocolImpl(RemoteDesc, InRequestMap, &TlsSettings));
	}
	else
	{
		UNSYNC_FATAL(L"Unknown remote protocol %d", (int)RemoteDesc.Protocol);
	}
}

FUnsyncProtocolImpl::FUnsyncProtocolImpl(const FRemoteDesc&		   RemoteDesc,
										 const FBlockRequestMap*   InRequestMap,
										 const FTlsClientSettings* TlsSettings)
: FRemoteProtocolBase(RemoteDesc, InRequestMap)
{
#if UNSYNC_USE_TLS
	if (RemoteDesc.bTlsEnable && TlsSettings)
	{
		FSocketHandle RawSocketHandle = SocketConnectTcp(RemoteDesc.HostAddress.c_str(), RemoteDesc.HostPort);
		if (RawSocketHandle)
		{
			FSocketTls* TlsSocket = new FSocketTls(RawSocketHandle, *TlsSettings);
			if (TlsSocket->IsTlsValid())
			{
				SocketHandle = std::unique_ptr<FSocketTls>(TlsSocket);
			}
			else
			{
				delete TlsSocket;
			}
		}
	}
#endif	// UNSYNC_USE_TLS

	if (!SocketHandle)
	{
		FSocketHandle RawSocketHandle = SocketConnectTcp(RemoteDesc.HostAddress.c_str(), RemoteDesc.HostPort);
		SocketHandle				  = std::unique_ptr<FSocketRaw>(new FSocketRaw(RawSocketHandle));
	}

	if (SocketHandle)
	{
		bIsConnetedToHost = [this]() {
			FHandshakePacket HandshakePacketTx;
			if (!SocketSendT(*SocketHandle, HandshakePacketTx))
			{
				UNSYNC_LOG(L"Failed to send the handshake packet");
				return false;
			}

			FHandshakePacket HandshakePacketRx;
			memset(&HandshakePacketRx, 0, sizeof(HandshakePacketRx));
			if (!SocketRecvT(*SocketHandle, HandshakePacketRx))
			{
				UNSYNC_LOG(L"Failed to receive the handshake packet");
				return false;
			}

			if (HandshakePacketRx.Magic != HandshakePacketTx.Magic || HandshakePacketRx.Protocol != HandshakePacketTx.Protocol ||
				HandshakePacketRx.Size != HandshakePacketTx.Size)
			{
				UNSYNC_LOG(L"Failed to receive the handshake packet");
				return false;
			}

			return true;
		}();
	}
}

FProxy::~FProxy()
{
}

bool
FProxy::Contains(const FDirectoryManifest& Manifest)
{
	return ProtocolImpl.get() && ProtocolImpl->Contains(Manifest);
}

bool
FProxy::IsValid() const
{
	return ProtocolImpl.get() && ProtocolImpl->IsValid();
}

TResult<FBuffer>
FProxy::DownloadManifest(std::string_view ManifestName)
{
	if (ProtocolImpl.get())
	{
		return ProtocolImpl->DownloadManifest(ManifestName);
	}
	else
	{
		return AppError(L"Server connection is invalid");
	}
}

FDownloadResult
FProxy::Download(const TArrayView<FNeedBlock> NeedBlocks, const FBlockDownloadCallback& CompletionCallback)
{
	if (ProtocolImpl.get())
	{
		return ProtocolImpl->Download(NeedBlocks, CompletionCallback);
	}
	else
	{
		return FDownloadResult(EDownloadRetryMode::Abort);
	}
}

bool
FUnsyncProtocolImpl::IsValid() const
{
	return bIsConnetedToHost && SocketHandle && SocketValid(*SocketHandle);
}

FDownloadResult
FUnsyncProtocolImpl::Download(const TArrayView<FNeedBlock> NeedBlocks, const FBlockDownloadCallback& CompletionCallback)
{
	if (!IsValid())
	{
		return FDownloadResult(EDownloadRetryMode::Abort);
	}

	const EStrongHashAlgorithmID StrongHasher = RequestMap->GetStrongHasher();

	std::unordered_set<FHash128> UniqueFileNamesMd5;

	std::vector<FBlockRequest> Requests;
	Requests.reserve(NeedBlocks.Size());

	for (const FNeedBlock& Block : NeedBlocks)
	{
		if (const FBlockRequest* Request = RequestMap->FindRequest(Block.Hash))
		{
			Requests.push_back(*Request);
			UniqueFileNamesMd5.insert(Request->FilenameMd5);
		}
	}

	std::vector<const std::string*> FileListUtf8;
	for (const FHash128& It : UniqueFileNamesMd5)
	{
		const std::string* Name = RequestMap->FindFile(It);
		if (Name)
		{
			FileListUtf8.push_back(Name);
		}
	}

	bool bOk = bIsConnetedToHost;

	// begin the command

	{
		FCommandPacket Packet;
		Packet.CommandId = COMMAND_ID_GET_BLOCKS;
		bOk &= SendStruct(*SocketHandle, Packet);
	}

	// send file list followed by requests
	if (bOk)
	{
		FBuffer FileListData;

		{
			FVectorStreamOut Writer(FileListData);

			for (const std::string* Str : FileListUtf8)
			{
				uint64 Len = Str->length();	 // 64 bit length, compatible with rust bincode
				Writer.WriteT(Len);
				Writer.Write(Str->c_str(), Len);
			}
		}

		FileListPacket FileListHeader;
		FileListHeader.DataSizeBytes = CheckedNarrow(FileListData.Size());
		FileListHeader.NumFiles		 = CheckedNarrow(FileListUtf8.size());

		bOk &= SendStruct(*SocketHandle, FileListHeader);
		bOk &= SendBuffer(*SocketHandle, FileListData);
	}

	uint64 BytesToDownload = 0;

	if (bOk)
	{
		FBuffer RequestData;

		{
			FVectorStreamOut Writer(RequestData);

			for (const FBlockRequest& It : Requests)
			{
				BytesToDownload += It.Size;
				Writer.WriteT(It);
			}
		}

		FBuffer RequestDataCompressed = Compress(RequestData.Data(), RequestData.Size());

		FRequestBlocksPacket RequestHeader;
		RequestHeader.CompressedSizeBytes	= CheckedNarrow(RequestDataCompressed.Size());
		RequestHeader.DecompressedSizeBytes = CheckedNarrow(RequestData.Size());
		RequestHeader.NumRequests			= CheckedNarrow(Requests.size());
		RequestHeader.StrongHashAlgorithmId = uint64(StrongHasher);

		bOk &= SendStruct(*SocketHandle, RequestHeader);
		bOk &= SendBuffer(*SocketHandle, RequestDataCompressed);
	}

	if (!bOk)
	{
		bIsConnetedToHost = false;
		return FDownloadResult(EDownloadRetryMode::Abort);
	}

	uint64 BytesDownloaded = 0;

	FBlockPacket BlockPacket;

	for (uint64 I = 0; I < (Requests.size() + 1) && bOk; ++I)
	{
		BlockPacket.DecompressedSize = 0;
		BlockPacket.Hash			 = {};

		uint32 PacketSize = 0;
		bOk &= SocketRecvT(*SocketHandle, PacketSize);
		bOk &= SocketRecvT(*SocketHandle, BlockPacket.Hash);
		bOk &= SocketRecvT(*SocketHandle, BlockPacket.DecompressedSize);

		uint64 CompressedDataSize = 0;
		bOk &= SocketRecvT(*SocketHandle, CompressedDataSize);

		BlockPacket.CompressedData.Resize(CompressedDataSize);

		bOk &= (SocketRecvAll(*SocketHandle, BlockPacket.CompressedData.Data(), BlockPacket.CompressedData.Size()) == CompressedDataSize);

		if (BlockPacket.Hash == FHash128{})	 // response is always terminated with an empty packet
		{
			break;
		}

		if (bOk)
		{
			FDownloadedBlock DownloadedBlock;
			DownloadedBlock.DecompressedSize = BlockPacket.DecompressedSize;
			DownloadedBlock.CompressedSize	 = BlockPacket.CompressedData.Size();
			DownloadedBlock.Data			 = BlockPacket.CompressedData.Data();
			CompletionCallback(DownloadedBlock, BlockPacket.Hash);
		}

		BytesDownloaded += BlockPacket.DecompressedSize;
	}

	if (!bOk)
	{
		SocketHandle	  = {};
		bIsConnetedToHost = false;
	}

	return ResultOk<EDownloadRetryMode>();
}

void
FUnsyncProtocolImpl::Invalidate()
{
	bIsConnetedToHost = false;
	SocketHandle	  = {};
}

ESocketSecurity
FUnsyncProtocolImpl::GetSocketSecurity() const
{
	if (SocketHandle)
	{
		return SocketHandle->Security;
	}
	else
	{
		return ESocketSecurity::None;
	}
}

FUnsyncProtocolImpl::~FUnsyncProtocolImpl()
{
	if (IsValid())
	{
		FCommandPacket Packet;
		Packet.CommandId = COMMAND_ID_DISCONNECT;
		SendStruct(*SocketHandle, Packet);
	}

	SocketHandle = {};

	bIsConnetedToHost = false;
}

void
FBlockRequestMap::AddFileBlocks(const fs::path& OriginalFilePath, const fs::path& ResolvedFilePath, const FFileManifest& FileManifest)
{
	UNSYNC_ASSERTF(StrongHasher != EStrongHashAlgorithmID::Invalid, L"Request map is not initialized");

	std::string OriginalFilePathUtf8 = ConvertWideToUtf8(OriginalFilePath.wstring());
	std::string ResolvedFilePathUtf8 = ConvertWideToUtf8(ResolvedFilePath.wstring());

	FHash128 OriginalNameHash = HashMd5Bytes((const uint8*)OriginalFilePathUtf8.c_str(), OriginalFilePathUtf8.length());
	FHash128 ResolvedNameHash = HashMd5Bytes((const uint8*)ResolvedFilePathUtf8.c_str(), ResolvedFilePathUtf8.length());

	auto FindResult = HashToFile.find(OriginalNameHash);
	if (FindResult == HashToFile.end())
	{
		HashToFile[OriginalNameHash] = uint32(FileListUtf8.size());
		HashToFile[ResolvedNameHash] = uint32(FileListUtf8.size());
		FileListUtf8.push_back(OriginalFilePathUtf8);
	}

	for (const FGenericBlock& Block : FileManifest.Blocks)
	{
		FBlockRequest Request;
		Request.FilenameMd5				 = OriginalNameHash;
		Request.BlockHash				 = Block.HashStrong.ToHash128();  // #wip-widehash
		Request.Offset					 = Block.Offset;
		Request.Size					 = Block.Size;
		BlockRequests[Request.BlockHash] = Request;

		if (!FileManifest.MacroBlocks.empty())
		{
			// TODO: could also just do the search in GetMacroBlockRequest() directly instead of pre-caching
			// TODO: since we know that blocks and macro-blocks are sorted, then we don't need a binary search here
			auto MacroBlockIt = std::lower_bound(FileManifest.MacroBlocks.begin(),
												 FileManifest.MacroBlocks.end(),
												 Block.Offset,
												 [](const FGenericBlock& A, uint64 B) { return (A.Offset + A.Size) < (B + 1); });

			if (MacroBlockIt != FileManifest.MacroBlocks.end())
			{
				const FGenericBlock& MacroBlock = *MacroBlockIt;
				UNSYNC_ASSERT(Block.Offset >= MacroBlock.Offset);
				UNSYNC_ASSERT(Block.Offset + Block.Size <= MacroBlock.Offset + MacroBlock.Size);

				FHash128 RequestKey = Block.HashStrong.ToHash128();

				if (MacroBlockRequests.find(RequestKey) == MacroBlockRequests.end())
				{
					FMacroBlockRequest MacroRequest;
					MacroRequest.Hash				  = MacroBlock.HashStrong;
					MacroRequest.Offset				  = Block.Offset - MacroBlock.Offset;
					MacroRequest.Size				  = Block.Size;
					MacroRequest.MacroBlockBaseOffset = MacroBlock.Offset;
					MacroRequest.MacroBlockTotalSize  = MacroBlock.Size;
					MacroBlockRequests[RequestKey]	  = MacroRequest;
				}
			}
			else
			{
				UNSYNC_FATAL(L"Found a block that does not belong to any macro block.");
			}
		}
	}
}

const FBlockRequest*
FBlockRequestMap::FindRequest(const FGenericHash& BlockHash) const
{
	FHash128 BlockHash128 = BlockHash.ToHash128();

	auto It = BlockRequests.find(BlockHash128);
	if (It == BlockRequests.end())
	{
		return nullptr;
	}
	else
	{
		return &It->second;
	}
}

const std::string*
FBlockRequestMap::FindFile(const FHash128& Hash) const
{
	auto It = HashToFile.find(Hash);
	if (It == HashToFile.end())
	{
		return nullptr;
	}
	else
	{
		return &FileListUtf8[It->second];
	}
}

FMacroBlockRequest
FBlockRequestMap::GetMacroBlockRequest(const FGenericHash& BlockHash) const
{
	FMacroBlockRequest Result = {};
	auto			   It	  = MacroBlockRequests.find(BlockHash.ToHash128());
	if (It != MacroBlockRequests.end())
	{
		Result = It->second;
	}
	return Result;
}

FProxyPool::FProxyPool(const FRemoteDesc& InRemoteDesc)
: ParallelDownloadSemaphore(InRemoteDesc.MaxConnections)
, RemoteDesc(InRemoteDesc)
, bValid(InRemoteDesc.IsValid())
{
}

std::unique_ptr<FProxy>
FProxyPool::Alloc()
{
	if (!bValid)
	{
		return nullptr;
	}

	std::lock_guard<std::mutex> LockGuard(Mutex);
	std::unique_ptr<FProxy>		Result;
	if (!Pool.empty())
	{
		std::swap(Pool.back(), Result);
		Pool.pop_back();
	}

	if (!Result || !Result->IsValid())
	{
		Result = std::make_unique<FProxy>(RemoteDesc, &RequestMap);
	}

	return Result;
}

void
FProxyPool::Dealloc(std::unique_ptr<FProxy>&& Proxy)
{
	if (Proxy.get() && Proxy->IsValid())
	{
		std::lock_guard<std::mutex> LockGuard(Mutex);
		Pool.push_back(std::move(Proxy));
	}
}

void
FProxyPool::Invalidate()
{
	bValid = false;
}

bool
FProxyPool::IsValid() const
{
	return bValid;
}

void
FProxyPool::BuildFileBlockRequests(const fs::path& OriginalFilePath, const fs::path& ResolvedFilePath, const FFileManifest& FileManifest)
{
	std::lock_guard<std::mutex> LockGuard(Mutex);
	RequestMap.AddFileBlocks(OriginalFilePath, ResolvedFilePath, FileManifest);
}

void
FProxyPool::InitRequestMap(EStrongHashAlgorithmID InStrongHasher)
{
	std::lock_guard<std::mutex> LockGuard(Mutex);
	RequestMap.Init(InStrongHasher);
}

}  // namespace unsync
