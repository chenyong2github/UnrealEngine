// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Store.h"
#include "DataStream.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"

namespace Trace
{

class FFileStore;

class FFileStoreInDataStream
	: public IInDataStream
{
public:
	FFileStoreInDataStream(FFileStore* Owner, FSessionHandle SessionHandle, const TCHAR* FilePath);
	virtual int32 Read(void* Data, uint32 Size) override;

private:
	FFileStore* Owner;
	FSessionHandle SessionHandle;
	TUniquePtr<FFileStream> Inner;
};

class FFileStoreOutDataStream
	: public IOutDataStream
{
public:
	FFileStoreOutDataStream(FFileStore* Owner, FSessionHandle SessionHandle, const TCHAR* FilePath);
	virtual ~FFileStoreOutDataStream();
	virtual void Write(const void* Data, uint32 Size) override;

private:
	FFileStore* Owner;
	FSessionHandle SessionHandle;
	TUniquePtr<IFileHandle> Inner;
};

class FFileStore
	: public IStore
{
public:
	FFileStore(const TCHAR* StoreDir);

	virtual void GetAvailableSessions(TArray<FSessionHandle>& OutTraces) override;
	virtual bool GetSessionInfo(FSessionHandle Handle, FSessionInfo& OutInfo) override;
	virtual IOutDataStream* CreateNewSession() override;
	virtual IInDataStream* OpenSessionStream(FSessionHandle Handle) override;

private:
	friend class FFileStoreInDataStream;
	friend class FFileStoreOutDataStream;

	struct FSessionInfoInternal
	{
		FString Name;
		FString Path;
		bool bIsLive;
	};

	bool IsSessionLive(FSessionHandle Handle) const;
	void CloseSessionStream(FSessionHandle Handle);

	mutable FCriticalSection SessionsCS;
	FString StoreDir;
	uint32 NextTraceId;
	TMap<FSessionHandle, TSharedPtr<FSessionInfoInternal>> AvailableSessions;
};

FFileStoreOutDataStream::FFileStoreOutDataStream(FFileStore* InOwner, FSessionHandle InSessionHandle, const TCHAR* InFilePath)
	: Owner(InOwner)
	, SessionHandle(InSessionHandle)
{
	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	IFileHandle* File = FileSystem.OpenWrite(InFilePath, true, true);
	check(File != nullptr);
	Inner.Reset(File);
}

FFileStoreOutDataStream::~FFileStoreOutDataStream()
{
	Inner.Reset(nullptr);
	Owner->CloseSessionStream(SessionHandle);
}

void FFileStoreOutDataStream::Write(const void* Data, uint32 Size)
{
	bool bSuccess = Inner->Write((const uint8*)Data, Size);
	check(bSuccess);
}

FFileStoreInDataStream::FFileStoreInDataStream(FFileStore* InOwner, FSessionHandle InSessionHandle, const TCHAR* InFilePath)
	: Owner(InOwner)
	, SessionHandle(InSessionHandle)
	, Inner(new FFileStream(InFilePath))
{
}

int32 FFileStoreInDataStream::Read(void* Data, uint32 Size)
{
	do 
	{
		int32 InnerResult = Inner->Read(Data, Size);
		if (InnerResult > 0)
		{
			return InnerResult;
		}
		Inner->UpdateFileSize();
		if (Owner->IsSessionLive(SessionHandle))
		{
			FPlatformProcess::Sleep(0.5);
			continue;
		}
		else
		{
			return 0;
		}
	} while (true);
}

FFileStore::FFileStore(const TCHAR* InStoreDir)
	: StoreDir(InStoreDir)
{
	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	FileSystem.CreateDirectory(*StoreDir);

	int32 HighestTraceId = -1;
	FileSystem.IterateDirectory(*StoreDir, [&HighestTraceId, this](const TCHAR* FileName, bool)
	{
		if (const TCHAR* Dot = FCString::Strrchr(FileName, '.'))
		{
			if (FCString::Stricmp(Dot, TEXT(".utrace")) == 0)
			{
				const TCHAR* Backslash = FCString::Strrchr(FileName, '\\');
				const TCHAR* Forwardslash = FCString::Strrchr(FileName, '/');
				const TCHAR* Slash = UPTRINT(Backslash) > UPTRINT(Forwardslash) ? Backslash : Forwardslash;
				if (Slash != nullptr)
				{
					int32 Id = FCString::Atoi(Slash + 1);
					TSharedPtr<FSessionInfoInternal> Session = MakeShared<FSessionInfoInternal>();
					Session->Name = FString::Printf(TEXT("%04d"), Id);
					Session->Path = FileName;
					AvailableSessions.Add(Id, Session);
					HighestTraceId = (Id > HighestTraceId) ? Id : HighestTraceId;
				}
			}
		}
		return true;
	});

	NextTraceId = HighestTraceId + 1;
}

void FFileStore::GetAvailableSessions(TArray<FSessionHandle>& OutTraces)
{
	FScopeLock Lock(&SessionsCS);
	OutTraces.Empty(AvailableSessions.Num());
	for (const auto& KeyValuePair : AvailableSessions)
	{
		OutTraces.Add(KeyValuePair.Key);
	}
}

bool FFileStore::GetSessionInfo(FSessionHandle Handle, FSessionInfo& OutInfo)
{
	FScopeLock Lock(&SessionsCS);
	if (!AvailableSessions.Contains(Handle))
	{
		return false;
	}
	TSharedPtr<FSessionInfoInternal> Session = AvailableSessions[Handle];
	OutInfo.Name = *Session->Name;
	OutInfo.bIsLive = Session->bIsLive;
	return true;
}

IOutDataStream* FFileStore::CreateNewSession()
{
	FScopeLock Lock(&SessionsCS);

	uint32 TraceId = NextTraceId++;
	FString TraceIdStr = FString::Printf(TEXT("%04d"), TraceId);
	FString TracePath = StoreDir;
	TracePath /= TraceIdStr;
	TracePath += TEXT(".utrace");

	TSharedPtr<FSessionInfoInternal> Session = MakeShared<FSessionInfoInternal>();
	Session->Name = TraceIdStr;
	Session->Path = TracePath;
	Session->bIsLive = true;
	AvailableSessions.Add(TraceId, Session);

	return new FFileStoreOutDataStream(this, TraceId, *Session->Path);
}

IInDataStream* FFileStore::OpenSessionStream(FSessionHandle Handle)
{
	FScopeLock Lock(&SessionsCS);
	if (!AvailableSessions.Contains(Handle))
	{
		return nullptr;
	}
	TSharedPtr<FSessionInfoInternal> Session = AvailableSessions[Handle];
	return new FFileStoreInDataStream(this, Handle, *Session->Path);
}

bool FFileStore::IsSessionLive(FSessionHandle Handle) const
{
	FScopeLock Lock(&SessionsCS);
	return AvailableSessions[Handle]->bIsLive;
}

void FFileStore::CloseSessionStream(FSessionHandle Handle)
{
	FScopeLock Lock(&SessionsCS);
	AvailableSessions[Handle]->bIsLive = false;
}

TSharedPtr<IStore> Store_Create(const TCHAR* StoreDir)
{
	TSharedRef<IStore> FileStore = MakeShared<FFileStore>(StoreDir);
	return FileStore;
}

}

