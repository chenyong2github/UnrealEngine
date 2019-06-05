// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Store.h"
#include "DataStream.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Containers/Ticker.h"

namespace Trace
{

class FFileStore;

class FFileStoreInDataStream
	: public IInDataStream
{
public:
	FFileStoreInDataStream(FFileStore* Owner, FStoreSessionHandle SessionHandle, FFileStream* File);
	virtual int32 Read(void* Data, uint32 Size) override;

private:
	FFileStore* Owner;
	FStoreSessionHandle SessionHandle;
	TUniquePtr<FFileStream> Inner;
};

class FFileStoreOutDataStream
	: public IOutDataStream
{
public:
	FFileStoreOutDataStream(FFileStore* Owner, FStoreSessionHandle SessionHandle, IFileHandle* File);
	virtual ~FFileStoreOutDataStream();
	virtual bool Write(const void* Data, uint32 Size) override;

private:
	FFileStore* Owner;
	FStoreSessionHandle SessionHandle;
	TUniquePtr<IFileHandle> Inner;
};

class FFileStore
	: public IStore
{
public:
	FFileStore(const TCHAR* StoreDir);
	~FFileStore();

	virtual void GetAvailableSessions(TArray<FStoreSessionInfo>& OutSessions) const override;
	virtual TTuple<FStoreSessionHandle, IOutDataStream*> CreateNewSession() override;
	virtual IInDataStream* OpenSessionStream(FStoreSessionHandle Handle) override;

private:
	friend class FFileStoreInDataStream;
	friend class FFileStoreOutDataStream;

	struct FSessionInfoInternal
	{
		FStoreSessionHandle Handle;
		FString Name;
		FString Path;
		bool bIsLive;
		bool bIsValid;
	};

	FSessionInfoInternal* AddSession(const FString& Path);
	void RemoveSession(FStoreSessionHandle Handle);
	FSessionInfoInternal* GetSession(FStoreSessionHandle Handle) const;
	bool Tick(float DeltaTime);
	void OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges);
	bool IsSessionLive(FStoreSessionHandle Handle) const;
	void CloseSessionStream(FStoreSessionHandle Handle);

	mutable FCriticalSection SessionsCS;
	FString StoreDir;
	IDirectoryWatcher* DirectoryWatcher;
	FDelegateHandle DirectoryWatcherHandle;
	FDelegateHandle TickHandle;
	FStoreSessionHandle NextSessionHandle = 1;
	TArray<FSessionInfoInternal*> Sessions;
	TMap<FString, FSessionInfoInternal*> SessionsByPathMap;
};

FFileStoreOutDataStream::FFileStoreOutDataStream(FFileStore* InOwner, FStoreSessionHandle InSessionHandle, IFileHandle* InFile)
	: Owner(InOwner)
	, SessionHandle(InSessionHandle)
{
	check(InFile);
	Inner.Reset(InFile);
}

FFileStoreOutDataStream::~FFileStoreOutDataStream()
{
	Inner.Reset(nullptr);
	Owner->CloseSessionStream(SessionHandle);
}

bool FFileStoreOutDataStream::Write(const void* Data, uint32 Size)
{
	return Inner->Write((const uint8*)Data, Size);
}

FFileStoreInDataStream::FFileStoreInDataStream(FFileStore* InOwner, FStoreSessionHandle InSessionHandle, FFileStream* InFileStream)
	: Owner(InOwner)
	, SessionHandle(InSessionHandle)
	, Inner(InFileStream)
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
	FileSystem.IterateDirectory(*StoreDir, [&HighestTraceId, this](const TCHAR* FileName, bool IsDirectory)
	{
		if (!IsDirectory && FPaths::GetExtension(FileName) == TEXT("utrace"))
		{
			AddSession(FileName);
		}
		return true;
	});

	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	DirectoryWatcher = DirectoryWatcherModule.Get();
	if (DirectoryWatcher)
	{
		DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(StoreDir, IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FFileStore::OnDirectoryChanged), DirectoryWatcherHandle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
		FTickerDelegate TickDelegate = FTickerDelegate::CreateRaw(this, &FFileStore::Tick);
		TickHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate, 0.5f);
	}
}

FFileStore::~FFileStore()
{
	FDirectoryWatcherModule* DirectoryWatcherModule = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	if (DirectoryWatcherModule)
	{
		IDirectoryWatcher* LocalDirectoryWatcher = DirectoryWatcherModule->Get();
		if (DirectoryWatcher)
		{
			LocalDirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(StoreDir, DirectoryWatcherHandle);
		}
	}
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	for (FSessionInfoInternal* Session : Sessions)
	{
		delete Session;
	}
}

FFileStore::FSessionInfoInternal* FFileStore::AddSession(const FString& Path)
{
	FScopeLock Lock(&SessionsCS);

	FSessionInfoInternal* Session = new FSessionInfoInternal();
	Session->Handle = NextSessionHandle++;
	Session->Name = FPaths::GetBaseFilename(Path);
	Session->Path = Path;
	Session->bIsValid = true;
	Session->bIsLive = false;
	Sessions.Add(Session);
	SessionsByPathMap.Add(Path, Session);

	return Session;
}

void FFileStore::RemoveSession(FStoreSessionHandle Handle)
{
	FScopeLock Lock(&SessionsCS);

	FSessionInfoInternal* Session = Sessions[Handle - 1];
	SessionsByPathMap.Remove(Session->Path);
	Sessions[Handle - 1]->bIsValid = false;
}

FFileStore::FSessionInfoInternal* FFileStore::GetSession(FStoreSessionHandle Handle) const
{
	FScopeLock Lock(&SessionsCS);
	if (Handle - 1 >= Sessions.Num())
	{
		return nullptr;
	}
	FSessionInfoInternal* Session = Sessions[Handle - 1];
	if (!Session->bIsValid)
	{
		return nullptr;
	}
	return Session;
}

void FFileStore::GetAvailableSessions(TArray<FStoreSessionInfo>& OutSessions) const
{
	FScopeLock Lock(&SessionsCS);
	OutSessions.Reserve(OutSessions.Num() + Sessions.Num());
	for (const FSessionInfoInternal* Session : Sessions)
	{
		if (Session->bIsValid)
		{
			FStoreSessionInfo& SessionInfo = OutSessions.AddDefaulted_GetRef();
			SessionInfo.Handle = Session->Handle;
			SessionInfo.Uri = *Session->Path;
			SessionInfo.Name = *Session->Name;
			SessionInfo.bIsLive = Session->bIsLive;
		}
	}
}

TTuple<FStoreSessionHandle, IOutDataStream*> FFileStore::CreateNewSession()
{
	FScopeLock Lock(&SessionsCS);

	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();

	FString TracePath = StoreDir / FString::Printf(TEXT("%s.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	uint64 Index = 1;
	while (FileSystem.FileExists(*TracePath))
	{
		TracePath = StoreDir / FString::Printf(TEXT("%s_%d.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), Index);
	}

	IFileHandle* File = FileSystem.OpenWrite(*TracePath, true, true);
	if (!File)
	{
		return MakeTuple(FStoreSessionHandle(0), (IOutDataStream*)nullptr);
	}

	FSessionInfoInternal* Session = AddSession(TracePath);
	Session->bIsLive = true;
	return MakeTuple(Session->Handle, static_cast<IOutDataStream*>(new FFileStoreOutDataStream(this, Session->Handle, File)));
}

IInDataStream* FFileStore::OpenSessionStream(FStoreSessionHandle Handle)
{
	FScopeLock Lock(&SessionsCS);
	
	FSessionInfoInternal* Session = GetSession(Handle);
	if (!Session)
	{
		return nullptr;
	}

	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	if (!FileSystem.FileExists(*Session->Path))
	{
		return nullptr;
	}

	FFileStream* FileStream = new FFileStream(*Session->Path);
	return new FFileStoreInDataStream(this, Handle, FileStream);
}

bool FFileStore::Tick(float DeltaTime)
{
	DirectoryWatcher->Tick(DeltaTime);
	return true;
}

void FFileStore::OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
{
	FScopeLock Lock(&SessionsCS);

	for (const FFileChangeData& FileChange : FileChanges)
	{
		FSessionInfoInternal** FindIt = SessionsByPathMap.Find(FileChange.Filename.ToUpper());
		switch (FileChange.Action)
		{
		case FFileChangeData::FCA_Removed:
			if (FindIt)
			{
				RemoveSession((*FindIt)->Handle);
			}
			break;
		case FFileChangeData::FCA_Added:
			if (!FindIt && FPaths::GetExtension(FileChange.Filename) == TEXT("utrace"))
			{
				AddSession(FileChange.Filename);
			}
			break;
		}
	}
}

bool FFileStore::IsSessionLive(FStoreSessionHandle Handle) const
{
	FScopeLock Lock(&SessionsCS);
	
	FSessionInfoInternal* Session = GetSession(Handle);
	if (!Session)
	{
		return false;
	}
	return Session->bIsLive;
}

void FFileStore::CloseSessionStream(FStoreSessionHandle Handle)
{
	FScopeLock Lock(&SessionsCS);
	
	FSessionInfoInternal* Session = GetSession(Handle);
	if (Session)
	{
		Session->bIsLive = false;
	}
}

TSharedPtr<IStore> Store_Create(const TCHAR* StoreDir)
{
	TSharedRef<IStore> FileStore = MakeShared<FFileStore>(StoreDir);
	return FileStore;
}

}

