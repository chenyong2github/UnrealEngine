// Copyright Epic Games, Inc. All Rights Reserved.

#include "StorageServerPlatformFile.h"
#include "StorageServerIoDispatcherBackend.h"
#include "HAL/IPlatformFileModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ScopeRWLock.h"
#include "StorageServerConnection.h"
#include "Modules/ModuleManager.h"
#include "Misc/StringBuilder.h"
#include "Algo/Replace.h"

#if WITH_COTF
#include "Modules/ModuleManager.h"
#include "CookOnTheFly.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogStorageServerPlatformFile, Log, All);

#if !UE_BUILD_SHIPPING

FStorageServerFileSystemTOC::~FStorageServerFileSystemTOC()
{
	FWriteScopeLock _(TocLock);
	for (auto& KV : Directories)
	{
		delete KV.Value;
	}
}

FStorageServerFileSystemTOC::FDirectory* FStorageServerFileSystemTOC::AddDirectoriesRecursive(const FString& DirectoryPath)
{
	FDirectory* Directory = new FDirectory();
	Directories.Add(DirectoryPath, Directory);
	FString ParentDirectoryPath = FPaths::GetPath(DirectoryPath);
	FDirectory* ParentDirectory;
	if (ParentDirectoryPath.IsEmpty())
	{
		ParentDirectory = &Root;
	}
	else
	{
		ParentDirectory = Directories.FindRef(ParentDirectoryPath);
		if (!ParentDirectory)
		{
			ParentDirectory = AddDirectoriesRecursive(ParentDirectoryPath);
		}
	}
	ParentDirectory->Directories.Add(DirectoryPath);
	return Directory;
}

void FStorageServerFileSystemTOC::AddFile(FStringView PathView, int32 Index)
{
	FWriteScopeLock _(TocLock);

	FString Path(PathView);
	FilePathToIndexMap.Add(Path, Index);
	FileIndexToPathMap.Add(Index, Path);
	FString DirectoryPath = FPaths::GetPath(Path);
	FDirectory* Directory = Directories.FindRef(DirectoryPath);
	if (!Directory)
	{
		Directory = AddDirectoriesRecursive(DirectoryPath);
	}
	Directory->Files.Add(Index);
}

bool FStorageServerFileSystemTOC::FileExists(const FString& Path)
{
	FReadScopeLock _(TocLock);
	return FilePathToIndexMap.Contains(Path);
}

bool FStorageServerFileSystemTOC::DirectoryExists(const FString& Path)
{
	FReadScopeLock _(TocLock);
	return Directories.Contains(Path);
}

int32* FStorageServerFileSystemTOC::FindFileIndex(const FString& Path)
{
	FReadScopeLock _(TocLock);
	return FilePathToIndexMap.Find(Path);
}

bool FStorageServerFileSystemTOC::IterateDirectory(const FString& Path, TFunctionRef<bool(int32, const TCHAR*)> Callback)
{
	UE_LOG(LogStorageServerPlatformFile, Verbose, TEXT("IterateDirectory '%s'"), *Path);

	FReadScopeLock _(TocLock);

	FDirectory* Directory = Directories.FindRef(Path);
	if (!Directory)
	{
		return false;
	}
	for (int32 FileIndex : Directory->Files)
	{
		const FString* FindFilePath = FileIndexToPathMap.Find(FileIndex);
		if (FindFilePath && !Callback(FileIndex, **FindFilePath))
		{
			return false;
		}
	}
	for (const FString& ChildDirectoryPath : Directory->Directories)
	{
		if (!Callback(-1, *ChildDirectoryPath))
		{
			return false;
		}
	}
	return true;
}

class FStorageServerFileHandle
	: public IFileHandle
{
	enum
	{
		BufferSize = 64 << 10
	};
	FStorageServerPlatformFile& Owner;
	FString Filename;
	int32 FileIndex;
	int64 FilePos = 0;
	int64 FileSize = -1;
	int64 BufferStart = -1;
	int64 BufferEnd = -1;
	uint8 Buffer[BufferSize];

public:
	FStorageServerFileHandle(FStorageServerPlatformFile& InOwner, const TCHAR* InFilename, int32 InFileIndex)
		: Owner(InOwner)
		, Filename(InFilename)
		, FileIndex(InFileIndex)
	{
	}

	~FStorageServerFileHandle()
	{
	}

	virtual int64 Size() override
	{
		if (FileSize < 0)
		{
			FFileStatData FileStatData = Owner.SendGetStatDataMessage(FileIndex);
			if (FileStatData.bIsValid)
			{
				FileSize = FileStatData.FileSize;
			}
			else
			{
				UE_LOG(LogStorageServerPlatformFile, Warning, TEXT("Failed to obtain size of file '%s'"), *Filename);
				FileSize = 0;
			}
		}
		return FileSize;
	}

	virtual int64 Tell() override
	{
		return FilePos;
	}

	virtual bool Seek(int64 NewPosition) override
	{
		FilePos = NewPosition;
		return true;
	}

	virtual bool SeekFromEnd(int64 NewPositionRelativeToEnd = 0) override
	{
		return Seek(Size() + NewPositionRelativeToEnd);
	}

	virtual bool Read(uint8* Destination, int64 BytesToRead) override
	{
		if (BytesToRead == 0)
		{
			return true;
		}

		if (BytesToRead > BufferSize)
		{
			int64 BytesRead = Owner.SendReadMessage(Destination, FileIndex, FilePos, BytesToRead);
			if (BytesRead == BytesToRead)
			{
				FilePos += BytesRead;
				return true;
			}
		}

		if (FilePos < BufferStart || BufferEnd < FilePos + BytesToRead)
		{
			int64 BytesRead = Owner.SendReadMessage(Buffer, FileIndex, FilePos, BufferSize);
			BufferStart = FilePos;
			BufferEnd = BufferStart + BytesRead;
		}

		int64 BufferOffset = FilePos - BufferStart;
		check(BufferEnd > BufferOffset);
		int64 BytesToReadFromBuffer = FMath::Min(BufferEnd - BufferOffset, BytesToRead);
		FMemory::Memcpy(Destination, Buffer + BufferOffset, BytesToReadFromBuffer);
		if (BytesToReadFromBuffer == BytesToRead)
		{
			FilePos += BytesToReadFromBuffer;
			return true;
		}
		
		return false;
	}

	virtual bool Write(const uint8* Source, int64 BytesToWrite) override
	{
		check(false);
		return false;
	}

	virtual bool Flush(const bool bFullFlush = false) override
	{
		return false;
	}

	virtual bool Truncate(int64 NewSize) override
	{
		return false;
	}
};

FStorageServerPlatformFile::FStorageServerPlatformFile()
{
}

FStorageServerPlatformFile::~FStorageServerPlatformFile()
{
}

bool FStorageServerPlatformFile::ShouldBeUsed(IPlatformFile* Inner, const TCHAR* CmdLine) const
{
	FString Host;
	if (FParse::Value(FCommandLine::Get(), TEXT("-ZenStoreHost="), Host))
	{
		if (!Host.ParseIntoArray(HostAddrs, TEXT("+"), true))
		{
			HostAddrs.Add(Host);
		}
	}

	return HostAddrs.Num() > 0;
}

bool FStorageServerPlatformFile::Initialize(IPlatformFile* Inner, const TCHAR* CmdLine)
{
	LowerLevel = Inner;
	bool bResult = false;
	if (HostAddrs.Num() > 0)
	{
		Connection.Reset(new FStorageServerConnection());
		FString StorageServerProject;
		FParse::Value(CmdLine, TEXT("-ZenStoreProject="), StorageServerProject);
		const TCHAR* ProjectOverride = StorageServerProject.IsEmpty() ? nullptr : *StorageServerProject;
		FString StorageServerPlatform;
		FParse::Value(CmdLine, TEXT("-ZenStorePlatform="), StorageServerPlatform);
		const TCHAR* PlatformOverride = StorageServerPlatform.IsEmpty() ? nullptr : *StorageServerPlatform;
		if (Connection->Initialize(HostAddrs, 1337, ProjectOverride, PlatformOverride))
		{
			if (SendGetFileListMessage())
			{
				FIoStatus IoDispatcherInitStatus = FIoDispatcher::Initialize();
				if (IoDispatcherInitStatus.IsOk())
				{
					FIoDispatcher& IoDispatcher = FIoDispatcher::Get();
					TSharedRef<FStorageServerIoDispatcherBackend> IoDispatcherBackend = MakeShared<FStorageServerIoDispatcherBackend>(*Connection.Get());
					IoDispatcher.Mount(IoDispatcherBackend);
#if WITH_COTF
					if (IsRunningCookOnTheFly())
					{
						UE::Cook::ICookOnTheFlyModule& CookOnTheFlyModule = FModuleManager::LoadModuleChecked<UE::Cook::ICookOnTheFlyModule>(TEXT("CookOnTheFly"));
						CookOnTheFlyModule.GetServerConnection().OnMessage().AddRaw(this, &FStorageServerPlatformFile::OnCookOnTheFlyMessage);
					}
#endif					
					bResult = true;
				}
				else
				{
					UE_LOG(LogStorageServerPlatformFile, Fatal, TEXT("Failed to initialize IoDispatcher with Zen host '%s'"), *Connection->GetHostAddr());
				}
			}
			else
			{
				UE_LOG(LogStorageServerPlatformFile, Fatal, TEXT("Failed to get file list from Zen at '%s'"), *Connection->GetHostAddr());
			}
		}
		else
		{
			UE_LOG(LogStorageServerPlatformFile, Fatal, TEXT("Failed to initialize connection"));
		}
	}
	if (!bResult)
	{
		Connection.Reset(nullptr);
		return false;
	}
	return true;
}

bool FStorageServerPlatformFile::FileExists(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		return true;
	}
	return LowerLevel->FileExists(Filename);
}

FDateTime FStorageServerPlatformFile::GetTimeStamp(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename))
	{
		int32* FindFileIndex = ServerToc.FindFileIndex(*StorageServerFilename);
		if (FindFileIndex)
		{
			FFileStatData FileStatData = SendGetStatDataMessage(*FindFileIndex);
			check(FileStatData.bIsValid);
			return FileStatData.ModificationTime;
		}
	}
	return LowerLevel->GetTimeStamp(Filename);
}

FDateTime FStorageServerPlatformFile::GetAccessTimeStamp(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename))
	{
		int32* FindFileIndex = ServerToc.FindFileIndex(*StorageServerFilename);
		if (FindFileIndex)
		{
			FFileStatData FileStatData = SendGetStatDataMessage(*FindFileIndex);
			check(FileStatData.bIsValid);
			return FileStatData.AccessTime;
		}
	}
	return LowerLevel->GetAccessTimeStamp(Filename);
}

int64 FStorageServerPlatformFile::FileSize(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename))
	{
		int32* FindFileIndex = ServerToc.FindFileIndex(*StorageServerFilename);
		if (FindFileIndex)
		{
			FFileStatData FileStatData = SendGetStatDataMessage(*FindFileIndex);
			check(FileStatData.bIsValid);
			return FileStatData.FileSize;
		}
	}
	return LowerLevel->FileSize(Filename);
}

bool FStorageServerPlatformFile::IsReadOnly(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		return true;
	}
	return LowerLevel->IsReadOnly(Filename);
}

FFileStatData FStorageServerPlatformFile::GetStatData(const TCHAR* FilenameOrDirectory)
{
	TStringBuilder<1024> StorageServerFilenameOrDirectory;
	if (MakeStorageServerPath(FilenameOrDirectory, StorageServerFilenameOrDirectory))
	{
		int32* FindFileIndex = ServerToc.FindFileIndex(*StorageServerFilenameOrDirectory);
		if (FindFileIndex)
		{
			return SendGetStatDataMessage(*FindFileIndex);
		}
		else if (ServerToc.DirectoryExists(*StorageServerFilenameOrDirectory))
		{
			return FFileStatData(
				FDateTime::MinValue(),
				FDateTime::MinValue(),
				FDateTime::MinValue(),
				0,
				true,
				true);
		}
	}
	return LowerLevel->GetStatData(FilenameOrDirectory);
}

IFileHandle* FStorageServerPlatformFile::InternalOpenFile(int32 FileIndex, const TCHAR* LocalFilename)
{
	return new FStorageServerFileHandle(*this, LocalFilename, FileIndex);
}

IFileHandle* FStorageServerPlatformFile::OpenRead(const TCHAR* Filename, bool bAllowWrite)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename))
	{
		int32* FindFileIndex = ServerToc.FindFileIndex(*StorageServerFilename);
		if (FindFileIndex)
		{
			return InternalOpenFile(*FindFileIndex, Filename);
		}
	}
	return LowerLevel->OpenRead(Filename, bAllowWrite);
}

bool FStorageServerPlatformFile::IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor)
{
	TStringBuilder<1024> StorageServerDirectory;
	bool bResult = false;
	if (MakeStorageServerPath(Directory, StorageServerDirectory) && ServerToc.DirectoryExists(*StorageServerDirectory))
	{
		bResult |= ServerToc.IterateDirectory(*StorageServerDirectory, [this, &Visitor](int32 FileIndex, const TCHAR* FilenameOrDirectory)
		{
			TStringBuilder<1024> LocalPath;
			bool bConverted = MakeLocalPath(FilenameOrDirectory, LocalPath);
			check(bConverted);
			return Visitor.Visit(*LocalPath, FileIndex < 0);
		});
	}
	else
	{
		bResult |= LowerLevel->IterateDirectory(Directory, Visitor);
	}
	return bResult;
}

bool FStorageServerPlatformFile::IterateDirectoryStat(const TCHAR* Directory, FDirectoryStatVisitor& Visitor)
{
	TStringBuilder<1024> StorageServerDirectory;
	bool bResult = false;
	if (MakeStorageServerPath(Directory, StorageServerDirectory) && ServerToc.DirectoryExists(*StorageServerDirectory))
	{
		bResult |= ServerToc.IterateDirectory(*StorageServerDirectory, [this, &Visitor](int32 FileIndex, const TCHAR* ServerFilenameOrDirectory)
		{
			TStringBuilder<1024> LocalPath;
			bool bConverted = MakeLocalPath(ServerFilenameOrDirectory, LocalPath);
			check(bConverted);
			FFileStatData FileStatData;
			if (FileIndex >= 0)
			{
				FileStatData = SendGetStatDataMessage(FileIndex);
				check(FileStatData.bIsValid);
			}
			else
			{
				FileStatData = FFileStatData(
					FDateTime::MinValue(),
					FDateTime::MinValue(),
					FDateTime::MinValue(),
					0,
					true,
					true);
			}
			return Visitor.Visit(*LocalPath, FileStatData);
		});
	}
	else
	{
		bResult |= LowerLevel->IterateDirectoryStat(Directory, Visitor);
	}
	return bResult;
}

bool FStorageServerPlatformFile::DirectoryExists(const TCHAR* Directory)
{
	TStringBuilder<1024> StorageServerDirectory;
	if (MakeStorageServerPath(Directory, StorageServerDirectory) && ServerToc.DirectoryExists(*StorageServerDirectory))
	{
		return true;
	}
	return LowerLevel->DirectoryExists(Directory);
}

FString FStorageServerPlatformFile::GetFilenameOnDisk(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		UE_LOG(LogStorageServerPlatformFile, Warning, TEXT("Attempting to get disk filename of remote file '%s'"), Filename);
		return Filename;
	}
	return LowerLevel->GetFilenameOnDisk(Filename);
}

bool FStorageServerPlatformFile::DeleteFile(const TCHAR* Filename)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		return false;
	}
	return LowerLevel->DeleteFile(Filename);
}

bool FStorageServerPlatformFile::MoveFile(const TCHAR* To, const TCHAR* From)
{
	TStringBuilder<1024> StorageServerTo;
	if (MakeStorageServerPath(To, StorageServerTo) && ServerToc.FileExists(*StorageServerTo))
	{
		return false;
	}
	TStringBuilder<1024> StorageServerFrom;
	if (MakeStorageServerPath(From, StorageServerFrom))
	{
		int32* FindFromFileIndex = ServerToc.FindFileIndex(*StorageServerFrom);
		if (FindFromFileIndex)
		{
			TUniquePtr<IFileHandle> ToFile(LowerLevel->OpenWrite(To, false, false));
			if (!ToFile)
			{
				return false;
			}

			TUniquePtr<IFileHandle> FromFile(InternalOpenFile(*FindFromFileIndex, *StorageServerFrom));
			if (!FromFile)
			{
				return false;
			}
			const int64 BufferSize = 64 << 10;
			TArray<uint8> Buffer;
			Buffer.SetNum(BufferSize);
			int64 BytesLeft = FromFile->Size();
			while (BytesLeft)
			{
				int64 BytesToWrite = FMath::Min(BufferSize, BytesLeft);
				if (!FromFile->Read(Buffer.GetData(), BytesToWrite))
				{
					return false;
				}
				if (!ToFile->Write(Buffer.GetData(), BytesToWrite))
				{
					return false;
				}
				BytesLeft -= BytesToWrite;
			}
			return true;
		}
	}
	return LowerLevel->MoveFile(To, From);
}

bool FStorageServerPlatformFile::SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		return bNewReadOnlyValue;
	}
	return LowerLevel->SetReadOnly(Filename, bNewReadOnlyValue);
}

void FStorageServerPlatformFile::SetTimeStamp(const TCHAR* Filename, FDateTime DateTime)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		return;
	}
	LowerLevel->SetTimeStamp(Filename, DateTime);
}

IFileHandle* FStorageServerPlatformFile::OpenWrite(const TCHAR* Filename, bool bAppend, bool bAllowRead)
{
	TStringBuilder<1024> StorageServerFilename;
	if (MakeStorageServerPath(Filename, StorageServerFilename) && ServerToc.FileExists(*StorageServerFilename))
	{
		return nullptr;
	}
	return LowerLevel->OpenWrite(Filename, bAppend, bAllowRead);
}

bool FStorageServerPlatformFile::CreateDirectory(const TCHAR* Directory)
{
	TStringBuilder<1024> StorageServerDirectory;
	if (MakeStorageServerPath(Directory, StorageServerDirectory) && ServerToc.DirectoryExists(*StorageServerDirectory))
	{
		return true;
	}
	return LowerLevel->CreateDirectory(Directory);
}

bool FStorageServerPlatformFile::DeleteDirectory(const TCHAR* Directory)
{
	TStringBuilder<1024> StorageServerDirectory;
	if (MakeStorageServerPath(Directory, StorageServerDirectory) && ServerToc.DirectoryExists(*StorageServerDirectory))
	{
		return false;
	}
	return LowerLevel->DeleteDirectory(Directory);
}

bool FStorageServerPlatformFile::MakeStorageServerPath(const TCHAR* LocalFilenameOrDirectory, FStringBuilderBase& OutPath) const
{
	FStringView LocalEngineDirView(FPlatformMisc::EngineDir());
	FStringView LocalProjectDirView(FPlatformMisc::ProjectDir());
	FStringView LocalFilenameOrDirectoryView(LocalFilenameOrDirectory);
	bool bValid = false;

	if (LocalFilenameOrDirectoryView.StartsWith(LocalEngineDirView, ESearchCase::IgnoreCase))
	{
		OutPath.Append(ServerEngineDirView);
		OutPath.Append(LocalFilenameOrDirectoryView.RightChop(LocalEngineDirView.Len()));
		bValid = true;
	}
	else if (LocalFilenameOrDirectoryView.StartsWith(LocalProjectDirView, ESearchCase::IgnoreCase))
	{
		OutPath.Append(ServerProjectDirView);
		OutPath.Append(LocalFilenameOrDirectoryView.RightChop(LocalProjectDirView.Len()));
		bValid = true;
	}

	if (bValid)
	{
		Algo::Replace(MakeArrayView(OutPath), '\\', '/');
		OutPath.RemoveSuffix(LocalFilenameOrDirectoryView.EndsWith('/') ? 1 : 0);
	}

	return bValid;
}

bool FStorageServerPlatformFile::MakeLocalPath(const TCHAR* ServerFilenameOrDirectory, FStringBuilderBase& OutPath) const
{
	FStringView ServerFilenameOrDirectoryView(ServerFilenameOrDirectory);
	if (ServerFilenameOrDirectoryView.StartsWith(ServerEngineDirView, ESearchCase::IgnoreCase))
	{
		OutPath.Append(FPlatformMisc::EngineDir());
		OutPath.Append(ServerFilenameOrDirectoryView.RightChop(ServerEngineDirView.Len()));
		return true;
	}
	else if (ServerFilenameOrDirectoryView.StartsWith(ServerProjectDirView, ESearchCase::IgnoreCase))
	{
		OutPath.Append(FPlatformMisc::ProjectDir());
		OutPath.Append(ServerFilenameOrDirectoryView.RightChop(ServerProjectDirView.Len()));
		return true;
	}
	return false;
}

bool FStorageServerPlatformFile::SendGetFileListMessage()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StorageServerPlatformFileGetFileList);
	
	Connection->FileManifestRequest([&](FIoChunkId Id, FStringView Path)
	{
		ServerToc.AddFile(Path, FileIndexFromChunkId(Id));
	});

	return true;
}

FFileStatData FStorageServerPlatformFile::SendGetStatDataMessage(int32 FileIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StorageServerPlatformFileGetStatData);
	int64 FileSize = Connection->FileSizeRequest(FileIndex);
	if (FileSize < 0)
	{
		return FFileStatData();
	}

	FDateTime CreationTime = FDateTime::Now();
	FDateTime AccessTime = FDateTime::Now();
	FDateTime ModificationTime = FDateTime::Now();

	return FFileStatData(CreationTime, AccessTime, ModificationTime, FileSize, false, true);
}

int64 FStorageServerPlatformFile::SendReadMessage(uint8* Destination, int32 FileIndex, int64 Offset, int64 BytesToRead)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StorageServerPlatformFileRead);
	int64 BytesRead = 0;
	Connection->ReadFileRequest(FileIndex, Offset, BytesToRead, [Destination, &BytesRead](FStorageServerResponse& Response)
	{
		BytesRead = Response.TotalSize();
		Response.Serialize(Destination, Response.TotalSize());
	});
	return BytesRead;
}

#if WITH_COTF
void FStorageServerPlatformFile::OnCookOnTheFlyMessage(const UE::Cook::FCookOnTheFlyMessage& Message)
{
	switch (Message.GetHeader().MessageType)
	{
		case UE::Cook::ECookOnTheFlyMessage::FilesAdded:
		{
			UE_LOG(LogCookOnTheFly, Verbose, TEXT("Received '%s' message"), LexToString(Message.GetHeader().MessageType));

			TArray<FString> Filenames;
			TArray<FIoChunkId> ChunkIds;

			{
				TUniquePtr<FArchive> Ar = Message.ReadBody();
				*Ar << Filenames;
				*Ar << ChunkIds;
			}

			check(Filenames.Num() == ChunkIds.Num());

			for (int32 Idx = 0, Num = Filenames.Num(); Idx < Num; ++Idx)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Adding file '%s'"), *Filenames[Idx]);
				ServerToc.AddFile(Filenames[Idx], FileIndexFromChunkId(ChunkIds[Idx]));
			}

			break;
		}
	}
}
#endif

class FStorageServerClientFileModule
	: public IPlatformFileModule
{
public:

	virtual IPlatformFile* GetPlatformFile() override
	{
		static TUniquePtr<IPlatformFile> AutoDestroySingleton = MakeUnique<FStorageServerPlatformFile>();
		return AutoDestroySingleton.Get();
	}
};

IMPLEMENT_MODULE(FStorageServerClientFileModule, StorageServerClient);

#endif
