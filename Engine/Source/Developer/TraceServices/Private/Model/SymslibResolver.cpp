// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

#include "SymslibResolver.h"
#include "Algo/Sort.h"
#include "Async/MappedFileHandle.h"
#include "Async/ParallelFor.h"
#include "Containers/StringView.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Logging/LogMacros.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "TraceServices/Model/AnalysisSession.h"
#include <atomic>

#include "symslib.h"

/////////////////////////////////////////////////////////////////////
DEFINE_LOG_CATEGORY_STATIC(LogSymslib, Log, All);

/////////////////////////////////////////////////////////////////////
namespace TraceServices {

/////////////////////////////////////////////////////////////////////

static const TCHAR* GUnknownModuleTextSymsLib = TEXT("Unknown");

/////////////////////////////////////////////////////////////////////

class FSymbolStringAllocator
{
public:
	FSymbolStringAllocator(ILinearAllocator& InAllocator, uint32 InBlockSize)
		: Allocator(InAllocator)
		, BlockSize(InBlockSize)
	{}

	const TCHAR* Store(const TCHAR* InString)
	{
		return Store(FStringView(InString));
	}

	const TCHAR* Store(const FStringView InString)
	{
		const uint32 StringSize = InString.Len() + 1;
		check(StringSize <= BlockSize);
		if (StringSize > BlockRemaining)
		{
			Block = (TCHAR*) Allocator.Allocate(BlockSize * sizeof(TCHAR));
			BlockRemaining = BlockSize;
			++BlockUsed;
		}
		const uint32 CopiedSize = InString.CopyString(Block, BlockRemaining - 1, 0);
		check(StringSize == CopiedSize + 1);
		Block[StringSize - 1] = TEXT('\0');
		BlockRemaining -= StringSize;
		const TCHAR* OutString = Block;
		Block += StringSize;
		return OutString;
	}

private:
	friend class FSymslibResolver;
	ILinearAllocator& Allocator;
	TCHAR* Block = nullptr;
	uint32 BlockSize;
	uint32 BlockRemaining = 0;
	uint32 BlockUsed = 0;
};
	
/////////////////////////////////////////////////////////////////////
FSymslibResolver::FSymslibResolver(IAnalysisSession& InSession)
	: Modules(InSession.GetLinearAllocator(), 128)
	, CancelTasks(false)
	, Session(InSession)
{
}

FSymslibResolver::~FSymslibResolver()
{
	CancelTasks = true;
	// Wait for cleanup task to finish
	CleanupTask->Wait();
}

void FSymslibResolver::QueueModuleLoad(const FStringView& ModulePath, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize)
{
	FWriteScopeLock _(ModulesLock);
	const FStringView ModuleName = FPathViews::GetCleanFilename(ModulePath);

	// Add module
	FModuleEntry* Entry = &Modules.PushBack();
	Entry->Base = Base;
	Entry->Size = Size;
	Entry->Name = Session.StoreString(ModuleName); //Allows safe sharing to UI
	Entry->Path = Session.StoreString(ModulePath);
	Entry->Status = EModuleStatus::Pending;
	Entry->Instance = nullptr;
	Entry->ImageId = TArrayView<const uint8>(ImageId, ImageIdSize);

	++ModulesDiscovered;

	// Queue up module to have symbols loaded. Run as background task as to not
	// interfere with Slate.
	++TasksInFlight;
	FFunctionGraphTask::CreateAndDispatchWhenReady(
		[this, Entry]
		{
			LoadModuleTracked(Entry);
			--TasksInFlight;
		}, TStatId{}, nullptr, ENamedThreads::AnyBackgroundThreadNormalTask);

	// Sort list according to base address
	SortedModules.Add(Entry);
	Algo::Sort(SortedModules, [](const FModuleEntry* Lhs, const FModuleEntry* Rhs) { return Lhs->Base < Rhs->Base; });
}

void FSymslibResolver::QueueSymbolResolve(uint64 Address, FResolvedSymbol* Symbol)
{
	FScopeLock _(&SymbolsQueueLock);
	MaybeDispatchQueuedAddresses();
	++SymbolsDiscovered;
	ResolveQueue.Add(FQueuedAddress{Address, Symbol});
}

void FSymslibResolver::OnAnalysisComplete()
{
	CleanupTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this]
	{
		// Dispatch any remaining requests.
		{
			FScopeLock _(&SymbolsQueueLock);
			DispatchQueuedAddresses();
			ResolveQueue.Reset();
		}

		// Wait for outstanding batches to complete.
		uint32 OutstandingTasks = TasksInFlight.load(std::memory_order_acquire);
		UE_LOG(LogSymslib, Display, TEXT("Waiting for %d outstanding tasks..."), OutstandingTasks);
		do
		{
			OutstandingTasks = TasksInFlight.load(std::memory_order_acquire);
			FPlatformProcess::Sleep(0.0);
		}
		while (OutstandingTasks > 0);

		// No tasks can be in running at this point.
		{
			FReadScopeLock _(FileHandlesAndRegionsLock);
			for (FMappedFileAndRegion& File : FileHandlesAndRegions)
			{
				// Release file region before releasing the file handle.
				delete File.Region;
				delete File.Handle;
			}
		}

		// Release memory used by syms library
		{
			FReadScopeLock _(ModulesLock);
			for (uint32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
			{
				FModuleEntry& Module = Modules[ModuleIndex];
				if (Module.Instance)
				{
					syms_quit(Module.Instance);
				}
			}
		}
		
		UE_LOG(LogSymslib, Display, TEXT("Allocated %.02f Mb of strings, %.02f Mb wasted."),
		       SymbolBytesAllocated / float(1024*1024), SymbolBytesWasted / float(1024*1024));
		
	});
}

void FSymslibResolver::GetStats(IModuleProvider::FStats* OutStats) const
{
	OutStats->ModulesDiscovered = ModulesDiscovered.load();
	OutStats->ModulesFailed = ModulesFailed.load();
	OutStats->ModulesLoaded = ModulesLoaded.load();
	OutStats->SymbolsDiscovered = SymbolsDiscovered.load();
	OutStats->SymbolsFailed = SymbolsFailed.load();
	OutStats->SymbolsResolved = SymbolsResolved.load();
}

/**
 * Checks if there are no modules in flight and that the queue has reached
 * the threshold for dispatching. Note that is up to the caller to synchronize.
 */
void FSymslibResolver::MaybeDispatchQueuedAddresses()
{
	const bool bModulesInFlight = (ModulesDiscovered.load() - ModulesFailed.load() - ModulesLoaded.load()) > 0;
	if (!bModulesInFlight && (ResolveQueue.Num() >= QueuedAddressLength))
	{
		DispatchQueuedAddresses();
		ResolveQueue.Reset();
	}
}

/**
 * Dispatches the currently queued addresses to be resolved. Note that is up to the caller to synchronize.
 */
void FSymslibResolver::DispatchQueuedAddresses()
{
	if (ResolveQueue.IsEmpty())
	{
		return;
	}
	
	TArray<FQueuedAddress> WorkingSet(ResolveQueue);
	TasksInFlight += SymbolTasksInParallel;
	const uint32 Stride = (WorkingSet.Num() + SymbolTasksInParallel - 1) / SymbolTasksInParallel;
	
	// Use background priority in order to not not interfere with Slate
	ParallelFor(SymbolTasksInParallel, [this, &WorkingSet, Stride](uint32 Index) {
		const uint32 StartIndex = Index * Stride;
		const uint32 EndIndex = FMath::Min(StartIndex + Stride, (uint32)WorkingSet.Num());
		TArrayView<FQueuedAddress> QueuedWork(&WorkingSet[StartIndex], EndIndex - StartIndex);
		ResolveSymbols(QueuedWork);
		--TasksInFlight;
	}, EParallelForFlags::BackgroundPriority);
}

void FSymslibResolver::ResolveSymbols(TArrayView<FQueuedAddress>& QueuedWork)
{
	// Create a local string allocator. We don't use the session string store due to contention when going wide. Since
	// the ModuleProvider already de-duplicates symbols we do not need this feature from the string store.
	FSymbolStringAllocator StringAllocator(Session.GetLinearAllocator(), (2 << 12) / sizeof(TCHAR) );
	for (const FQueuedAddress& ToResolve : QueuedWork)
	{
		if (CancelTasks.load(std::memory_order_relaxed))
		{
			break;
		}
		ResolveSymbolTracked(ToResolve.Address, ToResolve.Target, StringAllocator);
	}
	UE_LOG(LogSymslib, VeryVerbose, TEXT("String allocator used: %.02f kb, wasted: %.02f kb using %d blocks"),
		((StringAllocator.BlockUsed * StringAllocator.BlockSize - StringAllocator.BlockRemaining) * sizeof(TCHAR)) / 1024.0f,
		(StringAllocator.BlockRemaining * sizeof(TCHAR)) / 1024.0f,
		StringAllocator.BlockUsed);
	SymbolBytesAllocated.fetch_add(StringAllocator.BlockUsed * StringAllocator.BlockSize * sizeof(TCHAR));
	SymbolBytesWasted.fetch_add(StringAllocator.BlockRemaining * sizeof(TCHAR));
}

FSymslibResolver::FModuleEntry* FSymslibResolver::GetModuleForAddress(uint64 Address)
{
	FReadScopeLock _(ModulesLock);
	const int32 EntryIdx = Algo::LowerBoundBy(SortedModules, Address, [](const FModuleEntry* Entry) { return Entry->Base; }) - 1;
	if (EntryIdx < 0 || EntryIdx >= SortedModules.Num())
	{
		return nullptr;
	}
	return SortedModules[EntryIdx];
}

void FSymslibResolver::UpdateResolvedSymbol(FResolvedSymbol* Symbol, ESymbolQueryResult Result, const TCHAR* Module, const TCHAR* Name, const TCHAR* File, uint16 Line)
{
	Symbol->Module = Module;
	Symbol->Name = Name;
	Symbol->File = File;
	Symbol->Line = Line;
	Symbol->Result.store(Result, std::memory_order_release);
}

void FSymslibResolver::LoadModuleTracked(FModuleEntry* Module)
{
	if (CancelTasks.load(std::memory_order_relaxed))
	{
		return;
	}
	
	const EModuleStatus Status = LoadModule(Module);
	if (Status == EModuleStatus::Loaded)
	{
		++ModulesLoaded;
	}
	else
	{
		++ModulesFailed;
		Module->Status.store(Status);
	}

	//  Make the status visible
	Module->Status.store(Status);

	// If this is the last module in flight dispatch pending queries
	{
		FScopeLock _(&SymbolsQueueLock);
		MaybeDispatchQueuedAddresses();
	}
}

FSymslibResolver::EModuleStatus FSymslibResolver::LoadModule(FModuleEntry* Module)
{
	IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

	// Initialize the instance
	Module->Instance = syms_init_ex(Module->Base);
	SymsInstance* ModuleInstance = Module->Instance;

	if (!ModuleInstance)
	{
		UE_LOG(LogSymslib, Error, TEXT("Failed to initialize '%s'"), Module->Name);
		return EModuleStatus::Failed;
	}

	if (!PlatformFile->FileExists(Module->Path))
	{
		UE_LOG(LogSymslib, Warning, TEXT("File '%s' does not exist"), Module->Path);
		return EModuleStatus::Failed;
	}

	// Map the image file to memory
	IMappedFileHandle* ImageFileHandle = PlatformFile->OpenMapped(Module->Path);
	if (ImageFileHandle == nullptr)
	{
		UE_LOG(LogSymslib, Warning, TEXT("Failed to open '%s'"), Module->Path);
		return EModuleStatus::Failed;
	}
	IMappedFileRegion* ImageFileRegion = ImageFileHandle->MapRegion(0, ImageFileHandle->GetFileSize());
	check(ImageFileRegion != nullptr);

	// Track all open handles
	TrackFileHandlesAndRegions(ImageFileHandle, ImageFileRegion);

	// Load the image
	SymsErrorCode Result = syms_load_image(ModuleInstance, (void*)ImageFileRegion->GetMappedPtr(), ImageFileRegion->GetMappedSize(), 0);

	if (SYMS_RESULT_FAIL(Result))
	{
		UE_LOG(LogSymslib, Warning, TEXT("Failed to load '%s'"), Module->Path);
		return EModuleStatus::Failed;
	}

	// Iterate and map all the debug files
	const FStringView ModuleBasePath = FPathViews::GetPath(Module->Path);
	TStringBuilder<256> DebugFilePath;
	TArray<SymsFile> Files;

	SymsDebugFileIter DebugFiles;
	if (syms_debug_file_iter_init(&DebugFiles, ModuleInstance))
	{
		SymsString Path;
		while (syms_debug_file_iter_next(&DebugFiles, &Path))
		{
			// Map the debug file
			DebugFilePath.Reset();
			FPathViews::Append(DebugFilePath, ModuleBasePath);
			FPathViews::Append(DebugFilePath, ANSI_TO_TCHAR(Path.data));

			if (PlatformFile->FileExists(*DebugFilePath))
			{
				IMappedFileHandle* FileHandle = PlatformFile->OpenMapped(*DebugFilePath);
				if (FileHandle != nullptr)
				{
					IMappedFileRegion* FileRegion = FileHandle->MapRegion(0, FileHandle->GetFileSize());
					check(FileRegion != nullptr);

					Files.Push(SymsFile{
						Path.data,
						(void*)FileRegion->GetMappedPtr(),
						(uint64)FileRegion->GetMappedSize()
					});

					TrackFileHandlesAndRegions(FileHandle, FileRegion);
				}
				else
				{
					UE_LOG(LogSymslib, Warning, TEXT("Could not open the debug file '%s'"), *DebugFilePath);
				}
			}
		}
	}

	// Prepare to load the debug files
	Result = syms_load_debug_info_ex(ModuleInstance, Files.GetData(), Files.Num(), SYMS_LOAD_DEBUG_INFO_FLAGS_DEFER_BUILD_MODULE);

	if (SYMS_RESULT_FAIL(Result))
	{
		UE_LOG(LogSymslib, Display, TEXT("No debug information for '%s'"), Module->Path);
		return EModuleStatus::Failed;
	}

	// Split up the work of actually loading debug info
	const uint32 Count = syms_get_module_build_count(ModuleInstance);
	// Each work item needs some memory
	TArray<SymsArena*> Arenas;
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		Arenas.Add(syms_borrow_memory(ModuleInstance));
	}
	ParallelFor(Count, [ModuleInstance, &Arenas](uint32 Index)
	{
		syms_build_module(ModuleInstance, (SymsModID)Index, Arenas[Index]);
	});

	// Check age of debug data
	if (!Module->ImageId.IsEmpty() && ModuleInstance->img.type == SYMS_IMAGE_NT)
	{
		// For Pdbs checksum is a 16 byte guid and 8 byte unsigned integer for age
		static_assert(sizeof(FGuid) == 16, "Expected 16 byte FGuid");
		check(Module->ImageId.Num() == 20);
		FGuid* ModuleGuid = (FGuid*) Module->ImageId.GetData();
		uint32* ModuleAge = ((uint32*) Module->ImageId.GetData()) + 4;
		SymsDebugInfoPdb* PdbInfo = (SymsDebugInfoPdb*)ModuleInstance->debug_info.impl;
		FGuid* PdbGuid = (FGuid*)&PdbInfo->context.auth_guid;
		const uint32 PdbAge = PdbInfo->context.age;
		if (*ModuleGuid != *PdbGuid || *ModuleAge != PdbAge)
		{
			UE_LOG(LogSymslib, Warning, TEXT("Symbols for '%s' does not match traced binary."), Module->Name);
			return EModuleStatus::VersionMismatch;
		}
	}

	const uint32 SymbolCount = syms_get_proc_count(ModuleInstance); // get symbol count
	const uint32 LineCount = syms_get_line_count(ModuleInstance); // get line count
	UE_LOG(LogSymslib, Display, TEXT("Loaded symbols for '%s' at base 0x%016llx, %u symbols and %u lines."), Module->Name, Module->Base, SymbolCount, LineCount);

	return EModuleStatus::Loaded;
}

void FSymslibResolver::ResolveSymbolTracked(uint64 Address, FResolvedSymbol* Target, FSymbolStringAllocator& StringAllocator)
{
	if (!ResolveSymbol(Address, Target, StringAllocator))
	{
		++SymbolsFailed;
	}
	else
	{
		++SymbolsResolved;
	}
}

bool FSymslibResolver::ResolveSymbol(uint64 Address, FResolvedSymbol* Target, FSymbolStringAllocator& StringAllocator)
{
	FModuleEntry* Module = GetModuleForAddress(Address);
	if (!Module)
	{
		UE_LOG(LogSymslib, Warning, TEXT("No module mapped to address 0x%016llx."), Address);
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotLoaded, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, 0);
		return false;
	}

	EModuleStatus Status = Module->Status.load();
	while (Status == EModuleStatus::Pending)
	{
		//todo: yield
		Status = Module->Status.load();
	}

	switch (Status)
	{
	case EModuleStatus::Failed:
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotLoaded, Module->Name, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, 0);
		return false;
	case EModuleStatus::VersionMismatch:
		UpdateResolvedSymbol(Target, ESymbolQueryResult::Mismatch, Module->Name, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, 0);
		return false;
	default:
		break;
	}

	SymsProc Proc;
	SymsSourceFileMap File;

	// Find procedure and source file for address
	if (!syms_proc_from_va(Module->Instance, Address, &Proc) || !syms_va_to_src(Module->Instance, Address, &File))
	{
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotFound, Module->Name, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, 0); 
		return false;
	}

	constexpr uint32 MaxStringSize = 1024;

	// Read the symbol name
	ANSICHAR SymbolName[MaxStringSize];
	SymbolName[MaxStringSize - 1] = 0;
	syms_read_strref(Module->Instance, &Proc.name_ref, SymbolName, sizeof(SymbolName) - 1);
	check(SymbolName[MaxStringSize - 1] == 0);

	// Read the source location
	ANSICHAR SourceFile[MaxStringSize];
	SourceFile[MaxStringSize - 1] = 0;
	syms_read_strref(Module->Instance, &File.file.name, SourceFile, sizeof(SourceFile) - 1);
	check(SourceFile[MaxStringSize - 1] == 0);

	const TCHAR* SymbolNamePersistent =  StringAllocator.Store(ANSI_TO_TCHAR(SymbolName));
	const TCHAR* SourceFilePersistent = StringAllocator.Store(ANSI_TO_TCHAR(SourceFile));

	// Store the strings and update the target data
	UpdateResolvedSymbol(
		Target,
		ESymbolQueryResult::OK,
		Module->Name,
		SymbolNamePersistent,
		SourceFilePersistent,
		File.line.ln
	);

	return true;
}

void FSymslibResolver::TrackFileHandlesAndRegions(IMappedFileHandle* FileHandle, IMappedFileRegion* FileRegion)
{
	FWriteScopeLock _(FileHandlesAndRegionsLock);
	FileHandlesAndRegions.Add(FMappedFileAndRegion{ FileHandle, FileRegion });
}

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
