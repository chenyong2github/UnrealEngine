// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "Containers/UnrealString.h"

struct FProgramCounterSymbolInfo;

/** 
 * Symbol information associated with a program counter. 
 * FString version.
 * To be used by external tools.
 */
struct CORE_API FProgramCounterSymbolInfoEx
{
	/** Module name. */
	FString	ModuleName;

	/** Function name. */
	FString	FunctionName;

	/** Filename. */
	FString	Filename;

	/** Line number in file. */
	uint32	LineNumber;

	/** Symbol displacement of address.	*/
	uint64	SymbolDisplacement;

	/** Program counter offset into module. */
	uint64	OffsetInModule;

	/** Program counter. */
	uint64	ProgramCounter;

	/** Default constructor. */
	FProgramCounterSymbolInfoEx( FString InModuleName, FString InFunctionName, FString InFilename, uint32 InLineNumber, uint64 InSymbolDisplacement, uint64 InOffsetInModule, uint64 InProgramCounter );
};


/** Enumerates crash description versions. */
enum class ECrashDescVersions : int32
{
	/** Introduces a new crash description format. */
	VER_1_NewCrashFormat,

	/** Added misc properties (CPU,GPU,OS,etc), memory related stats and platform specific properties as generic payload. */
	VER_2_AddedNewProperties,

	/** Using crash context when available. */
	VER_3_CrashContext = 3,
};

/** Enumerates crash dump modes. */
enum class ECrashDumpMode : int32
{
	/** Default minidump settings. */
	Default = 0,

	/** Full memory crash minidump */
	FullDump = 1,

	/** Full memory crash minidump, even on ensures */
	FullDumpAlways = 2,
};

/** Portable stack frame */
struct FCrashStackFrame
{
	FString ModuleName;
	uint64 BaseAddress;
	uint64 Offset;

	FCrashStackFrame(FString ModuleNameIn, uint64 BaseAddressIn, uint64 OffsetIn)
		: ModuleName(MoveTemp(ModuleNameIn))
		, BaseAddress(BaseAddressIn)
		, Offset(OffsetIn)
	{
	}
};

/** Portable thread stack frame */
struct FThreadStackFrames {
	FString						ThreadName;
	uint32						ThreadId;
	TArray<FCrashStackFrame>	StackFrames;
};

enum class ECrashContextType
{
	Crash,
	Assert,
	Ensure,
	GPUCrash,
	Hang,
	OutOfMemory,
	AbnormalShutdown,

	Max
};

/** In development mode we can cause crashes in order to test reporting systems. */
enum class ECrashTrigger
{
	Debug = -1,
	Normal = 0
};

#define CR_MAX_ERROR_MESSAGE_CHARS 2048
#define CR_MAX_DIRECTORY_CHARS 256
#define CR_MAX_STACK_FRAMES 256
#define CR_MAX_THREAD_NAME_CHARS 64
#define CR_MAX_THREADS 256
#define CR_MAX_GENERIC_FIELD_CHARS 64
#define CR_MAX_COMMANDLINE_CHARS 1024
#define CR_MAX_RICHTEXT_FIELD_CHARS 512
#define CR_MAX_DYNAMIC_BUFFER_CHARS 1024*16

/**
 * Fixed size structure that holds session specific state.
 */
struct FSessionContext 
{
	bool 					bIsInternalBuild;
	bool 					bIsPerforceBuild;
	bool 					bIsSourceDistribution;
	bool 					bIsUE4Release;
	bool					bIsOOM;
	bool					bIsExitRequested;
	uint32					ProcessId;
	int32 					LanguageLCID;
	int32 					NumberOfCores;
	int32 					NumberOfCoresIncludingHyperthreads;
	int32 					SecondsSinceStart;
	int32 					CrashDumpMode;
	int32					CrashType;
	int32					OOMAllocationAlignment;
	uint64					OOMAllocationSize;
	TCHAR 					GameName[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR					EngineMode[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR					EngineModeEx[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					ExecutableName[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR					BuildConfigurationName[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					DeploymentName[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					BaseDir[CR_MAX_DIRECTORY_CHARS];
	TCHAR 					RootDir[CR_MAX_DIRECTORY_CHARS];
	TCHAR 					EpicAccountId[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					LoginIdStr[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					OsVersion[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					OsSubVersion[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					CPUVendor[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					CPUBrand[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					PrimaryGPUBrand[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					UserName[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					DefaultLocale[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					CrashGUIDRoot[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					UserActivityHint[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					GameSessionID[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					CommandLine[CR_MAX_COMMANDLINE_CHARS];
	TCHAR 					CrashReportClientRichText[CR_MAX_RICHTEXT_FIELD_CHARS];
	TCHAR 					GameStateName[CR_MAX_GENERIC_FIELD_CHARS];
	TCHAR 					CrashConfigFilePath[CR_MAX_DIRECTORY_CHARS];
	FPlatformMemoryStats	MemoryStats;
};

/** Additional user settings to be communicated to crash reporting client. */
struct FUserSettingsContext
{
	bool					bNoDialog = false;
	bool					bSendUnattendedBugReports = false;
	bool					bSendUsageData = false;
	bool					bImplicitSend = false;
	TCHAR					LogFilePath[CR_MAX_DIRECTORY_CHARS];
};

/**
 * Fixed size struct holds crash information and session specific state. It is designed
 * to shared between processes (e.g. Game and CrashReporterClient).
 */
struct FSharedCrashContext
{
	// Exception info
	TCHAR					ErrorMessage[CR_MAX_ERROR_MESSAGE_CHARS];
	uint32					ThreadIds[CR_MAX_THREADS];
	TCHAR					ThreadNames[CR_MAX_THREAD_NAME_CHARS * CR_MAX_THREADS];
	uint32					NumThreads;
	uint32					CrashingThreadId;
	uint32					NumStackFramesToIgnore;
	ECrashContextType		CrashType;

	// Additional user settings.
	FUserSettingsContext	UserSettings;

	// Platform specific crash context (must be portable)
	void*					PlatformCrashContext;
	// Directory for dumped files
	TCHAR					CrashFilesDirectory[CR_MAX_DIRECTORY_CHARS];
	// Game/Engine information not possible to catch out of process
	FSessionContext			SessionContext;
	// Count and offset into dynamic buffer to comma separated plugin list
	uint32					EnabledPluginsNum;
	uint32					EnabledPluginsOffset;
	// Count and offset into dynamic buffer to comma separated key=value data for engine information
	uint32					EngineDataNum;
	uint32					EngineDataOffset;
	// Count and offset into dynamic buffer to comma separated key=value data for  game information
	uint32					GameDataNum;
	uint32					GameDataOffset;
	// Fixed size dynamic buffer
	TCHAR					DynamicData[CR_MAX_DYNAMIC_BUFFER_CHARS];
};

/**
 *	Contains a runtime crash's properties that are common for all platforms.
 *	This may change in the future.
 */
struct CORE_API FGenericCrashContext
{
public:

	static const ANSICHAR* const CrashContextRuntimeXMLNameA;
	static const TCHAR* const CrashContextRuntimeXMLNameW;

	static const ANSICHAR* const CrashConfigFileNameA;
	static const TCHAR* const CrashConfigFileNameW;
	static const TCHAR* const CrashConfigExtension;
	static const TCHAR* const ConfigSectionName;
	static const TCHAR* const CrashConfigPurgeDays;
	static const TCHAR* const CrashGUIDRootPrefix;

	static const TCHAR* const CrashContextExtension;
	static const TCHAR* const RuntimePropertiesTag;
	static const TCHAR* const PlatformPropertiesTag;
	static const TCHAR* const EngineDataTag;
	static const TCHAR* const GameDataTag;
	static const TCHAR* const EnabledPluginsTag;
	static const TCHAR* const UE4MinidumpName;
	static const TCHAR* const NewLineTag;
	static const int32 CrashGUIDLength = 128;

	static const TCHAR* const CrashTypeCrash;
	static const TCHAR* const CrashTypeAssert;
	static const TCHAR* const CrashTypeEnsure;
	static const TCHAR* const CrashTypeGPU;
	static const TCHAR* const CrashTypeHang;
	static const TCHAR* const CrashTypeAbnormalShutdown;

	static const TCHAR* const EngineModeExUnknown;
	static const TCHAR* const EngineModeExDirty;
	static const TCHAR* const EngineModeExVanilla;

	// A guid that identifies this particular execution. Allows multiple crash reports from the same run of the project to be tied together
	static const FGuid ExecutionGuid;

	/** Initializes crash context related platform specific data that can be impossible to obtain after a crash. */
	static void Initialize();

	/** Initialized crash context, using a crash context (e.g. shared from another process). */
	static void InitializeFromContext(const FSessionContext& Context, const TCHAR* EnabledPlugins, const TCHAR* EngineData, const TCHAR* GameData);

	/**
	 * @return true, if the generic crash context has been initialized.
	 */
	static bool IsInitalized()
	{
		return bIsInitialized;
	}

	/**
	 * @return true if crash reporting is being handled out-of-process.
	 */
	static bool IsOutOfProcessCrashReporter()
	{
		return bIsOutOfProcess;
	}

	/** Set whether or not the out-of-process crash reporter is running. */
	static void SetIsOutOfProcessCrashReporter(bool bInValue)
	{
		bIsOutOfProcess = bInValue;
	}

	/** Default constructor. Optionally pass a process handle if building a crash context for a process other then current. */
	FGenericCrashContext(ECrashContextType InType, const TCHAR* ErrorMessage);

	virtual ~FGenericCrashContext() { }

	/** Get the file path to the temporary session context file that we create for the given process. */
	static FString GetTempSessionContextFilePath(uint64 ProcessID);

	/** Serializes all data to the buffer. */
	void SerializeContentToBuffer() const;

	/**
	 * @return the buffer containing serialized data.
	 */
	const FString& GetBuffer() const
	{
		return CommonBuffer;
	}

	/**
	 * @return a globally unique crash name.
	 */
	void GetUniqueCrashName(TCHAR* GUIDBuffer, int32 BufferSize) const;

	/**
	 * @return whether this crash is a full memory minidump
	 */
	const bool IsFullCrashDump() const;

	/** Serializes crash's informations to the specified filename. Should be overridden for platforms where using FFileHelper is not safe, all POSIX platforms. */
	virtual void SerializeAsXML( const TCHAR* Filename ) const;

	/** 
	 * Serializes session context to the given buffer. 
	 * NOTE: Assumes that the buffer already has a header and section open.
	 */
	static void SerializeSessionContext(FString& Buffer);
	
	template <typename Type>
	void AddCrashProperty(const TCHAR* PropertyName, const Type& Value) const
	{
		AddCrashPropertyInternal(CommonBuffer, PropertyName, Value);
	}

	/** Escapes and appends specified text to XML string */
	static void AppendEscapedXMLString( FString& OutBuffer, const TCHAR* Text );

	/** Unescapes a specified XML string, naive implementation. */
	static FString UnescapeXMLString( const FString& Text );

	/** Helper to get the standard string for the crash type based on crash event bool values. */
	static const TCHAR* GetCrashTypeString(ECrashContextType Type);

	/** Get the Game Name of the crash */
	static FString GetCrashGameName();

	/** Helper to get the crash report client config filepath saved by this instance and copied to each crash report folder. */
	static const TCHAR* GetCrashConfigFilePath();

	/** Helper to get the crash report client config folder used by GetCrashConfigFilePath(). */
	static const TCHAR* GetCrashConfigFolder();

	/** Helper to clean out old files in the crash report client config folder. */
	static void PurgeOldCrashConfig();

	/** Clears the engine data dictionary */
	static void ResetEngineData();

	/** Updates (or adds if not already present) arbitrary engine data to the crash context (will remove the key if passed an empty string) */
	static void SetEngineData(const FString& Key, const FString& Value);

	/** Clears the game data dictionary */
	static void ResetGameData();

	/** Updates (or adds if not already present) arbitrary game data to the crash context (will remove the key if passed an empty string) */
	static void SetGameData(const FString& Key, const FString& Value);

	/** Adds a plugin descriptor string to the enabled plugins list in the crash context */
	static void AddPlugin(const FString& PluginDesc);

	/** Flushes the logs. In the case of in memory logs is used on this configuration, dumps them to file. */
	static void DumpLog(const FString& CrashFolderAbsolute);

	/** Initializes a shared crash context from current state. Will not set all fields in Dst. */
	static void CopySharedCrashContext(FSharedCrashContext& Dst);

	/** We can't gather memory stats in crash handling function, so we gather them just before raising
	  * exception and use in crash reporting. 
	  */
	static void SetMemoryStats(const FPlatformMemoryStats& MemoryStats);

	/** Attempts to create the output report directory. */
	static bool CreateCrashReportDirectory(const TCHAR* CrashGUIDRoot, int32 CrashIndex, FString& OutCrashDirectoryAbsolute);

	/** Sets the process id to that has crashed. On supported platforms this will analyze the given process rather than current. Default is current process. */
	void SetCrashedProcess(const FProcHandle& Process) { ProcessHandle = Process; }

	/** Stores crashing thread id. */
	void SetCrashedThreadId(uint32 InId) { CrashedThreadId = InId; }

	/** Sets the number of stack frames to ignore when symbolicating from a minidump */
	void SetNumMinidumpFramesToIgnore(int32 InNumMinidumpFramesToIgnore);

	/** Generate raw call stack for crash report (image base + offset) */
	void CapturePortableCallStack(int32 NumStackFramesToIgnore, void* Context);
	
	/** Sets the portable callstack to a specified stack */
	virtual void SetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames);

	/** Gets the portable callstack to a specified stack and puts it into OutCallStack */
	virtual void GetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames, TArray<FCrashStackFrame>& OutCallStack) const;

	/** Adds a portable callstack for a thread */
	virtual void AddPortableThreadCallStack(uint32 ThreadId, const TCHAR* ThreadName, const uint64* StackFrames, int32 NumStackFrames);

	/** Allows platform implementations to copy files to report directory. */
	virtual void CopyPlatformSpecificFiles(const TCHAR* OutputDirectory, void* Context);

	/**
	 * @return whether this crash is a non-crash event
	 */
	ECrashContextType GetType() const { return Type; }

	/**
	 * Set the current deployment name (ie. EpicApp)
	 */
	static void SetDeploymentName(const FString& EpicApp);

	/**
	 * Sets the type of crash triggered. Used to distinguish crashes caused for debugging purposes.
	 */
	static void SetCrashTrigger(ECrashTrigger Type);

protected:
	/**
	 * @OutStr - a stream of Thread XML elements containing info (e.g. callstack) specific to an active thread
	 * @return - whether the operation was successful
	 */
	virtual bool GetPlatformAllThreadContextsString(FString& OutStr) const { return false; }

	FProcHandle ProcessHandle;
	ECrashContextType Type;
	uint32 CrashedThreadId;
	const TCHAR* ErrorMessage;
	int NumMinidumpFramesToIgnore;
	TArray<FCrashStackFrame> CallStack;
	TArray<FThreadStackFrames> ThreadCallStacks;

private:

	/** Serializes the session context section of the crash context to a temporary file. */
	static void SerializeTempCrashContextToFile();

	/** Serializes the current user setting struct to a buffer. */
	static void SerializeUserSettings(FString& Buffer);

	/** Writes a common property to the buffer. */
	static void AddCrashPropertyInternal(FString& Buffer, const TCHAR* PropertyName, const TCHAR* PropertyValue);

	/** Writes a common property to the buffer. */
	template <typename Type>
	static void AddCrashPropertyInternal(FString& Buffer, const TCHAR* PropertyName, const Type& Value)
	{
		AddCrashPropertyInternal(Buffer, PropertyName, *TTypeToString<Type>::ToString(Value));
	}

	/** Serializes platform specific properties to the buffer. */
	virtual void AddPlatformSpecificProperties() const;

	/** Add callstack information to the crash report xml */
	void AddPortableCallStack() const;

	/** Produces a hash based on the offsets of the portable callstack and adds it to the xml */
	void AddPortableCallStackHash() const;

	/** Writes header information to the buffer. */
	static void AddHeader(FString& Buffer);

	/** Writes footer to the buffer. */
	static void AddFooter(FString& Buffer);

	static void BeginSection(FString& Buffer, const TCHAR* SectionName);
	static void EndSection(FString& Buffer, const TCHAR* SectionName);

	/** Called once when GConfig is initialized. Opportunity to cache values from config. */
	static void InitializeFromConfig();

	/** Called to update any localized strings in the crash context */
	static void UpdateLocalizedStrings();

	/**	Whether the Initialize() has been called */
	static bool bIsInitialized;

	/** Whether or not crash reporting is being handled out-of-process. */
	static bool bIsOutOfProcess;

	/**	Static counter records how many crash contexts have been constructed */
	static int32 StaticCrashContextIndex;

	/** The buffer used to store the crash's properties. */
	mutable FString CommonBuffer;

	/**	Records which crash context we were using the StaticCrashContextIndex counter */
	int32 CrashContextIndex;

	// FNoncopyable
	FGenericCrashContext( const FGenericCrashContext& ) = delete;
	FGenericCrashContext& operator=(const FGenericCrashContext&) = delete;
};

struct CORE_API FGenericMemoryWarningContext
{};

namespace RecoveryService
{
	/** Generates a name for the disaster recovery service embedded in the CrashReporterClient. */
	CORE_API FString GetRecoveryServerName();
}
