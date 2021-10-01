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

		inline void* GetData() const
		{
			return Region.IsValid() ? (void*)Region->GetMappedPtr() : nullptr;
		}

		inline uint64 Num() const
		{
			return Region.IsValid() ? Region->GetMappedSize() : 0;
		}
	};

	static FString FindSymbolFile(const FString& Path)
	{
		IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

		// Extract only filename part in case Path is absolute path
		FString FileName = FPaths::GetCleanFilename(Path);

		FString SymbolPath = FPlatformMisc::GetEnvironmentVariable(L"UE_INSIGHTS_SYMBOL_PATH");

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

		FString Result = FPaths::Combine(SymbolPath, FileName);
		if (PlatformFile->FileExists(*Result))
		{
			// File found in symbol path
			return Result;
		}

		if (PlatformFile->FileExists(*Path))
		{
			// File found by absolute path
			return Path;
		}

		// File not found
		return FString();
	}

	static SYMS_String8 SymsLoader(void* User, SYMS_Arena* Arena, SYMS_String8 FileName)
	{
		IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

		FString FilePath = ANSI_TO_TCHAR(reinterpret_cast<char*>(FileName.str));

		TArray<FAutoMappedFile>* Files = static_cast<TArray<FAutoMappedFile>*>(User);
		FAutoMappedFile& File = Files->AddDefaulted_GetRef();

		if (Files->Num() == 1)
		{
			// we're loading main executable file
			FString SymbolFilePath = FindSymbolFile(FilePath);
			if (SymbolFilePath.IsEmpty())
			{
				UE_LOG(LogSymslib, Warning, TEXT("File '%s' not found"), *FilePath);
				return syms_str8(nullptr, 0);
			}

			if (!File.Load(*SymbolFilePath))
			{
				UE_LOG(LogSymslib, Warning, TEXT("Failed to open '%s'"), *SymbolFilePath);
			}

			File.FilePath = SymbolFilePath;
		}
		else
		{
			// we're loading secondary file, like .pdb for .exe

			// main .exe file folder, so we can lookup relative paths
			FString ModuleBasePath = FPaths::GetPath((*Files)[0].FilePath);

			bool bDebugFilePathFound = false;

			// First try path itself if it is absolute
			FString DebugFilePath = FilePath;
			if (!FPathViews::IsRelativePath(*DebugFilePath))
			{
				if (PlatformFile->FileExists(*DebugFilePath))
				{
					bDebugFilePathFound = true;
				}
			}

			// Otherwise try relative path to module file
			if (!bDebugFilePathFound)
			{
				DebugFilePath = FPaths::Combine(ModuleBasePath, FPaths::GetCleanFilename(DebugFilePath));
				if (PlatformFile->FileExists(*DebugFilePath))
				{
					bDebugFilePathFound = true;
				}
			}

			// Use path if file was found
			if (bDebugFilePathFound)
			{
				if (!File.Load(*DebugFilePath))
				{
					UE_LOG(LogSymslib, Warning, TEXT("Failed to open '%s'"), *DebugFilePath);
				}

				File.FilePath = DebugFilePath;
			}
		}

		return syms_str8(reinterpret_cast<SYMS_U8*>(File.GetData()), File.Num());
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
	IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

	// temporary memory used for loading
	SYMS_Group* Group = syms_group_alloc();

	SYMS_FileInfOptions opts = { 0 };
	opts.disable_fallback = 1;

	TArray<FAutoMappedFile> Files;
	SYMS_FileLoadCtx Context = { &SymsLoader, &Files };
	SYMS_FileInfResult FileInf = syms_group_infer_from_file(Group->arena, Context, syms_str8_cstring(TCHAR_TO_ANSI(Module->Path)), &opts);

	SYMS_DbgAccel* Dbg = FileInf.data_parsed.dbg;
	if (Dbg == nullptr || Dbg->format == SYMS_FileFormat_Null)
	{
		syms_group_release(Group);
		UE_LOG(LogSymslib, Display, TEXT("No debug information loaded for '%s'"), Module->Name);
		return EModuleStatus::Failed;
	}

	// check debug data mismatch to captured module
	if (!Module->ImageId.IsEmpty() && Dbg->format == SYMS_FileFormat_PDB)
	{
		// for Pdbs checksum is a 16 byte guid and 4 byte unsigned integer for age, but usually age is not used for matching debug file to exe
		static_assert(sizeof(FGuid) == 16, "Expected 16 byte FGuid");
		check(Module->ImageId.Num() == 20);
		FGuid* ModuleGuid = (FGuid*)Module->ImageId.GetData();

		SYMS_ExtMatchKey MatchKey = syms_ext_match_key_from_dbg(FileInf.data_parsed.dbg_data, Dbg);
		FGuid* PdbGuid = (FGuid*)MatchKey.v;

		if (*ModuleGuid != *PdbGuid)
		{
			syms_group_release(Group);
			UE_LOG(LogSymslib, Warning, TEXT("Symbols for '%s' does not match traced binary."), Module->Name);
			return EModuleStatus::VersionMismatch;
		}
	}

	FSymsInstance* Instance = &Module->Instance;

	// initialize group
	SYMS_GroupInitParams GroupInit = {};
	GroupInit.dbg_data = FileInf.data_parsed.dbg_data;
	GroupInit.dbg = FileInf.data_parsed.dbg;

	syms_set_lane(0);
	syms_group_init(Group, &GroupInit);

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

			SYMS_UnitID UnitID = static_cast<SYMS_UnitID>(Index + 1); // syms unit id's are 1-based
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

			uint32 UnitSymbolCount = 0;

			FSymsSymbol* Symbols = syms_push_array(Arena, FSymsSymbol, ProcCount);
			for (SYMS_U64 ProcIndex = 0; ProcIndex < ProcCount; ProcIndex++)
			{
				SYMS_SymbolID SymbolID = ProcArray->ids[ProcIndex];

				SYMS_SymbolKind Kind = syms_group_symbol_kind_from_sid(Group, UnitAccel, SymbolID);
				if (Kind == SYMS_SymbolKind_Procedure)
				{
					UnitSymbolCount++;
				}

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

			SymbolCount += UnitSymbolCount;
		});
		syms_group_end_multilane(Group);

		FPlatformTLS::FreeTlsSlot(LaneSlot);
	}

	Instance->UnitMap = syms_spatial_map_1d_copy(Instance->Arenas[0], syms_group_unit_map(Group));
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
	bool bFileValid = false;
	SYMS_String8 FileName = {};
	uint32 FileLine = 0;

	SYMS_UnitID UnitID = syms_spatial_map_1d_value_from_point(&Instance->UnitMap, VirtualOffset);
	if (UnitID > 0 && UnitID <= Instance->Units.Num())
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
				if (Line.src_coord.file_id > 0 && Line.src_coord.file_id <= Unit->FileTable.count)
				{
					FileName = Unit->FileTable.strings[Line.src_coord.file_id - 1];
					FileLine = Line.src_coord.line;
					bFileValid = true;
				}
			}
		}
	}

	// this includes skipping symbols without name (empty string)
	if (!SymsSymbol || !bFileValid || SymsSymbol && SymsSymbol->Name[0] == 0)
	{
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotFound, Module->Name, GUnknownModuleTextSymsLib, GUnknownModuleTextSymsLib, 0);
		return false;
	}

	constexpr uint32 MaxStringSize = 1024;

	ANSICHAR SymbolName[MaxStringSize];
	FCStringAnsi::Strncpy(SymbolName, SymsSymbol->Name, MaxStringSize);
	SymbolName[MaxStringSize - 1] = 0;

	ANSICHAR SourceFile[MaxStringSize];
	FCStringAnsi::Strncpy(SourceFile, reinterpret_cast<char*>(FileName.str), MaxStringSize);
	SourceFile[MaxStringSize - 1] = 0;

	const TCHAR* SymbolNamePersistent =  StringAllocator.Store(ANSI_TO_TCHAR(SymbolName));
	const TCHAR* SourceFilePersistent = StringAllocator.Store(ANSI_TO_TCHAR(SourceFile));

	// Store the strings and update the target data
	UpdateResolvedSymbol(
		Target,
		ESymbolQueryResult::OK,
		Module->Name,
		SymbolNamePersistent,
		SourceFilePersistent,
		FileLine
	);

	return true;
}

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
