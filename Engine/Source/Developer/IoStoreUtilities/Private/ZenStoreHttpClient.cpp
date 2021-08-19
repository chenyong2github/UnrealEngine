// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenStoreHttpClient.h"

#if PLATFORM_WINDOWS

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "curl/curl.h"
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoHash.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "ZenServerHttp.h"

DEFINE_LOG_CATEGORY_STATIC(LogZenStore, Log, All);

namespace UE { 

FZenStoreHttpClient::FZenStoreHttpClient(const FStringView InHostName, uint16 InPort)
: HostName(InHostName)
, Port(InPort)
{
	TStringBuilder<64> Uri;
	Uri.AppendAnsi("http://");
	Uri.Append(InHostName);
	Uri.AppendAnsi(":");
	Uri << InPort;

	RequestPool = MakeUnique<Zen::FZenHttpRequestPool>(Uri, 32);
}

FZenStoreHttpClient::~FZenStoreHttpClient()
{
}

void 
FZenStoreHttpClient::Initialize(FStringView InProjectId, 
	FStringView InOplogId, 
	FStringView ServerRoot,
	FStringView EngineRoot,
	FStringView ProjectRoot,
	bool		IsCleanBuild)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_Initialize);

	UE_LOG(LogZenStore, Display, TEXT("Establishing oplog %s / %s"), *FString(InProjectId), *FString(InOplogId));

	// Establish project

	{
		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> ProjectUri;
		ProjectUri << "/prj/" << InProjectId;
		TArray64<uint8> GetBuffer;
		UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(ProjectUri, &GetBuffer);

		// TODO: how to handle failure here? This is probably the most likely point of failure
		// if the service is not up or not responding

		if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
		{
			UE_LOG(LogZenStore, Display, TEXT("Zen project '%s' already exists"), *FString(InProjectId));
		}
		else
		{
			Request->Reset();

			FCbWriter ProjInfo;
			ProjInfo.BeginObject();
			ProjInfo << "id" << InProjectId;
			ProjInfo << "root" << ServerRoot;
			ProjInfo << "engine" << EngineRoot;
			ProjInfo << "project" << ProjectRoot;
			ProjInfo.EndObject();

			Res = Request->PerformBlockingPost(ProjectUri, ProjInfo.Save().AsObject());

			if (Res != Zen::FZenHttpRequest::Result::Success)
			{
				UE_LOG(LogZenStore, Error, TEXT("Zen project '%s' creation FAILED"), *FString(InProjectId));

				// TODO: how to recover / handle this?
			}
			else if (Request->GetResponseCode() == 201)
			{
				UE_LOG(LogZenStore, Display, TEXT("Zen project '%s' created"), *FString(InProjectId));
			}
			else
			{
				UE_LOG(LogZenStore, Warning, TEXT("Zen project '%s' creation returned success but not HTTP 201"), *FString(InProjectId));
			}
		}
	}

	// Establish oplog

	{
		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> OplogUri;
		OplogUri << "/prj/" << InProjectId << "/oplog/" << InOplogId;

		OplogPath = OplogUri;

		if (IsCleanBuild)
		{
			UE_LOG(LogZenStore, Display, TEXT("Deleting oplog '%s'/'%s' if it exists"), *FString(InProjectId), *FString(InOplogId));
			Request->PerformBlockingDelete(OplogUri);
			Request->Reset();
		}

		TArray64<uint8> GetBuffer;
		UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(OplogUri, &GetBuffer);
		FCbObjectView OplogInfo;

		if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
		{
			UE_LOG(LogZenStore, Display, TEXT("Zen oplog '%s'/'%s' already exists"), *FString(InProjectId), *FString(InOplogId));

			OplogInfo = FCbObjectView(GetBuffer.GetData());
		}
		else
		{
			FMemoryView Payload;

			Request->Reset();
			Res = Request->PerformBlockingPost(OplogUri, Payload);

			if (Res != Zen::FZenHttpRequest::Result::Success)
			{
				UE_LOG(LogZenStore, Error, TEXT("Zen oplog '%s'/'%s' creation FAILED"), *FString(InProjectId), *FString(InOplogId));

				// TODO: how to recover / handle this?
			}
			else if (Request->GetResponseCode() == 201)
			{
				UE_LOG(LogZenStore, Display, TEXT("Zen oplog '%s'/'%s' created"), *FString(InProjectId), *FString(InOplogId));
			}
			else
			{
				UE_LOG(LogZenStore, Warning, TEXT("Zen oplog '%s'/'%s' creation returned success but not HTTP 201"), *FString(InProjectId), *FString(InOplogId));
			}

			// Issue another GET to retrieve information

			GetBuffer.Reset();
			Request->Reset();
			Res = Request->PerformBlockingDownload(OplogUri, &GetBuffer);

			if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
			{
				OplogInfo = FCbObjectView(GetBuffer.GetData());
			}
		}

		TempDirPath = FUTF8ToTCHAR(OplogInfo["tempdir"].AsString());
	}

	{
		TStringBuilder<128> OplogUri;
		OplogUri << "/prj/" << InProjectId << "/oplog/" << InOplogId << "/new";

		OplogNewEntryPath = OplogUri;
	}

	OplogPrepNewEntryPath = TStringBuilder<128>().AppendAnsi("/prj/").Append(InProjectId).AppendAnsi("/oplog/").Append(InOplogId).AppendAnsi("/prep");

	bAllowRead = true;
	bAllowEdit = true;
}

void FZenStoreHttpClient::InitializeReadOnly(FStringView InProjectId, FStringView InOplogId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_InitializeReadOnly);

	UE_LOG(LogZenStore, Display, TEXT("Establishing oplog %s / %s"), *FString(InProjectId), *FString(InOplogId));

	// Establish project

	{
		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> ProjectUri;
		ProjectUri << "/prj/" << InProjectId;
		TArray64<uint8> GetBuffer;
		UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(ProjectUri, &GetBuffer);

		// TODO: how to handle failure here? This is probably the most likely point of failure
		// if the service is not up or not responding

		if (Res != Zen::FZenHttpRequest::Result::Success || Request->GetResponseCode() != 200)
		{
			UE_LOG(LogZenStore, Fatal, TEXT("Zen project '%s' not found"), *FString(InProjectId));
		}
	}

	// Establish oplog

	{
		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> OplogUri;
		OplogUri << "/prj/" << InProjectId << "/oplog/" << InOplogId;

		OplogPath = OplogUri;

		TArray64<uint8> GetBuffer;
		UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(OplogUri, &GetBuffer);

		if (Res != Zen::FZenHttpRequest::Result::Success || Request->GetResponseCode() != 200)
		{
			UE_LOG(LogZenStore, Fatal, TEXT("Zen oplog '%s'/'%s' not found"), *FString(InProjectId), *FString(InOplogId));
		}
	}

	bAllowRead = true;
}

static std::atomic<uint32> GOpCounter;

TFuture<TIoStatusOr<uint64>> FZenStoreHttpClient::AppendOp(FCbPackage OpEntry)
{
	check(bAllowEdit);

	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_AppendOp);

	return Async(EAsyncExecution::LargeThreadPool, [this, OpEntry]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Zen_AppendOp_Async);
		FLargeMemoryWriter SerializedPackage;

		const uint32 Salt = ++GOpCounter;
		bool IsUsingTempFiles = false;

		if (TempDirPath.IsEmpty())
		{
			// Old-style with all attachments by value

			OpEntry.Save(SerializedPackage);
		}
		else
		{
			TConstArrayView<FCbAttachment> Attachments = OpEntry.GetAttachments();

			// Prep phase

			TSet<FIoHash> NeedChunks;

			{
				FCbWriter Writer;
				Writer.BeginObject();
				Writer.BeginArray("have");

				for (const FCbAttachment& Attachment : Attachments)
				{
					Writer.AddHash(Attachment.GetHash());
				}

				Writer.EndArray();
				Writer.EndObject();

				FCbFieldIterator Prep = Writer.Save();

				UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

				bool IsOk = false;
				
				const Zen::FZenHttpRequest::Result Res = Request->PerformBlockingPost(OplogPrepNewEntryPath, Prep.AsObjectView());

				if (Res == Zen::FZenHttpRequest::Result::Success)
				{
					FCbObjectView NeedObject;

					if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
					{
						NeedObject = FCbObjectView(Request->GetResponseBuffer().GetData());

						for (auto& Entry : NeedObject["need"])
						{
							NeedChunks.Add(Entry.AsHash());
						}

						IsOk = true;
					}
				}
			}

			// This uses a slight variation for package attachment serialization
			// by writing larger attachments to a file and referencing it in the
			// core object. Small attachments are serialized inline as normal

			FCbWriter Writer;

			FCbObject PackageObj = OpEntry.GetObject();
			const FIoHash PackageObjHash = PackageObj.GetHash();

			Writer.AddObject(PackageObj);
			Writer.AddObjectAttachment(PackageObjHash);

			// Send phase

			for (const FCbAttachment& Attachment : Attachments)
			{
				bool IsSerialized = false;

				const FIoHash AttachmentHash = Attachment.GetHash();

				if (NeedChunks.Contains(AttachmentHash))
				{
					if (FSharedBuffer AttachView = Attachment.AsBinary())
					{
						if (AttachView.GetSize() >= StandaloneThresholdBytes)
						{
							// Write to temporary file. To avoid race conditions we derive
							// the file name from a salt value and the attachment hash

							FIoHash AttachmentSpec[] { FIoHash::HashBuffer(&Salt, sizeof Salt), AttachmentHash };
							FIoHash AttachmentId = FIoHash::HashBuffer(MakeMemoryView(AttachmentSpec));

							FString TempFilePath = TempDirPath / LexToString(AttachmentId);
							IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

							if (IFileHandle* FileHandle = PlatformFile.OpenWrite(*TempFilePath))
							{
								FileHandle->Write((const uint8*)AttachView.GetData(), AttachView.GetSize());
								delete FileHandle;

								Writer.AddHash(AttachmentHash);

								IsSerialized = true;
								IsUsingTempFiles = true;
							}
							else
							{
								// Take the slow path if we can't open the payload file in the
								// large attachment directory

								UE_LOG(LogZenStore, Warning, TEXT("Could not create file '%s', taking slow path for large attachment"), *TempFilePath);
							}
						}
					}

					if (!IsSerialized)
					{
						Attachment.Save(Writer);
					}
				}
				else
				{
					Writer.AddHash(AttachmentHash);
				}
			}
			Writer.AddNull();

			Writer.Save(SerializedPackage);
		}

		UE_LOG(LogZenStore, Verbose, TEXT("Package size: %" UINT64_FMT), SerializedPackage.TotalSize());

		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<64> NewOpPostUri;
		NewOpPostUri << OplogNewEntryPath;

		if (IsUsingTempFiles)
		{
			NewOpPostUri << "?salt=" << Salt;
		}

		if (UE::Zen::FZenHttpRequest::Result::Success == Request->PerformBlockingPost(NewOpPostUri, SerializedPackage.GetView()))
		{
			return TIoStatusOr<uint64>(SerializedPackage.TotalSize());
		}
		else
		{
			return TIoStatusOr<uint64>((FIoStatus)(FIoStatusBuilder(EIoErrorCode::Unknown) << TEXT("Append OpLog failed, NewOpLogPath='") << OplogNewEntryPath << TEXT("'")));
		}
	});
}

TIoStatusOr<uint64> FZenStoreHttpClient::GetChunkSize(const FIoChunkId& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_GetChunkSize);

	check(bAllowRead);

	UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());
	TArray64<uint8> GetBuffer;
	TStringBuilder<128> ChunkUri;
	ChunkUri << OplogPath << '/' << Id;
	UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingHead(ChunkUri);
	FString ContentLengthStr;
	if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200 && Request->GetHeader("Content-Length", ContentLengthStr))
	{
		return FCStringWide::Atoi64(*ContentLengthStr);
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadChunk(const FIoChunkId& Id, uint64 Offset, uint64 Size)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_ReadChunk);
	TStringBuilder<128> ChunkUri;
	ChunkUri << OplogPath << '/' << Id;
	return ReadOpLogUri(ChunkUri, Offset, Size);
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogAttachment(FStringView Id, uint64 Offset, uint64 Size)
{
	TStringBuilder<128> ChunkUri;
	ChunkUri << OplogPath << '/' << Id;
	return ReadOpLogUri(ChunkUri, Offset, Size);
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogUri(FStringBuilderBase& ChunkUri, uint64 Offset, uint64 Size)
{
	check(bAllowRead);

	UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());
	TArray64<uint8> GetBuffer;

	bool bHaveQuery = false;

	auto AppendQueryDelimiter = [&bHaveQuery, &ChunkUri]
	{
		if (bHaveQuery)
		{
			ChunkUri.Append(TEXT("&"_WSV));
		}
		else
		{
			ChunkUri.Append(TEXT("?"_WSV));
			bHaveQuery = true;
		}
	};

	if (Offset)
	{
		AppendQueryDelimiter();
		ChunkUri.Appendf(TEXT("offset=%" UINT64_FMT), Offset);
	}

	if (Size != ~uint64(0))
	{
		AppendQueryDelimiter();
		ChunkUri.Appendf(TEXT("size=%" UINT64_FMT), Size);
	}


	UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(ChunkUri, &GetBuffer);

	if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
	{
		return FIoBuffer(FIoBuffer::Clone, GetBuffer.GetData(), GetBuffer.Num());
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetOplog()
{
	return Async(EAsyncExecution::LargeThreadPool, [this]
	{
		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		
		TStringBuilder<128> Uri;
		Uri << OplogPath << "/entries";

		TArray64<uint8> GetBuffer;
		UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(Uri, &GetBuffer);

		if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
		{
			FCbObjectView Response(GetBuffer.GetData());
			return TIoStatusOr<FCbObject>(FCbObject::Clone(Response));
		}
		else
		{
			return TIoStatusOr<FCbObject>(FIoStatus(EIoErrorCode::NotFound));
		}
	});
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetFiles()
{
	return Async(EAsyncExecution::LargeThreadPool, [this]
	{
		UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		
		TStringBuilder<128> Uri;
		Uri << OplogPath << "/files";

		TArray64<uint8> GetBuffer;
		UE::Zen::FZenHttpRequest::Result Res = Request->PerformBlockingDownload(Uri, &GetBuffer);

		if (Res == Zen::FZenHttpRequest::Result::Success && Request->GetResponseCode() == 200)
		{
			FCbObjectView Response(GetBuffer.GetData());
			return TIoStatusOr<FCbObject>(FCbObject::Clone(Response));
		}
		else
		{
			return TIoStatusOr<FCbObject>(FIoStatus(EIoErrorCode::NotFound));
		}
	});
}

void 
FZenStoreHttpClient::StartBuildPass()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_StartBuildPass);

	check(bAllowEdit);
}

TIoStatusOr<uint64>
FZenStoreHttpClient::EndBuildPass(FCbPackage OpEntry)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_EndBuildPass);

	check(bAllowEdit);

	FLargeMemoryWriter SerializedPackage;
	OpEntry.Save(SerializedPackage);

	UE_LOG(LogZenStore, Verbose, TEXT("Package size: %lld"), SerializedPackage.TotalSize());

	UE::Zen::FZenScopedRequestPtr Request(RequestPool.Get());

	FMemoryView Payload { SerializedPackage.GetData(), (uint64)SerializedPackage.TotalSize()};
	
	if (UE::Zen::FZenHttpRequest::Result::Success == Request->PerformBlockingPost(OplogNewEntryPath, Payload))
	{
		return static_cast<uint64>(Payload.GetSize());
	}
	else
	{
		return (FIoStatus)(FIoStatusBuilder(EIoErrorCode::Unknown) << TEXT("End build pass failed, NewOpLogPath='") << OplogNewEntryPath << TEXT("'"));
	}
}

} // UE

#else // not PLATFORM_WINDOWS, dummy implementation stub for now

namespace UE {
namespace Zen {
	struct FZenHttpRequestPool
	{
	};
}

FZenStoreHttpClient::FZenStoreHttpClient(const FStringView InHostName, uint16 InPort)
{
}

FZenStoreHttpClient::~FZenStoreHttpClient()
{
}

void FZenStoreHttpClient::Initialize(
	FStringView InProjectId,
	FStringView InOplogId,
	FStringView ServerRoot,
	FStringView EngineRoot,
	FStringView ProjectRoot,
	bool		IsCleanBuild)
{
}

void FZenStoreHttpClient::InitializeReadOnly(FStringView InProjectId, FStringView InOplogId)
{
}

TIoStatusOr<uint64> FZenStoreHttpClient::GetChunkSize(const FIoChunkId& Id)
{
	return 0;
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadChunk(const FIoChunkId& Id, uint64 Offset, uint64 Size)
{
	return FIoBuffer();
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogAttachment(FStringView Id, uint64 Offset, uint64 Size)
{
	return FIoBuffer();
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogUri(FStringBuilderBase& ChunkUri, uint64 Offset, uint64 Size)
{
	return FIoBuffer();
}

void FZenStoreHttpClient::StartBuildPass()
{
}

TIoStatusOr<uint64> FZenStoreHttpClient::EndBuildPass(FCbPackage OpEntry)
{
	return FIoStatus(EIoErrorCode::Unknown);
}

TFuture<TIoStatusOr<uint64>> FZenStoreHttpClient::AppendOp(FCbPackage OpEntry)
{
	return TFuture<TIoStatusOr<uint64>>();
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetOplog()
{
	return TFuture<TIoStatusOr<FCbObject>>();
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetFiles()
{
	return TFuture<TIoStatusOr<FCbObject>>();
}

}

#endif // PLATFORM_WINDOWS
