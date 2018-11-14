// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/**
*
* A lightweight multi-threaded CSV profiler which can be used for profiling in Test/Shipping builds
*/

#pragma once

#include "CoreTypes.h"
#include "Containers/Queue.h"
#include "UObject/NameTypes.h"
#include "Templates/UniquePtr.h"

#if WITH_SERVER_CODE
  #define CSV_PROFILER (WITH_ENGINE && 1)
#else
  #define CSV_PROFILER (WITH_ENGINE && !UE_BUILD_SHIPPING)

  #if CSV_PROFILER && !ALLOW_DEBUG_FILES
	#undef CSV_PROFILER
	#define CSV_PROFILER 0
  #endif

#endif

#if CSV_PROFILER

// Helpers
#define CSV_CATEGORY_INDEX(CategoryName)						(_GCsvCategory_##CategoryName.Index)
#define CSV_CATEGORY_INDEX_GLOBAL								(0)
#define CSV_STAT_FNAME(StatName)								(_GCsvStat_##StatName.Name)

// Inline stats (no up front definition)
#define CSV_SCOPED_TIMING_STAT(Category,StatName)				FScopedCsvStat _ScopedCsvStat_ ## StatName (#StatName, CSV_CATEGORY_INDEX(Category));
#define CSV_SCOPED_TIMING_STAT_GLOBAL(StatName)					FScopedCsvStat _ScopedCsvStat_ ## StatName (#StatName, CSV_CATEGORY_INDEX_GLOBAL);
#define CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StatName)				FScopedCsvStatExclusive _ScopedCsvStatExclusive_ ## StatName (#StatName);

#define CSV_CUSTOM_STAT(Category,StatName,Value,Op)				FCsvProfiler::RecordCustomStat(#StatName, CSV_CATEGORY_INDEX(Category), Value, Op)
#define CSV_CUSTOM_STAT_GLOBAL(StatName,Value,Op) 				FCsvProfiler::RecordCustomStat(#StatName, CSV_CATEGORY_INDEX_GLOBAL, Value, Op)

// Stats declared up front
#define CSV_DEFINE_STAT(Category,StatName)						FCsvDeclaredStat _GCsvStat_##StatName((TCHAR*)TEXT(#StatName), CSV_CATEGORY_INDEX(Category));
#define CSV_DEFINE_STAT_GLOBAL(StatName)						FCsvDeclaredStat _GCsvStat_##StatName((TCHAR*)TEXT(#StatName), CSV_CATEGORY_INDEX_GLOBAL);
#define CSV_DECLARE_STAT_EXTERN(Category,StatName)				extern FCsvDeclaredStat _GCsvStat_##StatName
#define CSV_CUSTOM_STAT_DEFINED(StatName,Value,Op)				FCsvProfiler::RecordCustomStat(_GCsvStat_##StatName.Name, _GCsvStat_##StatName.CategoryIndex, Value, Op);

// Categories
#define CSV_DEFINE_CATEGORY(CategoryName,bDefaultValue)			FCsvCategory _GCsvCategory_##CategoryName(TEXT(#CategoryName),bDefaultValue)
#define CSV_DECLARE_CATEGORY_EXTERN(CategoryName)				extern FCsvCategory _GCsvCategory_##CategoryName

#define CSV_DEFINE_CATEGORY_MODULE(Module_API,CategoryName,bDefaultValue)	FCsvCategory Module_API _GCsvCategory_##CategoryName(TEXT(#CategoryName),bDefaultValue)
#define CSV_DECLARE_CATEGORY_MODULE_EXTERN(Module_API,CategoryName)			extern Module_API FCsvCategory _GCsvCategory_##CategoryName

// Events
#define CSV_EVENT(Category, Format, ...) 						FCsvProfiler::RecordEventf( CSV_CATEGORY_INDEX(Category), Format, ##__VA_ARGS__ )
#define CSV_EVENT_GLOBAL(Format, ...) 							FCsvProfiler::RecordEventf( CSV_CATEGORY_INDEX_GLOBAL, Format, ##__VA_ARGS__ )

#else
  #define CSV_CATEGORY_INDEX(CategoryName)						
  #define CSV_CATEGORY_INDEX_GLOBAL								
  #define CSV_STAT_FNAME(StatName)								
  #define CSV_SCOPED_TIMING_STAT(Category,StatName)				
  #define CSV_SCOPED_TIMING_STAT_GLOBAL(StatName)					
  #define CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StatName)
  #define CSV_CUSTOM_STAT(Category,StatName,Value,Op)				
  #define CSV_CUSTOM_STAT_GLOBAL(StatName,Value,Op) 				
  #define CSV_DEFINE_STAT(Category,StatName)						
  #define CSV_DEFINE_STAT_GLOBAL(StatName)						
  #define CSV_DECLARE_STAT_EXTERN(Category,StatName)				
  #define CSV_CUSTOM_STAT_DEFINED(StatName,Value,Op)				
  #define CSV_DEFINE_CATEGORY(CategoryName,bDefaultValue)			
  #define CSV_DECLARE_CATEGORY_EXTERN(CategoryName)				
  #define CSV_DEFINE_CATEGORY_MODULE(Module_API,CategoryName,bDefaultValue)	
  #define CSV_DECLARE_CATEGORY_MODULE_EXTERN(Module_API,CategoryName)			
  #define CSV_EVENT(Category, Format, ...) 						
  #define CSV_EVENT_GLOBAL(Format, ...) 							
#endif


#if CSV_PROFILER
class FCsvProfilerFrame;
class FCsvProfilerThreadData;
class FCsvProfilerProcessingThread;
class FName;

enum class ECsvCustomStatOp : uint8
{
	Set,
	Min,
	Max,
	Accumulate,
};

enum class ECsvCommandType : uint8
{
	Start,
	Stop,
	Count
};

struct FCsvCategory;

#define CSV_STAT_NAME_PREFIX TEXT("__CSVSTAT__")

struct FCsvDeclaredStat
{
	FCsvDeclaredStat(TCHAR* NameString, uint32 InCategoryIndex) : Name(*(FString(CSV_STAT_NAME_PREFIX) + NameString)), CategoryIndex(InCategoryIndex) {}
	FName Name;
	uint32 CategoryIndex;
};

struct FCsvCaptureCommand
{
	FCsvCaptureCommand()
		: CommandType(ECsvCommandType::Count)
		, FrameRequested(-1)
		, Value(-1)
	{}

	FCsvCaptureCommand(ECsvCommandType InCommandType, uint32 InFrameRequested, uint32 InValue = -1, const FString& InDestinationFolder = FString(), const FString& InFilename = FString(), const FString& InCustomMetadata = FString(), bool InbWriteCompletionFile = false)
		: CommandType(InCommandType)
		, FrameRequested(InFrameRequested)
		, Value(InValue)
		, DestinationFolder(InDestinationFolder)
		, Filename(InFilename)
		, CustomMetadata(InCustomMetadata)
		, bWriteCompletionFile(InbWriteCompletionFile)
	{}

	ECsvCommandType CommandType;
	uint32 FrameRequested;
	uint32 Value;
	FString DestinationFolder;
	FString Filename;
	FString CustomMetadata;
	bool bWriteCompletionFile;
};

/**
* FCsvProfiler class. This manages recording and reporting all for CSV stats
*/
class FCsvProfiler
{
	friend class FCsvProfilerProcessingThread;
	friend class FCsvProfilerThreadData;
	friend struct FCsvCategory;
private:
	static TUniquePtr<FCsvProfiler> Instance;		
public:
	FCsvProfiler();
	~FCsvProfiler();
	static CORE_API FCsvProfiler* Get();

	CORE_API void Init();

	/** Begin static interface (used by macros)*/
	/** Push/pop events */
	CORE_API static void BeginStat(const char * StatName, uint32 CategoryIndex);
	CORE_API static void EndStat(const char * StatName, uint32 CategoryIndex);

	CORE_API static void BeginExclusiveStat(const char * StatName);
	CORE_API static void EndExclusiveStat(const char * StatName);

	CORE_API static void RecordCustomStat(const char * StatName, uint32 CategoryIndex, float Value, const ECsvCustomStatOp CustomStatOp);
	CORE_API static void RecordCustomStat(const FName& StatName, uint32 CategoryIndex, float Value, const ECsvCustomStatOp CustomStatOp);
	CORE_API static void RecordCustomStat(const char * StatName, uint32 CategoryIndex, int32 Value, const ECsvCustomStatOp CustomStatOp);
	CORE_API static void RecordCustomStat(const FName& StatName, uint32 CategoryIndex, int32 Value, const ECsvCustomStatOp CustomStatOp);

	CORE_API static void RecordEvent(int32 CategoryIndex, const FString& EventText);
	CORE_API static void RecordEventAtTimestamp(int32 CategoryIndex, const FString& EventText, uint64 Cycles64);

	template <typename FmtType, typename... Types>
	FORCEINLINE static void RecordEventf(int32 CategoryIndex, const FmtType& Fmt, Types... Args)
	{
		static_assert(TIsArrayOrRefOfType<FmtType, TCHAR>::Value, "Formatting string must be a TCHAR array.");
		static_assert(TAnd<TIsValidVariadicFunctionArg<Types>...>::Value, "Invalid argument(s) passed to FCsvProfiler::RecordEventf");
		RecordEventfInternal(CategoryIndex, Fmt, Args...);
	}

	/** Singleton interface */
	CORE_API bool IsCapturing();
	CORE_API bool IsCapturing_Renderthread();

	CORE_API int32 GetCaptureFrameNumber();

	CORE_API bool EnableCategoryByString(const FString& CategoryName) const;

	/** Per-frame update */
	CORE_API void BeginFrame();
	CORE_API void EndFrame();

	/** Begin/End Capture */
	CORE_API void BeginCapture( int InNumFramesToCapture = -1,
		const FString& InDestinationFolder = FString(),
		const FString& InFilename = FString(),
		const FString& InCustomMetadata = FString(),
		bool bInWriteCompletionFile = false);

	CORE_API void EndCapture();

	/** Final cleanup */
	void Release();

	/** Renderthread begin/end frame */
	CORE_API void BeginFrameRT();
	CORE_API void EndFrameRT();

	CORE_API void SetDeviceProfileName(FString InDeviceProfileName);

private:
	CORE_API static void VARARGS RecordEventfInternal(int32 CategoryIndex, const TCHAR* Fmt, ...);

	static CORE_API int32 RegisterCategory(const FString& Name, bool bEnableByDefault, bool bIsGlobal);
	static int32 GetCategoryIndex(const FString& Name);

	void GetProfilerThreadDataArray(TArray<FCsvProfilerThreadData*>& OutProfilerThreadDataArray);
	void WriteCaptureToFile();
	float ProcessStatData();

	const TArray<uint64>& GetTimestampsForThread(uint32 ThreadId) const;

	int32 NumFramesToCapture;
	int32 CaptureFrameNumber;

	bool bInsertEndFrameAtFrameStart;
	bool bWriteCompletionFile;

	uint64 LastEndFrameTimestamp;
	uint32 CaptureEndFrameCount;

	FString OutputFilename;
	FString CustomMetadata;
	TQueue<FCsvCaptureCommand> CommandQueue;
	FCsvProfilerProcessingThread* ProcessingThread;

	FString DeviceProfileName;

	FThreadSafeCounter IsShuttingDown;
};

class FScopedCsvStat
{
public:
	FScopedCsvStat(const char * InStatName, uint32 InCategoryIndex)
		: StatName(InStatName)
		, CategoryIndex(InCategoryIndex)
	{
		FCsvProfiler::BeginStat(StatName, CategoryIndex);
	}

	~FScopedCsvStat()
	{
		FCsvProfiler::EndStat(StatName, CategoryIndex);
	}
	const char * StatName;
	uint32 CategoryIndex;
};

class FScopedCsvStatExclusive 
{
public:
	FScopedCsvStatExclusive(const char * InStatName)
		: StatName(InStatName)
	{
		FCsvProfiler::BeginExclusiveStat(StatName);
	}

	~FScopedCsvStatExclusive()
	{
		FCsvProfiler::EndExclusiveStat(StatName);
	}
	const char * StatName;
};


struct FCsvCategory
{
	FCsvCategory() : Index(-1) {}
	FCsvCategory(const TCHAR* CategoryString, bool bDefaultValue, bool bIsGlobal = false)
	{
		Name = CategoryString;
		Index = FCsvProfiler::RegisterCategory(Name, bDefaultValue, bIsGlobal);
	}

	uint32 Index;
	FString Name;
};


CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, Exclusive);


#endif //CSV_PROFILER