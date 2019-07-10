// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DirectoryWatchRequestWindows.h"
#include "DirectoryWatcherPrivate.h"

FDirectoryWatchRequestWindows::FDirectoryWatchRequestWindows(uint32 Flags)
{
	bPendingDelete = false;
	bEndWatchRequestInvoked = false;

	MaxChanges = 16384;
	bWatchSubtree = (Flags & IDirectoryWatcher::WatchOptions::IgnoreChangesInSubtree) == 0;
	bool bIncludeDirectoryEvents = (Flags & IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges) != 0;

	NotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | (bIncludeDirectoryEvents? FILE_NOTIFY_CHANGE_DIR_NAME : 0) | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

	DirectoryHandle = INVALID_HANDLE_VALUE;

	BufferLength = sizeof(FILE_NOTIFY_INFORMATION) * MaxChanges;
	Buffer = FMemory::Malloc(BufferLength, alignof(DWORD));
	BackBuffer = FMemory::Malloc(BufferLength, alignof(DWORD));

	FMemory::Memzero(&Overlapped, sizeof(Overlapped));
	FMemory::Memzero(Buffer, BufferLength);
	FMemory::Memzero(BackBuffer, BufferLength);

	Overlapped.hEvent = this;
}

FDirectoryWatchRequestWindows::~FDirectoryWatchRequestWindows()
{
	if (Buffer)
	{
		FMemory::Free(Buffer);
	}

	if (BackBuffer)
	{
		FMemory::Free(BackBuffer);
	}

	if ( DirectoryHandle != INVALID_HANDLE_VALUE )
	{
		::CloseHandle(DirectoryHandle);
		DirectoryHandle = INVALID_HANDLE_VALUE;
	}
}

bool FDirectoryWatchRequestWindows::Init(const FString& InDirectory)
{
	check(Buffer);

	if ( InDirectory.Len() == 0 )
	{
		// Verify input
		return false;
	}

	Directory = InDirectory;

	if ( DirectoryHandle != INVALID_HANDLE_VALUE )
	{
		// If we already had a handle for any reason, close the old handle
		::CloseHandle(DirectoryHandle);
	}

	// Make sure the path is absolute
	const FString FullPath = FPaths::ConvertRelativePathToFull(Directory);

	// Get a handle to the directory with FILE_FLAG_BACKUP_SEMANTICS as per remarks for ReadDirectoryChanges on MSDN
	DirectoryHandle = ::CreateFile(
		*FullPath,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL
		);

	if ( DirectoryHandle == INVALID_HANDLE_VALUE )
	{
		// Failed to obtain a handle to this directory
		return false;
	}

	const bool bSuccess = !!::ReadDirectoryChangesW(
		DirectoryHandle,
		Buffer,
		BufferLength,
		bWatchSubtree,
		NotifyFilter,
		NULL,
		&Overlapped,
		&FDirectoryWatchRequestWindows::ChangeNotification);
	
	if ( !bSuccess  )
	{
		::CloseHandle(DirectoryHandle);
		DirectoryHandle = INVALID_HANDLE_VALUE;
		return false;
	}

	return true;
}

FDelegateHandle FDirectoryWatchRequestWindows::AddDelegate( const IDirectoryWatcher::FDirectoryChanged& InDelegate )
{
	Delegates.Add(InDelegate);
	return Delegates.Last().GetHandle();
}

bool FDirectoryWatchRequestWindows::RemoveDelegate( FDelegateHandle InHandle )
{
	return Delegates.RemoveAll([=](const IDirectoryWatcher::FDirectoryChanged& Delegate) {
		return Delegate.GetHandle() == InHandle;
	}) != 0;
}

bool FDirectoryWatchRequestWindows::HasDelegates() const
{
	return Delegates.Num() > 0;
}

HANDLE FDirectoryWatchRequestWindows::GetDirectoryHandle() const
{
	return DirectoryHandle;
}

void FDirectoryWatchRequestWindows::EndWatchRequest()
{
	if ( !bEndWatchRequestInvoked && !bPendingDelete )
	{
		if ( DirectoryHandle != INVALID_HANDLE_VALUE )
		{
#if WINVER >= 0x600		// CancelIoEx() is only supported on Windows Vista and higher
			CancelIoEx(DirectoryHandle, &Overlapped);
#else
			CancelIo(DirectoryHandle);
#endif
			// Clear the handle so we don't setup any more requests, and wait for the operation to finish
			HANDLE TempDirectoryHandle = DirectoryHandle;
			DirectoryHandle = INVALID_HANDLE_VALUE;
			WaitForSingleObjectEx(TempDirectoryHandle, 1000, true);
			
			::CloseHandle(TempDirectoryHandle);
		}
		else
		{
			// The directory handle was never opened
			bPendingDelete = true;
		}

		// Only allow this to be invoked once
		bEndWatchRequestInvoked = true;
	}
}

void FDirectoryWatchRequestWindows::ProcessPendingNotifications()
{
	// Trigger all listening delegates with the files that have changed
	if ( FileChanges.Num() > 0 )
	{
		for (int32 DelegateIdx = 0; DelegateIdx < Delegates.Num(); ++DelegateIdx)
		{
			Delegates[DelegateIdx].Execute(FileChanges);
		}

		FileChanges.Empty();
	}
}

void FDirectoryWatchRequestWindows::ProcessChange(uint32 Error, uint32 NumBytes)
{
	if (Error == ERROR_OPERATION_ABORTED) 
	{
		// The operation was aborted, likely due to EndWatchRequest canceling it.
		// Mark the request for delete so it can be cleaned up next tick.
		bPendingDelete = true;
		UE_CLOG(!GIsRequestingExit, LogDirectoryWatcher, Log, TEXT("A directory notification for '%s' was aborted."), *Directory);
		return; 
	}

	const bool bValidNotification = (Error != ERROR_IO_INCOMPLETE && NumBytes > 0 );
	const bool bAccessError = (Error == ERROR_ACCESS_DENIED);

	auto CloseHandleAndMarkForDelete = [this]()
	{
		::CloseHandle(DirectoryHandle);
		DirectoryHandle = INVALID_HANDLE_VALUE;
		bPendingDelete = true;
	};

	// Swap the pointer to the backbuffer so we can start a new read as soon as possible
	if ( bValidNotification )
	{
		Swap(Buffer, BackBuffer);
		check(Buffer && BackBuffer);
	}

	if ( !bValidNotification )
	{
		if (bAccessError)
		{
			CloseHandleAndMarkForDelete();
			UE_LOG(LogDirectoryWatcher, Log, TEXT("A directory notification failed for '%s' because it could not be accessed. Aborting watch request..."), *Directory);
			return;
		}
		else
		{
			UE_LOG(LogDirectoryWatcher, Log, TEXT("A directory notification failed for '%s' because it was empty or there was a buffer overflow. Attemping another request..."), *Directory);
		}
	}

	// Start up another read
	const bool bSuccess = !!::ReadDirectoryChangesW(
		DirectoryHandle,
		Buffer,
		BufferLength,
		bWatchSubtree,
		NotifyFilter,
		NULL,
		&Overlapped,
		&FDirectoryWatchRequestWindows::ChangeNotification);

	if ( !bSuccess  )
	{
		// Failed to re-create the read request.
		CloseHandleAndMarkForDelete();
		UE_LOG(LogDirectoryWatcher, Log, TEXT("A directory notification failed for '%s', and we were unable to create a new request."), *Directory);
		return;
	}

	// No need to process the change if we can not execute any delegates
	if ( !HasDelegates() || !bValidNotification )
	{
		return;
	}

	// Process the change
	void* InfoBase = BackBuffer;
	do
	{
		FILE_NOTIFY_INFORMATION* NotifyInfo = (FILE_NOTIFY_INFORMATION*)InfoBase;

		// Copy the WCHAR out of the NotifyInfo so we can put a NULL terminator on it and convert it to a FString
		FString LeafFilename;
		{
			// The Memcpy below assumes that WCHAR and TCHAR are equivalent (which they should be on Windows)
			static_assert(sizeof(WCHAR) == sizeof(TCHAR), "WCHAR is assumed to be the same size as TCHAR on Windows!");

			const int32 LeafFilenameLen = NotifyInfo->FileNameLength / sizeof(WCHAR);
			LeafFilename.GetCharArray().AddZeroed(LeafFilenameLen + 1);
			FMemory::Memcpy(LeafFilename.GetCharArray().GetData(), NotifyInfo->FileName, NotifyInfo->FileNameLength);
		}

		FFileChangeData::EFileChangeAction Action;
		switch(NotifyInfo->Action)
		{
			case FILE_ACTION_ADDED:
			case FILE_ACTION_RENAMED_NEW_NAME:
				Action = FFileChangeData::FCA_Added;
				break;

			case FILE_ACTION_REMOVED:
			case FILE_ACTION_RENAMED_OLD_NAME:
				Action = FFileChangeData::FCA_Removed;
				break;

			case FILE_ACTION_MODIFIED:
				Action = FFileChangeData::FCA_Modified;
				break;

			default:
				Action = FFileChangeData::FCA_Unknown;
				break;
		}
		FileChanges.Emplace(Directory / LeafFilename, Action);

		// If there is not another entry, break the loop
		if ( NotifyInfo->NextEntryOffset == 0 )
		{
			break;
		}

		// Adjust the offset and update the NotifyInfo pointer
		InfoBase = (uint8*)InfoBase + NotifyInfo->NextEntryOffset;
	}
	while(true);
}

void FDirectoryWatchRequestWindows::ChangeNotification(::DWORD Error, ::DWORD NumBytes, LPOVERLAPPED InOverlapped)
{
	FDirectoryWatchRequestWindows* Request = (FDirectoryWatchRequestWindows*)InOverlapped->hEvent;

	check(Request);
	Request->ProcessChange((uint32)Error, (uint32)NumBytes);
}
