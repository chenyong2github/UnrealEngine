// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

#include "SymslibResolver.h"
#include "Algo/ForEach.h"
#include "Algo/Sort.h"
#include "Async/MappedFileHandle.h"
#include "Async/ParallelFor.h"
#include "Containers/StringView.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTLS.h"
#include "Logging/LogMacros.h"
#include "Misc/CString.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "TraceServices/Model/AnalysisSession.h"
#include <atomic>

/////////////////////////////////////////////////////////////////////
DEFINE_LOG_CATEGORY_STATIC(LogSymslib, Log, All);

/////////////////////////////////////////////////////////////////////
namespace TraceServices {

/////////////////////////////////////////////////////////////////////

static const TCHAR* GUnknownModuleTextSymsLib = TEXT("Unknown");

/////////////////////////////////////////////////////////////////////


namespace
{
	struct FAutoMappedFile
	{
		FString FilePath;

		TUniquePtr<IMappedFileHandle> Handle;
		TUniquePtr<IMappedFileRegion> Region;

		bool Load(const TCHAR* FileName)
		{
			Handle.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenMapped(FileName));
			Region.Reset(Handle.IsValid() ? Handle->MapRegion(0, Handle->GetFileSize()) : nullptr);
			return Region.IsValid();
		}

		SYMS_String8 GetData() const
		{
			return Region.IsValid()
				? syms_str8((SYMS_U8*)Region->GetMappedPtr(), Region->GetMappedSize())
				: syms_str8(nullptr, 0);
		}
	};

	static FString FindSymbolFile(const FString& Path)
	{
		IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

		// Extract only filename part in case Path is absolute path
		FString FileName = FPaths::GetCleanFilename(Path);

		FString SymbolPath = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_INSIGHTS_SYMBOL_PATH"));

		FString SymbolPathPart;
		while (SymbolPath.Split(TEXT(";"), &SymbolPathPart, &SymbolPath))
		{
			FString Result = FPaths::Combine(SymbolPathPart, FileName);
			if (PlatformFile->FileExists(*Result))
			{
				// File found in one of symbol paths
				return Result;
			}
		}

		if (!SymbolPath.IsEmpty())
		{
			FString Result = FPaths::Combine(SymbolPath, FileName);
			if (PlatformFile->FileExists(*Result))
			{
				// File found in symbol path
				return Result;
			}
		}

		// File not found
		return FString();
	}

	// use _NT_SYMBOL_PATH environment variable format
	// for more information see: https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/advanced-symsrv-use
	// to explicitly download symbol from MS Symbol Server for specific dll file, run:
	// "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symchk.exe" /r C:\Windows\system32\kernel32.dll /s srv*C:\Symbols*https://msdl.microsoft.com/download/symbols
	static FString FindWindowsSymbolFile(const FString& GuidPath)
	{
		FString SymbolPath = FPlatformMisc::GetEnvironmentVariable(TEXT("_NT_SYMBOL_PATH"));
		if (SymbolPath.IsEmpty())
		{
			return FString();
		}

		IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

		TArray<FString> Parts;
		SymbolPath.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& Part : Parts)
		{
			if (Part.StartsWith(TEXT("srv*")))
			{
				TArray<FString> Srv;
				Part.RightChop(4).ParseIntoArray(Srv, TEXT("*"));
				for (const FString& Path : Srv)
				{
					FString FilePath = FPaths::Combine(Path, GuidPath);
					if (PlatformFile->FileExists(*FilePath))
					{
						return FilePath;
					}
				}
			}
			else if (!Part.Contains(TEXT("*")))
			{
				FString FilePath = FPaths::Combine(Part, GuidPath);
				if (PlatformFile->FileExists(*FilePath))
				{
					return FilePath;
				}
			}
		;}

		return FString();
	}

	static bool LoadBinary(const TCHAR* Path, SYMS_Arena* Arena, SYMS_ParseBundle& Bundle, TArray<FAutoMappedFile>& Files)
	{
		FString FilePath = Path;

		bool bFileFound = false;
		IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

		// First lookup file in symbol path
		FString BinaryPath = FindSymbolFile(FilePath);
		if (!BinaryPath.IsEmpty())
		{
			bFileFound = true;
		}

		if (!bFileFound)
		{
			// If file is absolute path, try that
			if (!FPathViews::IsRelativePath(*FilePath) && PlatformFile->FileExists(*FilePath))
			{
				BinaryPath = FilePath;
				bFileFound = true;
			}
		}

		if (!bFileFound)
		{
			UE_LOG(LogSymslib, Warning, TEXT("Binary file '%s' not found"), *FilePath);
			return false;
		}

		FAutoMappedFile& File = Files.AddDefaulted_GetRef();
		if (!File.Load(*BinaryPath))
		{
			UE_LOG(LogSymslib, Warning, TEXT("Failed to load '%s' file"), *BinaryPath);
			return false;
		}

		SYMS_FileAccel* Accel = syms_file_accel_from_data(Arena, File.GetData());
		SYMS_BinAccel* BinAccel = syms_bin_accel_from_file(Arena, File.GetData(), Accel);
		if (!syms_accel_is_good(BinAccel))
		{
			UE_LOG(LogSymslib, Warning, TEXT("Cannot parse '%s' binary file"), *BinaryPath);
			return false;
		}

		// remember full path where binary file was found, so debug file can be looked up next to it
		File.FilePath = BinaryPath;

		Bundle.bin_data = File.GetData();
		Bundle.bin = BinAccel;

		return true;
	}

	static bool LoadDebug(SYMS_Arena* Arena, SYMS_ParseBundle& Bundle, TArray<FAutoMappedFile>& Files)
	{
		if (syms_bin_is_dbg(Bundle.bin))
		{
			// binary has debug info built-in (like dwarf file)
			Bundle.dbg = syms_dbg_accel_from_bin(Arena, Files[0].GetData(), Bundle.bin);
			Bundle.dbg_data = Bundle.bin_data;
			return true;
		}

		// we're loading extra file (pdb for exe)
		SYMS_ExtFileList List = syms_ext_file_list_from_bin(Arena, Files[0].GetData(), Bundle.bin);
		if (!List.first)
		{
			UE_LOG(LogSymslib, Warning, TEXT("Binary file '%s' built without debug info"), *Files[0].FilePath);
			return false;
		}
		SYMS_ExtFile ExtFile = List.first->ext_file;

		// debug file path from metadata in executable
		FString FilePath = ANSI_TO_TCHAR(reinterpret_cast<char*>(ExtFile.file_name.str));

		bool bFileFound = false;
		IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

		// First try file in symbol path
		FString DebugPath = FindSymbolFile(FilePath);
		if (!DebugPath.IsEmpty())
		{
			bFileFound = true;
		}

		if (!bFileFound)
		{
			// if executable is PE format, try Windows symbol path format with guid in path
			if (Bundle.bin->format == SYMS_FileFormat_PE)
			{
				SYMS_PeBinAccel* PeAccel = reinterpret_cast<SYMS_PeBinAccel*>(Bundle.bin);

				SYMS_PeGuid* Guid = &PeAccel->dbg_guid;
				SYMS_U32 Age = PeAccel->dbg_age;

				FString FileName = FPaths::GetCleanFilename(FilePath);
				FString GuidPath = FString::Printf(TEXT("%s/%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X/%s"),
					*FileName,
					Guid->data1, Guid->data2, Guid->data3,
					Guid->data4[0], Guid->data4[1], Guid->data4[2], Guid->data4[3], Guid->data4[4], Guid->data4[5], Guid->data4[6], Guid->data4[7],
					Age,
					*FileName);

				DebugPath = FindWindowsSymbolFile(GuidPath);
				if (!DebugPath.IsEmpty())
				{
					bFileFound = true;
				}
			}
		}

		if (!bFileFound)
		{
			// Try direct path if it is absolute
			if (!FPathViews::IsRelativePath(*FilePath) && PlatformFile->FileExists(*FilePath))
			{
				DebugPath = FilePath;
				bFileFound = true;
			}
		}

		if (!bFileFound)
		{
			// Try relative path to module binary file that was found in LoadBinary function
			FString ModuleBasePath = FPaths::GetPath(Files[0].FilePath);
			DebugPath = FPaths::Combine(ModuleBasePath, FPaths::GetCleanFilename(FilePath));
			if (PlatformFile->FileExists(*DebugPath))
			{
				bFileFound = true;
			}
		}

		if (!bFileFound)
		{
			UE_LOG(LogSymslib, Warning, TEXT("Debug file '%s' not found"), *FilePath);
			return false;
		}

		FAutoMappedFile& File = Files.AddDefaulted_GetRef();
		if (!File.Load(*DebugPath))
		{
			UE_LOG(LogSymslib, Warning, TEXT("Failed to load '%s' file"), *DebugPath);
			return false;
		}

		SYMS_FileAccel* Accel = syms_file_accel_from_data(Arena, File.GetData());
		SYMS_DbgAccel* DbgAccel = syms_dbg_accel_from_file(Arena, File.GetData(), Accel);
		if (!syms_accel_is_good(DbgAccel))
		{
			UE_LOG(LogSymslib, Warning, TEXT("Cannot parse '%s' debug file"), *DebugPath);
			return false;
		}

		File.FilePath = DebugPath;

		Bundle.dbg = DbgAccel;
		Bundle.dbg_data = File.GetData();

		return true;
	}

	static bool MatchImageId(const TArray<uint8>& ImageId, SYMS_ParseBundle DataParsed)
	{
		if (DataParsed.dbg->format == SYMS_FileFormat_PDB)
		{
			// for Pdbs checksum is a 16 byte guid and 4 byte unsigned integer for age, but usually age is not used for matching debug file to exe
			static_assert(sizeof(FGuid) == 16, "Expected 16 byte FGuid");
			check(ImageId.Num() == 20);
			FGuid* ModuleGuid = (FGuid*)ImageId.GetData();

			SYMS_ExtMatchKey MatchKey = syms_ext_match_key_from_dbg(DataParsed.dbg_data, DataParsed.dbg);
			FGuid* PdbGuid = (FGuid*)MatchKey.v;

			if (*ModuleGuid != *PdbGuid)
			{
				// mismatch
				return false;
			}
		}
		else if (DataParsed.bin->format == SYMS_FileFormat_ELF)
		{
			// try different ways of geting build id from elf binary
			SYMS_String8 FoundId = { 0, 0 };

			SYMS_String8 Bin = DataParsed.bin_data;
			SYMS_ElfSectionArray Sections = DataParsed.bin->elf_accel.sections;
			for (SYMS_U64 SectionIndex = 0; SectionIndex < Sections.count; SectionIndex += 1)
			{
				SYMS_U64 SectionOffset = Sections.v[SectionIndex].file_range.min;
				SYMS_U64 SectionSize = Sections.v[SectionIndex].file_range.max - SectionOffset;

				if (syms_string_match(Sections.v[SectionIndex].name, syms_str8_lit(".note.gnu.build-id"), 0))
				{
					if (SectionSize > 12)
					{
						SYMS_U32 NameSize = *(SYMS_U32*)&Bin.str[SectionOffset + 0];
						SYMS_U32 DescSize = *(SYMS_U32*)&Bin.str[SectionOffset + 4];
						SYMS_U32 Type = *(SYMS_U32*)&Bin.str[SectionOffset + 8];

						const SYMS_U32 NT_GNU_BUILD_ID = 3;
						// name must be "GNU\0", contents must be at least 16 bytes, and type must be 3
						if (NameSize == 4 && DescSize >= 16 && Type == NT_GNU_BUILD_ID)
						{
							SYMS_U64 NameOffset = sizeof(NameSize) + sizeof(DescSize) + sizeof(Type);
							SYMS_String8 NameStr = syms_str8(&Bin.str[SectionOffset + NameOffset], 4);
							if (NameSize <= SectionSize && syms_string_match(NameStr, syms_str8((SYMS_U8*)"GNU", 4), 0))
							{
								SYMS_U64 DescOffset = NameOffset + SYMS_AlignPow2(NameSize, 4);
								if (DescOffset + 16 <= SectionSize)
								{
									FoundId = syms_str8(&Bin.str[SectionOffset + DescOffset], 16);
								}
							}
						}
					}
					break;
				}
				else if (syms_hash_djb2(Sections.v[SectionIndex].name) == 0xaab84f54dfa67dee)
				{
					if (SectionSize >= 16)
					{
						FoundId = syms_str8(&Bin.str[SectionOffset], 16);
					}
					break;
				}
			}

			if (FoundId.size == 16 && FoundId.size == ImageId.Num())
			{
				if (FMemory::Memcmp(ImageId.GetData(), FoundId.str, FoundId.size) != 0)
				{
					// mismatch
					return false;
				}
			}
		}

		// either ID's are matching, or ID is not found in which case return "success" case
		return true;
	}
}

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
	UE_LOG(LogSymslib, Log, TEXT("UE_INSIGHTS_SYMBOL_PATH: '%s'"), *FPlatformMisc::GetEnvironmentVariable(TEXT("UE_INSIGHTS_SYMBOL_PATH")));
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

		// Release memory used by syms library
		{
			FReadScopeLock _(ModulesLock);
			for (uint32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
			{
				FModuleEntry& Module = Modules[ModuleIndex];
				Algo::ForEach(Module.Instance.Arenas, syms_arena_release);
				Module.Instance.Arenas.Empty();
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

	uint32 Stride = (WorkingSet.Num() - 1) / SymbolTasksInParallel + 1;
	constexpr uint32 MinStride = 4;
	Stride = FMath::Max(Stride, MinStride);
	const uint32 ActualSymbolTasksInParallel = (WorkingSet.Num() + Stride - 1) / Stride;
	TasksInFlight += ActualSymbolTasksInParallel;

	// Use background priority in order to not not interfere with Slate
	ParallelFor(ActualSymbolTasksInParallel, [this, &WorkingSet, Stride](uint32 Index) {
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
	// temporary memory used for loading
	SYMS_Group* Group = syms_group_alloc();

	// memory-mapped binary & debug files
	TArray<FAutoMappedFile> Files;

	// contents of binary & debug file
	SYMS_ParseBundle Bundle;

	if (!LoadBinary(Module->Path, Group->arena, Bundle, Files))
	{
		syms_group_release(Group);
		return EModuleStatus::Failed;
	}

	if (!LoadDebug(Group->arena, Bundle, Files))
	{
		syms_group_release(Group);
		return EModuleStatus::Failed;
	}

	// check debug data mismatch to captured module
	if (!Module->ImageId.IsEmpty() && !MatchImageId(Module->ImageId, Bundle))
	{
		syms_group_release(Group);
		UE_LOG(LogSymslib, Warning, TEXT("Symbols for '%s' does not match traced binary."), Module->Name);
		return EModuleStatus::VersionMismatch;
	}

	FSymsInstance* Instance = &Module->Instance;

	// initialize group
	syms_set_lane(0);
	syms_group_init(Group, &Bundle);

	// unit storage
	SYMS_U64 UnitCount = syms_group_unit_count(Group);
	Instance->Units.SetNum(UnitCount);

	// per-thread arena storage (at least one)
	int32 WorkerThreadCount = FMath::Max(1, FTaskGraphInterface::Get().GetNumWorkerThreads());
	Instance->Arenas.SetNum(WorkerThreadCount);

	// how many symbols are loaded
	std::atomic<uint32> SymbolCount = 0;

	// parse debug info in multiple threads
	{
		uint32 LaneSlot = FPlatformTLS::AllocTlsSlot();

		std::atomic<uint32> LaneCount = 0;
		syms_group_begin_multilane(Group, WorkerThreadCount);
		ParallelFor(UnitCount, [Instance, Group, LaneSlot, &LaneCount, &SymbolCount](uint32 Index)
		{
			SYMS_Arena* Arena;
			uint32 LaneValue = uint32(reinterpret_cast<intptr_t>(FPlatformTLS::GetTlsValue(LaneSlot)));
			if (LaneValue == 0)
			{
				// first time we are on this thread
				LaneValue = ++LaneCount;
				FPlatformTLS::SetTlsValue(LaneSlot, reinterpret_cast<void*>(intptr_t(LaneValue)));

				// syms lane index is 0-based
				uint32 LaneIndex = LaneValue - 1;
				syms_set_lane(LaneIndex);
				Arena = Instance->Arenas[LaneIndex] = syms_arena_alloc();
			}
			else
			{
				uint32 LaneIndex = LaneValue - 1;
				syms_set_lane(LaneIndex);
				Arena = Instance->Arenas[LaneIndex];
			}

			SYMS_ArenaTemp Scratch = syms_get_scratch(0, 0);

			SYMS_UnitID UnitID = static_cast<SYMS_UnitID>(Index) + 1; // syms unit id's are 1-based
			FSymsUnit* Unit = &Instance->Units[Index];

			SYMS_SpatialMap1D* ProcSpatialMap = syms_group_proc_map_from_uid(Group, UnitID);
			Unit->ProcMap = syms_spatial_map_1d_copy(Arena, ProcSpatialMap);

			SYMS_String8Array* FileTable = syms_group_file_table_from_uid_with_fallbacks(Group, UnitID);
			Unit->FileTable = syms_string_array_copy(Arena, 0, FileTable);

			SYMS_LineParseOut* LineParse = syms_group_line_parse_from_uid(Group, UnitID);
			Unit->LineTable = syms_line_table_with_indexes_from_parse(Arena, LineParse);

			SYMS_SpatialMap1D* LineSpatialMap = syms_group_line_sequence_map_from_uid(Group, UnitID);
			Unit->LineMap = syms_spatial_map_1d_copy(Arena, LineSpatialMap);

			SYMS_UnitAccel* UnitAccel = syms_group_unit_from_uid(Group, UnitID);

			SYMS_IDMap ProcIdMap = syms_id_map_alloc(Scratch.arena, 4093);

			SYMS_SymbolIDArray* ProcArray = syms_group_proc_sid_array_from_uid(Group, UnitID);
			SYMS_U64 ProcCount = ProcArray->count;

			FSymsSymbol* Symbols = syms_push_array(Arena, FSymsSymbol, ProcCount);
			for (SYMS_U64 ProcIndex = 0; ProcIndex < ProcCount; ProcIndex++)
			{
				SYMS_SymbolID SymbolID = ProcArray->ids[ProcIndex];

				SYMS_String8 Name = syms_group_symbol_name_from_sid(Arena, Group, UnitAccel, SymbolID);
				Symbols[ProcIndex].Name = reinterpret_cast<char*>(Name.str);

				syms_id_map_insert(Scratch.arena, &ProcIdMap, SymbolID, &Symbols[ProcIndex]);
			}

			SYMS_SpatialMap1D* ProcMap = &Unit->ProcMap;
			for (SYMS_SpatialMap1DRange* Range = ProcMap->ranges, *EndRange = ProcMap->ranges + ProcMap->count; Range < EndRange; Range++)
			{
				void* SymbolPtr = syms_id_map_ptr_from_u64(&ProcIdMap, Range->val);
				Range->val = SYMS_U64(reinterpret_cast<intptr_t>(SymbolPtr));
			}

			syms_release_scratch(Scratch);

			SymbolCount += ProcCount;
		});
		syms_group_end_multilane(Group);

		FPlatformTLS::FreeTlsSlot(LaneSlot);
	}

	SYMS_Arena* Arena = Instance->Arenas[0];

	// store stripped format symbols
	{
		SYMS_StrippedInfoArray StrippedInfo = syms_group_stripped_info(Group);

		FSymsSymbol* StrippedSymbols = syms_push_array(Arena, FSymsSymbol, StrippedInfo.count);
		for (SYMS_U64 Index = 0; Index < StrippedInfo.count; Index++)
		{
			SYMS_StrippedInfo* Info = &StrippedInfo.info[Index];
			FSymsSymbol* StrippedSymbol = &StrippedSymbols[Index];

			SYMS_String8 Name = syms_push_string_copy(Arena, Info->name);
			StrippedSymbol->Name = reinterpret_cast<char*>(Name.str);
		}

		Instance->StrippedMap = syms_spatial_map_1d_copy(Arena, syms_group_stripped_info_map(Group));
		Instance->StrippedSymbols = StrippedSymbols;

		SymbolCount += StrippedInfo.count;
	}

	Instance->UnitMap = syms_spatial_map_1d_copy(Arena, syms_group_unit_map(Group));
	Instance->DefaultBase = syms_group_default_vbase(Group);

	syms_group_release(Group);
	Instance->Arenas.RemoveAll([](SYMS_Arena* arena) { return arena == nullptr; });

	UE_LOG(LogSymslib, Display, TEXT("Loaded symbols for '%s' at base 0x%016llx, %u symbols."), Module->Name, Module->Base, SymbolCount.load());

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

	// Find procedure and source file for address

	FSymsInstance* Instance = &Module->Instance;
	SYMS_U64 VirtualOffset = Address + Instance->DefaultBase - Module->Base;

	FSymsSymbol* SymsSymbol = nullptr;

	constexpr uint32 MaxStringSize = 1024;
	const TCHAR* SourceFilePersistent = nullptr;
	uint32 SourceFileLine = 0;

	SYMS_UnitID UnitID = syms_spatial_map_1d_value_from_point(&Instance->UnitMap, VirtualOffset);
	if (UnitID)
	{
		FSymsUnit* Unit = &Instance->Units[UnitID - 1];

		SYMS_U64 Value = syms_spatial_map_1d_value_from_point(&Unit->ProcMap, VirtualOffset);
		if (Value)
		{
			SymsSymbol = reinterpret_cast<FSymsSymbol*>(Value);

			SYMS_U64 SeqNumber = syms_spatial_map_1d_value_from_point(&Unit->LineMap, VirtualOffset);
			if (SeqNumber)
			{
				SYMS_Line Line = syms_line_from_sequence_voff(&Unit->LineTable, SeqNumber, VirtualOffset);
				if (Line.src_coord.file_id)
				{
					SYMS_String8 FileName = Unit->FileTable.strings[Line.src_coord.file_id - 1];

					ANSICHAR SourceFile[MaxStringSize];
					FCStringAnsi::Strncpy(SourceFile, reinterpret_cast<char*>(FileName.str), MaxStringSize);
					SourceFile[MaxStringSize - 1] = 0;
					SourceFilePersistent = StringAllocator.Store(ANSI_TO_TCHAR(SourceFile));

					SourceFileLine = Line.src_coord.line;
				}
			}
		}
	}

	if (SymsSymbol == nullptr)
	{
		// try lookup into stripped format symbols
		SYMS_U64 Value = syms_spatial_map_1d_value_from_point(&Instance->StrippedMap, VirtualOffset);
		if (Value)
		{
			SymsSymbol = &Instance->StrippedSymbols[Value - 1];

			// use module name as filename
			SourceFilePersistent = StringAllocator.Store(Module->Name);
		}
	}

	// this includes skipping symbols without name (empty string)
	if (!SymsSymbol || !SourceFilePersistent || SymsSymbol && SymsSymbol->Name[0] == 0)
	{
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotFound, Module->Name, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, 0);
		return false;
	}

	ANSICHAR SymbolName[MaxStringSize];
	FCStringAnsi::Strncpy(SymbolName, SymsSymbol->Name, MaxStringSize);
	SymbolName[MaxStringSize - 1] = 0;
	const TCHAR* SymbolNamePersistent =  StringAllocator.Store(ANSI_TO_TCHAR(SymbolName));

	// Store the strings and update the target data
	UpdateResolvedSymbol(
		Target,
		ESymbolQueryResult::OK,
		Module->Name,
		SymbolNamePersistent,
		SourceFilePersistent,
		SourceFileLine
	);

	return true;
}

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
