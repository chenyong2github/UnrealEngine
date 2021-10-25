// Copyright Epic Games, Inc. All Rights Reserved.

#include "Linux/DirectoryWatchRequestLinux.h"
#include "HAL/FileManager.h"
#include "DirectoryWatcherPrivate.h"

// To see inotify watch events:
//   TestPAL dirwatcher -LogCmds="LogDirectoryWatcher VeryVerbose"

#define EVENT_SIZE     ( sizeof(struct inotify_event) )
#define EVENT_BUF_LEN  ( 1024 * ( EVENT_SIZE + 16 ) )

static bool GDumpStats = false;
static bool GDumpedError = false;
static FString GINotifyErrorMsg;

static uint32 GetPathNameHash(const FString& Key)
{
	const TCHAR* Str = &Key[0];
	uint32 StrLen = sizeof(TCHAR) * Key.Len();

	return CityHash64(reinterpret_cast<const char*>(Str), StrLen);
}

FDirectoryWatchRequestLinux::FDirectoryWatchRequestLinux()
:	bWatchSubtree(false)
,	bEndWatchRequestInvoked(false)
,	FileDescriptor(-1)
{
}

FDirectoryWatchRequestLinux::~FDirectoryWatchRequestLinux()
{
	Shutdown();
}

void FDirectoryWatchRequestLinux::Shutdown()
{
	// Go through and inotify_rm_watch all our watch descriptors
	for (auto MapIt = WatchDescriptorsToPaths.CreateConstIterator(); MapIt; ++MapIt)
	{
		inotify_rm_watch(FileDescriptor, MapIt->Key);
	}

	WatchDescriptorsToPaths.Empty();
	PathNameHashSet.Empty();

	if (FileDescriptor != -1)
	{
		close(FileDescriptor);
		FileDescriptor = -1;
	}
}

bool FDirectoryWatchRequestLinux::Init(const FString& InDirectory, uint32 Flags)
{
	if (InDirectory.Len() == 0)
	{
		// Verify input
		return false;
	}

	Shutdown();

	bWatchSubtree = (Flags & IDirectoryWatcher::WatchOptions::IgnoreChangesInSubtree) == 0;

	bEndWatchRequestInvoked = false;

	// Make sure the path is absolute
	WatchDirectory = FPaths::ConvertRelativePathToFull(InDirectory);

	UE_LOG(LogDirectoryWatcher, Verbose, TEXT("Adding watch for directory tree '%s'"), *WatchDirectory);

	FileDescriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (FileDescriptor == -1)
	{
		if (errno == EMFILE)
		{
			SetINotifyErrorMsg(TEXT("Failed to init inotify (ran out of inotify instances)"));
		}
		else
		{
			UE_LOG(LogDirectoryWatcher, Error, TEXT("Failed to init inotify (errno=%d, %s)"), errno, UTF8_TO_TCHAR(strerror(errno)));
		}
		return false;
	}

	// Find all subdirs and add inotify watch requests
	WatchDirectoryTree(WatchDirectory, nullptr);

	return true;
}

FDelegateHandle FDirectoryWatchRequestLinux::AddDelegate(const IDirectoryWatcher::FDirectoryChanged& InDelegate, uint32 Flags)
{
	Delegates.Emplace(InDelegate, Flags);
	return Delegates.Last().Key.GetHandle();
}

bool FDirectoryWatchRequestLinux::RemoveDelegate(FDelegateHandle InHandle)
{
	return Delegates.RemoveAll([=](const FWatchDelegate& Delegate) {
		return Delegate.Key.GetHandle() == InHandle;
	}) != 0;
}

bool FDirectoryWatchRequestLinux::HasDelegates() const
{
	return Delegates.Num() > 0;
}

void FDirectoryWatchRequestLinux::EndWatchRequest()
{
	bEndWatchRequestInvoked = true;
}

void FDirectoryWatchRequestLinux::ProcessNotifications(TMap<FString, FDirectoryWatchRequestLinux*>& RequestMap)
{
	// Trigger any file change notification delegates
	for (auto MapIt = RequestMap.CreateConstIterator(); MapIt; ++MapIt)
	{
		FDirectoryWatchRequestLinux &WatchRequest = *MapIt.Value();

		WatchRequest.ProcessPendingNotifications();
	}

	DumpINotifyErrorDetails(RequestMap);
}

void FDirectoryWatchRequestLinux::DumpStats(TMap<FString, FDirectoryWatchRequestLinux*>& RequestMap)
{
	GDumpStats = true;
	DumpINotifyErrorDetails(RequestMap);
}

void FDirectoryWatchRequestLinux::ProcessPendingNotifications()
{
	/** Each FFileChangeData tracks whether it is a directory or not */
	TArray<TPair<FFileChangeData, bool>> FileChanges;

	ProcessChanges(FileChanges);

	// Trigger all listening delegates with the files that have changed
	if (FileChanges.Num() > 0)
	{
		TMap<uint32, TArray<FFileChangeData>> FileChangeCache;

		for (const FWatchDelegate& Delegate : Delegates)
		{
			// Filter list of all file changes down to ones that just match this delegate's flags
			TArray<FFileChangeData>* CachedChanges = FileChangeCache.Find(Delegate.Value);

			if (CachedChanges)
			{
				Delegate.Key.Execute(*CachedChanges);
			}
			else
			{
				const bool bIncludeDirs = (Delegate.Value & IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges) != 0;
				TArray<FFileChangeData>& Changes = FileChangeCache.Add(Delegate.Value);

				for (const TPair<FFileChangeData, bool>& FileChangeData : FileChanges)
				{
					if (!FileChangeData.Value || bIncludeDirs)
					{
						Changes.Add(FileChangeData.Key);
					}
				}
				Delegate.Key.Execute(Changes);
			}
		}

		FileChanges.Empty();
	}
}

void FDirectoryWatchRequestLinux::WatchDirectoryTree(const FString & RootAbsolutePath, TArray<TPair<FFileChangeData, bool>>* FileChanges)
{
	if (bEndWatchRequestInvoked || (FileDescriptor == -1))
	{
		return;
	}

	// If this isn't our root watch directory or under it, don't watch
	if (!RootAbsolutePath.StartsWith(WatchDirectory, ESearchCase::CaseSensitive) ||
		(!bWatchSubtree && (RootAbsolutePath != WatchDirectory)))
	{
		return;
	}

	UE_LOG(LogDirectoryWatcher, VeryVerbose, TEXT("Watching tree '%s'"), *RootAbsolutePath);

	if (FileChanges)
	{
		FileChanges->Emplace(FFileChangeData(RootAbsolutePath, FFileChangeData::FCA_Added), true);
	}

	TArray<FString> AllFiles;
	if (bWatchSubtree)
	{
		IPlatformFile::GetPlatformPhysical().IterateDirectoryRecursively(*RootAbsolutePath,
			[&AllFiles, FileChanges](const TCHAR* Name, bool bIsDirectory)
				{
					if (bIsDirectory)
					{
						AllFiles.Add(Name);
					}

					if (FileChanges)
					{
						FileChanges->Emplace(FFileChangeData(Name, FFileChangeData::FCA_Added), bIsDirectory);
					}
					return true;
				});
	}

	// Add root path
	AllFiles.Add(RootAbsolutePath);

	for (const FString& FolderName: AllFiles)
	{
		uint32 PathNameHash = GetPathNameHash(FolderName);

		if (PathNameHashSet.Find(PathNameHash) == nullptr)
		{
			int32 NotifyFilter = IN_CREATE | IN_MOVE | IN_MODIFY | IN_DELETE;
			int32 WatchDescriptor = inotify_add_watch(FileDescriptor, TCHAR_TO_UTF8(*FolderName), NotifyFilter);

			if (WatchDescriptor == -1)
			{
				if (errno == ENOSPC)
				{
					FString ErrorMsg = FString::Printf(
						TEXT("inotify_add_watch cannot watch folder %s (Out of inotify watches)"), *FolderName);
					SetINotifyErrorMsg(ErrorMsg);
				}
				else
				{
					UE_LOG(LogDirectoryWatcher, Warning, TEXT("inotify_add_watch cannot watch folder %s (errno = %d, %s)"),
							*FolderName, errno, UTF8_TO_TCHAR(strerror(errno)));
				}
			}
			else
			{
				UE_LOG(LogDirectoryWatcher, VeryVerbose, TEXT("+ Added a watch %d for '%s'"), WatchDescriptor, *FolderName);

				// Set the inotify watch descriptor -> folder name mapping
				WatchDescriptorsToPaths.Add(WatchDescriptor, FolderName);

				// Add hashed directory path
				PathNameHashSet.Add(PathNameHash);
			}
		}
	}
}

void FDirectoryWatchRequestLinux::UnwatchDirectoryTree(const FString& RootAbsolutePath)
{
	UE_LOG(LogDirectoryWatcher, VeryVerbose, TEXT("Unwatching tree '%s'"), *RootAbsolutePath);

	for (auto MapIt = WatchDescriptorsToPaths.CreateIterator(); MapIt; ++MapIt)
	{
		int WatchDescriptor = MapIt->Key;
		const FString& Path = MapIt->Value;

		if (Path.StartsWith(RootAbsolutePath, ESearchCase::CaseSensitive))
		{
			UE_LOG(LogDirectoryWatcher, VeryVerbose, TEXT("- Removing a watch %d for '%s'"), WatchDescriptor, *Path);

			// delete the descriptor
			int RetVal = inotify_rm_watch(FileDescriptor, WatchDescriptor);

			// This function may be called when root path has been deleted, and inotify_rm_watch() will fail
			// with an EINVAL when removing a watch on a deleted file.
			if (RetVal == -1 && errno != EINVAL)
			{
				UE_LOG(LogDirectoryWatcher, Warning, TEXT("inotify_rm_watch cannot remove descriptor %d for folder '%s' (errno = %d, %s)"),
						WatchDescriptor, *Path, errno, ANSI_TO_TCHAR(strerror(errno)));
			}

			// Safe version of:
			//   WatchDescriptorsToPaths.Remove(WatchDescriptor);
			MapIt.RemoveCurrent();

			PathNameHashSet.Remove(GetPathNameHash(Path));
		}
	}
}

static FString INotifyFlagsToStr(uint32 INotifyFlags)
{
#if UE_BUILD_SHIPPING
	return FString();
#else
	FString Ret = TEXT("[");

#define _XTAG(_x) if (INotifyFlags & _x) Ret += FString(TEXT(" ")) + TEXT(#_x)
	_XTAG(IN_ACCESS);
	_XTAG(IN_MODIFY);
	_XTAG(IN_ATTRIB);
	_XTAG(IN_CLOSE_WRITE);
	_XTAG(IN_CLOSE_NOWRITE);
	_XTAG(IN_OPEN);
	_XTAG(IN_MOVED_FROM);
	_XTAG(IN_MOVED_TO);
	_XTAG(IN_CREATE);
	_XTAG(IN_DELETE);
	_XTAG(IN_DELETE_SELF);
	_XTAG(IN_MOVE_SELF);
	_XTAG(IN_UNMOUNT);
	_XTAG(IN_Q_OVERFLOW);
	_XTAG(IN_IGNORED);
	_XTAG(IN_ISDIR);
#undef _XTAG

	Ret += TEXT(" ]");
	return Ret;
#endif
}

void FDirectoryWatchRequestLinux::ProcessChanges(TArray<TPair<FFileChangeData, bool>>& FileChanges)
{
	uint8_t Buffer[EVENT_BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));

	if (FileDescriptor == -1)
	{
		return;
	}

	// Loop while events can be read from inotify file descriptor
	for (;;)
	{
		// Read event stream
		ssize_t Len = read(FileDescriptor, Buffer, EVENT_BUF_LEN);

		// If the non-blocking read() found no events to read, then it returns -1 with errno set to EAGAIN.
		if (Len == -1 && errno != EAGAIN)
		{
			UE_LOG(LogDirectoryWatcher, Error, TEXT("FDirectoryWatchRequestLinux::ProcessChanges() read() error getting events for path '%s' (errno = %d, %s)"),
				*WatchDirectory, errno, ANSI_TO_TCHAR(strerror(errno)));
			break;
		}

		if (Len <= 0)
		{
			break;
		}

		if (bEndWatchRequestInvoked)
		{
			continue;
		}

		// Loop over all events in the buffer
		uint8_t* Ptr = Buffer;
		while (Ptr < Buffer + Len)
		{
			const struct inotify_event* Event;
			FFileChangeData::EFileChangeAction Action = FFileChangeData::FCA_Unknown;

			Event = reinterpret_cast<const struct inotify_event *>(Ptr);
			Ptr += EVENT_SIZE + Event->len;

			// Skip if overflowed
			if ((Event->wd != -1) && (Event->mask & IN_Q_OVERFLOW) == 0)
			{
				const FString *EventPathPtr = WatchDescriptorsToPaths.Find(Event->wd);

				UE_LOG(LogDirectoryWatcher, VeryVerbose, TEXT("Event: watch descriptor %d, mask 0x%08x, EventPath: '%s' Event Name: '%s' %s"),
					Event->wd, Event->mask, EventPathPtr ? *(*EventPathPtr) : TEXT("nullptr"), ANSI_TO_TCHAR(Event->name), *INotifyFlagsToStr(Event->mask));

				// If we're geting multiple events (e.g. DELETE, IGNORED) we could have removed descriptor on previous iteration,
				// so we need to handle inability to find it in the map
				if (EventPathPtr)
				{
					const FString& EventPath = *EventPathPtr;
					FString AffectedFile = EventPath / UTF8_TO_TCHAR(Event->name); // By default, some events report about the file itself

					bool bIsDir = (Event->mask & IN_ISDIR) != 0;

					if ((Event->mask & IN_CREATE) || (Event->mask & IN_MOVED_TO))
					{
						// IN_CREATE: File/directory created in watched directory
						// IN_MOVED_TO: Generated for the directory containing the new filename when a file is renamed
						if (bIsDir)
						{
							// If a directory was created/moved, watch it and add changes to FileChanges.
							// Leave Action as FCA_Unknown so nothing gets added down below.
							WatchDirectoryTree(AffectedFile, &FileChanges);
						}
						else
						{
							Action = FFileChangeData::FCA_Added;
						}
					}
					else if (Event->mask & IN_MODIFY)
					{
						// IN_MODIFY: File was modified
						// If a directory was modified, we expect to get events from already watched files in it
						Action = FFileChangeData::FCA_Modified;
					}
					// Check if the file/directory itself has been deleted (IGNORED can also be sent on delete)
					else if ((Event->mask & IN_DELETE_SELF) || (Event->mask & IN_IGNORED) || (Event->mask & IN_UNMOUNT))
					{
						// IN_DELETE_SELF: Watched file/directory was itself deleted.
						//   In addition, an IN_IGNORED event will subsequently be generated for the watch descriptor
						// IN_UNMOUNT: Filesystem containing watched object was unmounted.
						//   In addition, an IN_IGNORED event will subsequently be generated for the watch descriptor
						// IN_IGNORED: Watch was removed explicitly (inotify_rm_watch(2)) or
						//   automatically (file was deleted, or filesystem was unmounted).

						// If a directory was deleted, we expect to get events from already watched files in it
						AffectedFile = EventPath;

						if (bIsDir)
						{
							UnwatchDirectoryTree(EventPath);
						}
						else
						{
							// NOTE: I don't *believe* this code should ever get called? We only watch directories.
							// But to be on the safe side, still handling this case...

							// inotify_rm_watch() may fail here as watch descriptor is potentially no longer valid, so ignore return
							inotify_rm_watch(FileDescriptor, Event->wd);

							// Remove from both mappings
							WatchDescriptorsToPaths.Remove(Event->wd);
							PathNameHashSet.Remove(GetPathNameHash(EventPath));
						}

						Action = FFileChangeData::FCA_Removed;
					}
					else if ((Event->mask & IN_DELETE) || (Event->mask & IN_MOVED_FROM))
					{
						// IN_DELETE: File/directory deleted from watched directory
						// IN_MOVED_FROM: Generated for the directory containing the old filename when a file is renamed

						// If a directory was deleted/moved, unwatch it
						if (bIsDir)
						{
							UnwatchDirectoryTree(AffectedFile);
						}

						Action = FFileChangeData::FCA_Removed;
					}

					if (Event->len && (Action != FFileChangeData::FCA_Unknown))
					{
						FileChanges.Emplace(FFileChangeData(AffectedFile, Action), bIsDir);
					}
				}
			}
		}
	}
}

static uint32 get_inotify_procfs_value(const char *FileName)
{
	char Buf[256];
	uint32 InterfaceVal = 0;
	
	snprintf(Buf, sizeof(Buf), "/proc/sys/fs/inotify/%s", FileName);
	Buf[sizeof(Buf) - 1] = 0;
	
	FILE* FilePtr = fopen(Buf, "r");
	if (FilePtr)
	{
		if (fscanf(FilePtr, "%u", &InterfaceVal) != 1)
		{
			InterfaceVal = 0;
		}
		fclose(FilePtr);
	}

	return InterfaceVal;
}

void FDirectoryWatchRequestLinux::SetINotifyErrorMsg(const FString &ErrorMsg)
{
	if (!GINotifyErrorMsg.Len())
	{
		GINotifyErrorMsg = ErrorMsg;
	}
}

static FString GetLinkName(const char *Pathname)
{
	FString Result;
	char Filename[PATH_MAX + 1];

	ssize_t Ret = readlink(Pathname, Filename, sizeof(Filename));
	if ((Ret > 0) && (Ret < sizeof(Filename)))
	{
		Filename[Ret] = 0;
		Result = Filename;
	}
	return Result;
}

static uint32 INotifyParseFDInfoFile(const FString& Executable, int Pid, const char *d_name)
{
	uint32 INotifyCount = 0;

	FILE* FilePtr = fopen(d_name, "r");
	if (FilePtr)
	{
		char line_buf[256];

		for (;;)
		{
			if (!fgets(line_buf, sizeof(line_buf), FilePtr))
			{
				break;
			}

			if (!strncmp(line_buf, "inotify ", 8))
			{
				INotifyCount++;
			}
		}

		fclose(FilePtr);
	}

	return INotifyCount;
}

static void INotifyParseFDDir(const FString& Executable, int Pid, uint32 &INotifyCountTotal, uint32& INotifyInstancesTotal)
{
	char Buf[256];
	uint32 INotifyCount = 0;
	uint32 INotifyInstances = 0;

	snprintf(Buf, sizeof(Buf), "/proc/%d/fd", Pid);
	Buf[sizeof(Buf) - 1] = 0;

	DIR* dir_fd = opendir(Buf);
	if (dir_fd)
	{
		for (;;)
		{
			struct dirent* dp_fd = readdir(dir_fd);
			if (!dp_fd)
			{
				break;
			}

			if ((dp_fd->d_type == DT_LNK) && isdigit(dp_fd->d_name[0]))
			{
				snprintf(Buf, sizeof(Buf), "/proc/%d/fd/%s", Pid, dp_fd->d_name);
				Buf[sizeof(Buf) - 1] = 0;

				FString Filename = GetLinkName(Buf);
				if (Filename == TEXT("anon_inode:inotify"))
				{
					snprintf(Buf, sizeof(Buf), "/proc/%d/fdinfo/%s", Pid, dp_fd->d_name);
					Buf[sizeof(Buf) - 1] = 0;

					uint32 Count = INotifyParseFDInfoFile(Executable, Pid, Buf);
					if (Count)
					{
						INotifyInstances++;
						INotifyCount += Count;
					}
				}
			}
		}

		closedir(dir_fd);
	}

	if (INotifyCount)
	{
		FString ExeName = FPaths::GetCleanFilename(Executable);

		UE_LOG(LogDirectoryWatcher, Warning, TEXT("  %s (pid %d) watches:%u instances:%u"), *ExeName, Pid, INotifyCount, INotifyInstances);

		INotifyCountTotal += INotifyCount;
		INotifyInstancesTotal += INotifyInstances;
	}
}

static void INotifyDumpProcessStats()
{
	uint32 INotifyCountTotal = 0;
	uint32 INotifyInstancesTotal = 0;

	DIR* dir_proc = opendir("/proc");
	if (dir_proc)
	{
		for (;;)
		{
			struct dirent* dp_proc = readdir(dir_proc);
			if (!dp_proc)
			{
				break;
			}

			if ((dp_proc->d_type == DT_DIR) && isdigit(dp_proc->d_name[0]))
			{
				char Buf[256];
				int32 Pid = atoi(dp_proc->d_name);

				snprintf(Buf, sizeof(Buf), "/proc/%d/exe", Pid);
				Buf[sizeof(Buf) - 1] = 0;

				FString Executable = GetLinkName(Buf);
				if (Executable.Len())
				{
					INotifyParseFDDir(Executable, Pid, INotifyCountTotal, INotifyInstancesTotal);
				}
			}
		}

		closedir(dir_proc);
	}

	UE_LOG(LogDirectoryWatcher, Warning, TEXT("Total inotify Watches:%u Instances:%u"), INotifyCountTotal, INotifyInstancesTotal);
}

void FDirectoryWatchRequestLinux::DumpINotifyErrorDetails(TMap<FString, FDirectoryWatchRequestLinux*>& RequestMap)
{
	if (!GDumpStats)
	{
		if (GDumpedError || !GINotifyErrorMsg.Len())
		{
			return;
		}
		GDumpedError = true;

		UE_LOG(LogDirectoryWatcher, Warning, TEXT("inotify error: %s"), *GINotifyErrorMsg);
	}
	GDumpStats = false;

	uint32 MaxQueuedEvents = get_inotify_procfs_value("max_queued_events");
	uint32 MaxUserInstances = get_inotify_procfs_value("max_user_instances");
	uint32 MaxUserWatches = get_inotify_procfs_value("max_user_watches");

	UE_LOG(LogDirectoryWatcher, Warning, TEXT("inotify limits"));
	UE_LOG(LogDirectoryWatcher, Warning, TEXT("  max_queued_events: %u"), MaxQueuedEvents);
	UE_LOG(LogDirectoryWatcher, Warning, TEXT("  max_user_instances: %u"), MaxUserInstances);
	UE_LOG(LogDirectoryWatcher, Warning, TEXT("  max_user_watches: %u"), MaxUserWatches);

	UE_LOG(LogDirectoryWatcher, Warning, TEXT("inotify per-process stats"));
	INotifyDumpProcessStats();

	uint32 Count = 0;
	UE_LOG(LogDirectoryWatcher, Warning, TEXT("Current watch requests"));

	for (auto MapIt = RequestMap.CreateConstIterator(); MapIt; ++MapIt)
	{
		uint32 DirCount = 1;
		FDirectoryWatchRequestLinux &WatchRequest = *MapIt.Value();

		// Get actual count of subdirectories
		if (WatchRequest.bWatchSubtree)
		{
			IPlatformFile::GetPlatformPhysical().IterateDirectoryRecursively(*WatchRequest.WatchDirectory,
				[&DirCount](const TCHAR* Name, bool bIsDirectory)
				{
					DirCount += bIsDirectory;
					return true;
				});
		}

		UE_LOG(LogDirectoryWatcher, Warning, TEXT("  %s: %u watches (%u total dirs)"),
			*WatchRequest.WatchDirectory, WatchRequest.WatchDescriptorsToPaths.Num(), DirCount);
		
		Count += WatchRequest.WatchDescriptorsToPaths.Num();
	}
	UE_LOG(LogDirectoryWatcher, Warning, TEXT("Total UE inotify Watches:%u Instances:%u"), Count, RequestMap.Num());
}

#if !UE_BUILD_SHIPPING

static bool INotifyCommandHandler(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("DumpINotifyStats")))
	{
		UE_LOG(LogDirectoryWatcher, Warning, TEXT("Dumping inotify stats"));
		GDumpStats = true;
		return true;
	}

	return false;
}

FStaticSelfRegisteringExec FDirectoryWatchRequestLinuxExecs(INotifyCommandHandler);

#endif
