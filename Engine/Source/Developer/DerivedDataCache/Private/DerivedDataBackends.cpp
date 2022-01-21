// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataBackendThrottleWrapper.h"
#include "DerivedDataBackendVerifyWrapper.h"
#include "DerivedDataCacheUsageStats.h"
#include "FileBackedDerivedDataBackend.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/ThreadSafeCounter.h"
#include "HierarchicalDerivedDataBackend.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Math/UnitConversion.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineBuildSettings.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Modules/ModuleManager.h"
#include "PakFileDerivedDataBackend.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/CompactBinaryPackage.h"
#include <atomic>

DEFINE_LOG_CATEGORY(LogDerivedDataCache);

#define MAX_BACKEND_KEY_LENGTH (120)
#define LOCTEXT_NAMESPACE "DerivedDataBackendGraph"

static TAutoConsoleVariable<FString> GDumpDDCName(
	TEXT("DDC.Graph"), TEXT(""),
	TEXT("Name of custom DDC Graph to use."),
	ECVF_Default);

namespace UE::DerivedData::CacheStore::AsyncPut
{
FDerivedDataBackendInterface* CreateAsyncPutDerivedDataBackend(FDerivedDataBackendInterface* InnerBackend, bool bCacheInFlightPuts);
} // UE::DerivedData::CacheStore::AsyncPut

namespace UE::DerivedData::CacheStore::FileSystem
{
FDerivedDataBackendInterface* CreateFileSystemDerivedDataBackend(const TCHAR* CacheDirectory, const TCHAR* Params, const TCHAR* AccessLogFileName);
} // UE::DerivedData::CacheStore::FileSystem

namespace UE::DerivedData::CacheStore::Http
{
FDerivedDataBackendInterface* CreateHttpDerivedDataBackend(
	const TCHAR* NodeName,
	const TCHAR* ServiceUrl,
	const TCHAR* Namespace,
	const TCHAR* StructuredNamespace,
	const TCHAR* OAuthProvider,
	const TCHAR* OAuthClientId,
	const TCHAR* OAuthData, 
	const FDerivedDataBackendInterface::ESpeedClass* ForceSpeedClass,
	bool bReadOnly);
} // UE::DerivedData::CacheStore::Http

namespace UE::DerivedData::CacheStore::Memory
{
FFileBackedDerivedDataBackend* CreateMemoryDerivedDataBackend(const TCHAR* Name, int64 MaxCacheSize, bool bCanBeDisabled);
} // UE::DerivedData::CacheStore::Memory

namespace UE::DerivedData::CacheStore::PakFile
{
IPakFileDerivedDataBackend* CreatePakFileDerivedDataBackend(const TCHAR* Filename, bool bWriting, bool bCompressed);
} // UE::DerivedData::CacheStore::PakFile

namespace UE::DerivedData::CacheStore::S3
{
FDerivedDataBackendInterface* CreateS3DerivedDataBackend(const TCHAR* RootManifestPath, const TCHAR* BaseUrl, const TCHAR* Region, const TCHAR* CanaryObjectKey, const TCHAR* CachePath);
} // UE::DerivedData::CacheStore::S3

namespace UE::DerivedData::CacheStore::ZenCache
{
FDerivedDataBackendInterface* CreateZenDerivedDataBackend(const TCHAR* NodeName, const TCHAR* ServiceUrl, const TCHAR* Namespace);
} // UE::DerivedData::CacheStore::ZenCache

namespace UE::DerivedData::CacheStore::Graph
{

/**
  * This class is used to create a singleton that represents the derived data cache hierarchy and all of the wrappers necessary
  * ideally this would be data driven and the backends would be plugins...
**/
class FDerivedDataBackendGraph : public FDerivedDataBackend
{
public:
	/**
	* constructor, builds the cache tree
	**/
	FDerivedDataBackendGraph()
		: RootCache(NULL)
		, BootCache(NULL)
		, WritePakCache(NULL)
		, AsyncPutWrapper(NULL)
		, HierarchicalWrapper(NULL)
		, bUsingSharedDDC(false)
		, bIsShuttingDown(false)
		, MountPakCommand(
		TEXT( "DDC.MountPak" ),
		*LOCTEXT("CommandText_DDCMountPak", "Mounts read-only pak file").ToString(),
		FConsoleCommandWithArgsDelegate::CreateRaw( this, &FDerivedDataBackendGraph::MountPakCommandHandler ) )
		, UnountPakCommand(
		TEXT( "DDC.UnmountPak" ),
		*LOCTEXT("CommandText_DDCUnmountPak", "Unmounts read-only pak file").ToString(),
		FConsoleCommandWithArgsDelegate::CreateRaw( this, &FDerivedDataBackendGraph::UnmountPakCommandHandler ) )
	{
		check(IsInGameThread()); // we pretty much need this to be initialized from the main thread...it uses GConfig, etc
		check(GConfig && GConfig->IsReadyForUse());
		RootCache = NULL;
		TMap<FString, FDerivedDataBackendInterface*> ParsedNodes;		

		// Create the graph using ini settings. The string "default" forwards creation to use the default graph.

		FString CustomDDCName;
		FParse::Value(FCommandLine::Get(), TEXT("-DDC="), CustomDDCName);
		if (CustomDDCName.IsEmpty() || FCString::Stricmp(*GraphName, TEXT("default")) == 0)
		{
			CustomDDCName = GDumpDDCName.GetValueOnGameThread();
		}

		if (!CustomDDCName.IsEmpty())
		{
			GraphName = CustomDDCName;
			if (GraphName == TEXT("None"))
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("DDC backend graph of 'None' specified, graph will be invalid for use."));
				return;
			}

			RootCache = ParseNode(TEXT("Root"), GEngineIni, *GraphName, ParsedNodes);

			if( RootCache == NULL )
			{
				// Remove references to any backend instances that might have been created
				ParsedNodes.Empty();
				DestroyCreatedBackends();
				UE_LOG( LogDerivedDataCache, Warning, TEXT("FDerivedDataBackendGraph: Unable to create backend graph using the specified graph settings (\"%s\"). Reverting to the default graph."), *GraphName );
			}
		}

		if( !RootCache )
		{
			// Use default graph
			GraphName = FApp::IsEngineInstalled() ? TEXT("InstalledDerivedDataBackendGraph") : TEXT("DerivedDataBackendGraph");
			FString Entry;
			if( !GConfig->GetString( *GraphName, TEXT("Root"), Entry, GEngineIni ) || !Entry.Len())
			{
				UE_LOG( LogDerivedDataCache, Fatal, TEXT("Unable to create backend graph using the default graph settings (%s) ini=%s."), *GraphName, *GEngineIni );
			}
			RootCache = ParseNode( TEXT("Root"), GEngineIni, *GraphName, ParsedNodes );
			if (!RootCache)
			{
				UE_LOG(LogDerivedDataCache, Fatal, TEXT("Unable to create backend graph using the default graph settings (%s) ini=%s. ")
					TEXT("At least one backend in the graph must be available."), *GraphName, *GEngineIni);
			}
		}

		// Make sure AsyncPutWrapper is created
		if( !AsyncPutWrapper )
		{
			AsyncPutWrapper = AsyncPut::CreateAsyncPutDerivedDataBackend( RootCache, /*bCacheInFlightPuts*/ true );
			check(AsyncPutWrapper);
			CreatedBackends.Add( AsyncPutWrapper );
			RootCache = AsyncPutWrapper;
		}

		if (MaxKeyLength == 0)
		{
			MaxKeyLength = MAX_BACKEND_KEY_LENGTH;
		}
	}

	/**
	 * Helper function to get the value of parsed bool as the return value
	 **/
	bool GetParsedBool( const TCHAR* Stream, const TCHAR* Match ) const
	{
		bool bValue = 0;
		FParse::Bool( Stream, Match, bValue );
		return bValue;
	}

	/**
	 * Parses backend graph node from ini settings
	 *
	 * @param NodeName Name of the node to parse
	 * @param IniFilename Ini filename
	 * @param IniSection Section in the ini file containing the graph definition
	 * @param InParsedNodes Map of parsed nodes and their names to be able to find already parsed nodes
	 * @return Derived data backend interface instance created from ini settings
	 */
	FDerivedDataBackendInterface* ParseNode( const TCHAR* NodeName, const FString& IniFilename, const TCHAR* IniSection, TMap<FString, FDerivedDataBackendInterface*>& InParsedNodes  )
	{
		FDerivedDataBackendInterface* ParsedNode = NULL;
		FString Entry;
		if( GConfig->GetString( IniSection, NodeName, Entry, IniFilename ) )
		{
			// Trim whitespace at the beginning.
			Entry.TrimStartInline();
			// Remove brackets.
			Entry.RemoveFromStart(TEXT("("));
			Entry.RemoveFromEnd(TEXT(")"));

			FString	NodeType;
			if( FParse::Value( *Entry, TEXT("Type="), NodeType ) )
			{
				if( NodeType == TEXT("FileSystem") )
				{
					ParsedNode = ParseDataCache( NodeName, *Entry );
				}
				else if( NodeType == TEXT("Boot") )
				{
					if( BootCache == NULL )
					{
						BootCache = ParseBootCache( NodeName, *Entry, BootCacheFilename );
						ParsedNode = BootCache;
					}
					else
					{
						UE_LOG( LogDerivedDataCache, Warning, TEXT("FDerivedDataBackendGraph:  Unable to create %s Boot cache because only one Boot or Sparse cache node is supported."), NodeName );
					}
				}
				else if( NodeType == TEXT("Memory") )
				{
					ParsedNode = ParseMemoryCache( NodeName, *Entry );
				}
				else if( NodeType == TEXT("Hierarchical") )
				{
					ParsedNode = ParseHierarchicalCache( NodeName, *Entry, IniFilename, IniSection, InParsedNodes );
				}
				else if( NodeType == TEXT("KeyLength") )
				{
					if( MaxKeyLength == 0 )
					{
						ParsedNode = ParseKeyLength( NodeName, *Entry, IniFilename, IniSection, InParsedNodes );
					}
					else
					{
						UE_LOG( LogDerivedDataCache, Warning, TEXT("FDerivedDataBackendGraph:  Unable to create %s KeyLength because only one KeyLength node is supported."), NodeName );
					}
				}
				else if( NodeType == TEXT("AsyncPut") )
				{
					if( AsyncPutWrapper == NULL )
					{
						AsyncPutWrapper = ParseAsyncPut( NodeName, *Entry, IniFilename, IniSection, InParsedNodes );
						ParsedNode = AsyncPutWrapper;
					}
					else
					{
						UE_LOG( LogDerivedDataCache, Warning, TEXT("FDerivedDataBackendGraph:  Unable to create %s AsyncPutWrapper because only one AsyncPutWrapper node is supported."), NodeName );
					}
				}
				else if( NodeType == TEXT("Verify") )
				{
					ParsedNode = ParseVerify( NodeName, *Entry, IniFilename, IniSection, InParsedNodes );
				}
				else if( NodeType == TEXT("ReadPak") )
				{
					ParsedNode = ParsePak( NodeName, *Entry, false );
				}
				else if( NodeType == TEXT("WritePak") )
				{
					ParsedNode = ParsePak( NodeName, *Entry, true );
				}
				else if (NodeType == TEXT("S3"))
				{
					ParsedNode = ParseS3Cache(NodeName, *Entry);
				}
				else if (NodeType == TEXT("Http"))
				{
					ParsedNode = ParseHttpCache(NodeName, *Entry, IniFilename, IniSection);
				}
				else if (NodeType == TEXT("Zen"))
				{
					ParsedNode = ParseZenCache(NodeName, *Entry);
				}
				
				if (ParsedNode)
				{
					// Add a throttling layer if parameters are found
					uint32 LatencyMS = 0;
					FParse::Value(*Entry, TEXT("LatencyMS="), LatencyMS);

					uint32 MaxBytesPerSecond = 0;
					FParse::Value(*Entry, TEXT("MaxBytesPerSecond="), MaxBytesPerSecond);

					if (LatencyMS != 0 || MaxBytesPerSecond != 0)
					{
						ParsedNode = new Throttle::FDerivedDataBackendThrottleWrapper(ParsedNode, LatencyMS, MaxBytesPerSecond, *Entry);
					}
				}
			}
		}

		if( ParsedNode )
		{
			// Store this node so that we don't require any order of adding backend nodes
			InParsedNodes.Add( NodeName, ParsedNode );
			// Keep references to all created nodes.
			CreatedBackends.AddUnique( ParsedNode );

			// parse any debug options for this backend. E.g. -ddc-<name>-missrate
			FDerivedDataBackendInterface::FBackendDebugOptions DebugOptions;

			if (FDerivedDataBackendInterface::FBackendDebugOptions::ParseFromTokens(DebugOptions, NodeName, FCommandLine::Get()))
			{
				if (!ParsedNode->ApplyDebugOptions(DebugOptions))
				{
					UE_LOG(LogDerivedDataCache, Warning, TEXT("Node %s is ignoring one or mode -ddc-<nodename>-opt debug options"), NodeName);
				}
			}
		}

		return ParsedNode;
	}

	/**
	 * Creates Read/write Pak file interface from ini settings
	 *
	 * @param NodeName Node name
	 * @param Entry Node definition
	 * @param bWriting true to create pak interface for writing
	 * @return Pak file data backend interface instance or NULL if unsuccessful
	 */
	FDerivedDataBackendInterface* ParsePak( const TCHAR* NodeName, const TCHAR* Entry, const bool bWriting )
	{
		FDerivedDataBackendInterface* PakNode = NULL;
		FString PakFilename;
		FParse::Value( Entry, TEXT("Filename="), PakFilename );
		bool bCompressed = GetParsedBool(Entry, TEXT("Compressed="));

		if( !PakFilename.Len() )
		{
			UE_LOG( LogDerivedDataCache, Log, TEXT("FDerivedDataBackendGraph:  %s pak cache Filename not found in *engine.ini, will not use a pak cache."), NodeName );
		}
		else
		{
			// now add the pak read cache (if any) to the front of the cache hierarchy
			if ( bWriting )
			{
				FGuid Temp = FGuid::NewGuid();
				ReadPakFilename = PakFilename;
				WritePakFilename = PakFilename + TEXT(".") + Temp.ToString();
				WritePakCache = PakFile::CreatePakFileDerivedDataBackend(*WritePakFilename, /*bWriting*/ true, bCompressed);
				PakNode = WritePakCache;
			}
			else
			{
				bool bReadPak = FPlatformFileManager::Get().GetPlatformFile().FileExists( *PakFilename );
				if( bReadPak )
				{
					PakFile::IPakFileDerivedDataBackend* ReadPak = PakFile::CreatePakFileDerivedDataBackend(*PakFilename, /*bWriting*/ false, bCompressed);
					ReadPakFilename = PakFilename;
					PakNode = ReadPak;
					ReadPakCache.Add(ReadPak);
				}
				else
				{
					UE_LOG( LogDerivedDataCache, Log, TEXT("FDerivedDataBackendGraph:  %s pak cache file %s not found, will not use a pak cache."), NodeName, *PakFilename );
				}
			}
		}
		return PakNode;
	}

	/**
	 * Creates Verify wrapper interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @param IniFilename ini filename.
	 * @param IniSection ini section containing graph definition
	 * @param InParsedNodes map of nodes and their names which have already been parsed
	 * @return Verify wrapper backend interface instance or NULL if unsuccessful
	 */
	FDerivedDataBackendInterface* ParseVerify( const TCHAR* NodeName, const TCHAR* Entry, const FString& IniFilename, const TCHAR* IniSection, TMap<FString, FDerivedDataBackendInterface*>& InParsedNodes )
	{
		FDerivedDataBackendInterface* InnerNode = NULL;
		FString InnerName;
		if( FParse::Value( Entry, TEXT("Inner="), InnerName ) )
		{
			FDerivedDataBackendInterface** ParsedInnerNode = InParsedNodes.Find( InnerName );
			if( ParsedInnerNode )
			{
				UE_LOG( LogDerivedDataCache, Warning, TEXT("Inner node %s for Verify node %s already exists. Nodes can only be used once."), *InnerName, NodeName );
				return NULL;
			}
			else
			{
				InnerNode = ParseNode( *InnerName, IniFilename, IniSection, InParsedNodes );
			}
		}

		FDerivedDataBackendInterface* VerifyNode = NULL;
		if( InnerNode )
		{
			IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("VerifyDDC/")), false, true);

			const bool bFix = GetParsedBool( Entry, TEXT("Fix=") );
			VerifyNode = new Verify::FDerivedDataBackendVerifyWrapper( InnerNode, bFix );
		}
		else
		{
			UE_LOG( LogDerivedDataCache, Warning, TEXT("Unable to find inner node %s for Verify node %s. Verify node will not be created."), *InnerName, NodeName );
		}

		return VerifyNode;
	}

	/**
	 * Creates AsyncPut wrapper interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @param IniFilename ini filename.
	 * @param IniSection ini section containing graph definition
	 * @param InParsedNodes map of nodes and their names which have already been parsed
	 * @return AsyncPut wrapper backend interface instance or NULL if unsuccessfull
	 */
	FDerivedDataBackendInterface* ParseAsyncPut( const TCHAR* NodeName, const TCHAR* Entry, const FString& IniFilename, const TCHAR* IniSection, TMap<FString, FDerivedDataBackendInterface*>& InParsedNodes )
	{
		FDerivedDataBackendInterface* InnerNode = NULL;
		FString InnerName;
		if( FParse::Value( Entry, TEXT("Inner="), InnerName ) )
		{
			FDerivedDataBackendInterface** ParsedInnerNode = InParsedNodes.Find( InnerName );
			if( ParsedInnerNode )
			{
				UE_LOG( LogDerivedDataCache, Warning, TEXT("Inner node %s for AsyncPut node %s already exists. Nodes can only be used once."), *InnerName, NodeName );
				return NULL;
			}
			else
			{
				InnerNode = ParseNode( *InnerName, IniFilename, IniSection, InParsedNodes );
			}
		}

		FDerivedDataBackendInterface* AsyncNode = NULL;
		if( InnerNode )
		{
			AsyncNode = AsyncPut::CreateAsyncPutDerivedDataBackend( InnerNode, /*bCacheInFlightPuts*/ true );
		}
		else
		{
			UE_LOG( LogDerivedDataCache, Warning, TEXT("Unable to find inner node %s for AsyncPut node %s. AsyncPut node will not be created."), *InnerName, NodeName );
		}

		return AsyncNode;
	}

	/**
	 * Creates KeyLength wrapper interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @param IniFilename ini filename.
	 * @param IniSection ini section containing graph definition
	 * @param InParsedNodes map of nodes and their names which have already been parsed
	 * @return KeyLength wrapper backend interface instance or NULL if unsuccessfull
	 */
	FDerivedDataBackendInterface* ParseKeyLength( const TCHAR* NodeName, const TCHAR* Entry, const FString& IniFilename, const TCHAR* IniSection, TMap<FString, FDerivedDataBackendInterface*>& InParsedNodes )
	{
		FDerivedDataBackendInterface* InnerNode = nullptr;
		FString InnerName;
		if( FParse::Value( Entry, TEXT("Inner="), InnerName ) )
		{
			FDerivedDataBackendInterface** ParsedInnerNode = InParsedNodes.Find( InnerName );
			if( ParsedInnerNode )
			{
				UE_LOG( LogDerivedDataCache, Warning, TEXT("Inner node %s for KeyLength node %s already exists. Nodes can only be used once."), *InnerName, NodeName );
				return NULL;
			}
			else
			{
				InnerNode = ParseNode( *InnerName, IniFilename, IniSection, InParsedNodes );
			}
		}

		if( InnerNode )
		{
			int32 KeyLength = MAX_BACKEND_KEY_LENGTH;
			FParse::Value( Entry, TEXT("Length="), KeyLength );
			MaxKeyLength = FMath::Clamp( KeyLength, 0, MAX_BACKEND_KEY_LENGTH );
		}
		else
		{
			UE_LOG( LogDerivedDataCache, Warning, TEXT("Unable to find inner node %s for KeyLength node %s. KeyLength node will not be created."), *InnerName, NodeName );
		}

		return InnerNode;
	}

	/**
	 * Creates Hierarchical interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @param IniFilename ini filename.
	 * @param IniSection ini section containing graph definition
	 * @param InParsedNodes map of nodes and their names which have already been parsed
	 * @return Hierarchical backend interface instance or NULL if unsuccessfull
	 */
	FDerivedDataBackendInterface* ParseHierarchicalCache( const TCHAR* NodeName, const TCHAR* Entry, const FString& IniFilename, const TCHAR* IniSection, TMap<FString, FDerivedDataBackendInterface*>& InParsedNodes )
	{
		const TCHAR* InnerMatch = TEXT("Inner=");
		const int32 InnerMatchLength = FCString::Strlen( InnerMatch );

		TArray< FDerivedDataBackendInterface* > InnerNodes;
		FString InnerName;
		while ( FParse::Value( Entry, InnerMatch, InnerName ) )
		{
			// Check if the child has already been parsed
			FDerivedDataBackendInterface** ParsedInnerNode = InParsedNodes.Find( InnerName );
			if( ParsedInnerNode )
			{
				UE_LOG( LogDerivedDataCache, Warning, TEXT("Inner node %s for hierarchical node %s already exists. Nodes can only be used once."), *InnerName, NodeName );
			}
			else
			{
				FDerivedDataBackendInterface* InnerNode = ParseNode( *InnerName, IniFilename, IniSection, InParsedNodes );
				if( InnerNode )
				{
					InnerNodes.Add( InnerNode );
				}
				else
				{
					UE_LOG( LogDerivedDataCache, Log, TEXT("Unable to find inner node %s for hierarchical cache %s."), *InnerName, NodeName );
				}
			}

			// Move the Entry pointer forward so that we can find more children
			Entry = FCString::Strifind( Entry, InnerMatch );
			Entry += InnerMatchLength;
		}

		FDerivedDataBackendInterface* Hierarchy = NULL;
		if( InnerNodes.Num() > 1 )
		{
			Hierarchical::FHierarchicalDerivedDataBackend* HierarchyBackend = new Hierarchical::FHierarchicalDerivedDataBackend( InnerNodes );
			Hierarchy = HierarchyBackend;
			if (HierarchicalWrapper == NULL)
			{
				HierarchicalWrapper = HierarchyBackend;
			}
		}
		else if ( InnerNodes.Num() == 1 )
		{
			Hierarchy = InnerNodes[ 0 ];
			InnerNodes.Empty();
		}
		else
		{
			UE_LOG( LogDerivedDataCache, Warning, TEXT("Hierarchical cache %s has no inner backends and will not be created."), NodeName );
		}

		return Hierarchy;
	}

	/**
	 * Creates Filesystem data cache interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @return Filesystem data cache backend interface instance or NULL if unsuccessfull
	 */
	FDerivedDataBackendInterface* ParseDataCache( const TCHAR* NodeName, const TCHAR* Entry )
	{
		FDerivedDataBackendInterface* DataCache = NULL;

		// Parse Path by default, it may be overwriten by EnvPathOverride
		FString Path;
		FParse::Value( Entry, TEXT("Path="), Path );

		// Check the EnvPathOverride environment variable to allow persistent overriding of data cache path, eg for offsite workers.
		FString EnvPathOverride;
		if( FParse::Value( Entry, TEXT("EnvPathOverride="), EnvPathOverride ) )
		{
			FString FilesystemCachePathEnv = FPlatformMisc::GetEnvironmentVariable( *EnvPathOverride );
			if( FilesystemCachePathEnv.Len() > 0 )
			{
				Path = FilesystemCachePathEnv;
				UE_LOG( LogDerivedDataCache, Log, TEXT("Found environment variable %s=%s"), *EnvPathOverride, *Path );
			}
		}

		if (!EnvPathOverride.IsEmpty())
		{
			FString DDCPath;
			if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *EnvPathOverride, DDCPath))
			{
				if (DDCPath.Len() > 0)
				{
					Path = DDCPath;
					UE_LOG( LogDerivedDataCache, Log, TEXT("Found registry key GlobalDataCachePath %s=%s"), *EnvPathOverride, *Path );
				}
			}
		}

		// Check the CommandLineOverride argument to allow redirecting in build scripts
		FString CommandLineOverride;
		if( FParse::Value( Entry, TEXT("CommandLineOverride="), CommandLineOverride ) )
		{
			FString Value;
			if (FParse::Value(FCommandLine::Get(), *(CommandLineOverride + TEXT("=")), Value))
			{
				Path = Value;
				UE_LOG(LogDerivedDataCache, Log, TEXT("Found command line override %s=%s"), *CommandLineOverride, *Path);
			}
		}

		// Paths starting with a '?' are looked up from config
		if (Path.StartsWith(TEXT("?")) && !GConfig->GetString(TEXT("DerivedDataCacheSettings"), *Path + 1, Path, GEngineIni))
		{
			Path.Empty();
		}

		// Allow the user to override it from the editor
		FString EditorOverrideSetting;
		if(FParse::Value(Entry, TEXT("EditorOverrideSetting="), EditorOverrideSetting))
		{
			FString Setting = GConfig->GetStr(TEXT("/Script/UnrealEd.EditorSettings"), *EditorOverrideSetting, GEditorSettingsIni);
			if(Setting.Len() > 0)
			{
				FString SettingPath;
				if(FParse::Value(*Setting, TEXT("Path="), SettingPath))
				{
					SettingPath = SettingPath.TrimQuotes();
					if(SettingPath.Len() > 0)
					{
						Path = SettingPath;
					}
				}
			}
		}

		if( !Path.Len() )
		{
			UE_LOG( LogDerivedDataCache, Log, TEXT("%s data cache path not found in *engine.ini, will not use an %s cache."), NodeName, NodeName );
		}
		else if( Path == TEXT("None") )
		{
			UE_LOG( LogDerivedDataCache, Log, TEXT("Disabling %s data cache - path set to 'None'."), NodeName );
		}
		else
		{
			FDerivedDataBackendInterface* InnerFileSystem = NULL;

			// Try to set up the shared drive, allow user to correct any issues that may exist.
			bool RetryOnFailure = false;
			do
			{
				RetryOnFailure = false;

				// Don't create the file system if shared data cache directory is not mounted
				bool bShared = FCString::Stricmp(NodeName, TEXT("Shared")) == 0;
				
				// parameters we read here from the ini file
				FString WriteAccessLog;
				bool bPromptIfMissing = false;

				FParse::Value(Entry, TEXT("WriteAccessLog="), WriteAccessLog);
				FParse::Bool(Entry, TEXT("PromptIfMissing="), bPromptIfMissing);

				if (!bShared || IFileManager::Get().DirectoryExists(*Path))
				{
					InnerFileSystem = FileSystem::CreateFileSystemDerivedDataBackend(*Path, Entry, *WriteAccessLog);
				}

				if (InnerFileSystem)
				{
					bUsingSharedDDC |= bShared;

					DataCache = InnerFileSystem;
					UE_LOG(LogDerivedDataCache, Log, TEXT("Using %s data cache path %s: %s"), NodeName, *Path, !InnerFileSystem->IsWritable() ? TEXT("ReadOnly") : TEXT("Writable"));
					Directories.AddUnique(Path);
				}
				else
				{
					FString Message = FString::Printf(TEXT("%s data cache path (%s) is unavailable so cache will be disabled."), NodeName, *Path);
					
					UE_LOG(LogDerivedDataCache, Warning, TEXT("%s"), *Message);

					// Give the user a chance to retry incase they need to connect a network drive or something.
					if (bPromptIfMissing && !FApp::IsUnattended() && !IS_PROGRAM)
					{
						Message += FString::Printf(TEXT("\n\nRetry connection to %s?"), *Path);
						EAppReturnType::Type MessageReturn = FPlatformMisc::MessageBoxExt(EAppMsgType::YesNo, *Message, TEXT("Could not access DDC"));
						RetryOnFailure = MessageReturn == EAppReturnType::Yes;
					}
				}
			} while (RetryOnFailure);
		}

		return DataCache;
	}

	/**
	 * Creates an S3 data cache interface.
	 */
	FDerivedDataBackendInterface* ParseS3Cache(const TCHAR* NodeName, const TCHAR* Entry)
	{
		FString ManifestPath;
		if (!FParse::Value(Entry, TEXT("Manifest="), ManifestPath))
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'Manifest'."), NodeName);
			return nullptr;
		}

		FString BaseUrl;
		if (!FParse::Value(Entry, TEXT("BaseUrl="), BaseUrl))
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'BaseUrl'."), NodeName);
			return nullptr;
		}

		FString CanaryObjectKey;
		FParse::Value(Entry, TEXT("Canary="), CanaryObjectKey);

		FString Region;
		if (!FParse::Value(Entry, TEXT("Region="), Region))
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'Region'."), NodeName);
			return nullptr;
		}

		// Check the EnvPathOverride environment variable to allow persistent overriding of data cache path, eg for offsite workers.
		FString EnvPathOverride;
		FString CachePath = FPaths::ProjectSavedDir() / TEXT("S3DDC");
		if (FParse::Value(Entry, TEXT("EnvPathOverride="), EnvPathOverride))
		{
			FString FilesystemCachePathEnv = FPlatformMisc::GetEnvironmentVariable(*EnvPathOverride);
			if (FilesystemCachePathEnv.Len() > 0)
			{
				if (FilesystemCachePathEnv == TEXT("None"))
				{
					UE_LOG(LogDerivedDataCache, Log, TEXT("Node %s disabled due to %s=None"), NodeName, *EnvPathOverride);
					return nullptr;
				}
				else
				{
					CachePath = FilesystemCachePathEnv;
					UE_LOG(LogDerivedDataCache, Log, TEXT("Found environment variable %s=%s"), *EnvPathOverride, *CachePath);
				}
			}

			if (!EnvPathOverride.IsEmpty())
			{
				FString DDCPath;
				if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *EnvPathOverride, DDCPath))
				{
					if ( DDCPath.Len() > 0 )
					{
						CachePath = DDCPath;			
						UE_LOG( LogDerivedDataCache, Log, TEXT("Found registry key GlobalDataCachePath %s=%s"), *EnvPathOverride, *CachePath );
					}
				}
			}
		}

		if (FDerivedDataBackendInterface* Backend = S3::CreateS3DerivedDataBackend(*ManifestPath, *BaseUrl, *Region, *CanaryObjectKey, *CachePath))
		{
			return Backend;
		}

		UE_LOG(LogDerivedDataCache, Log, TEXT("S3 backend is not supported on the current platform."));
		return nullptr;
	}

	void ParseHttpCacheParams(
		const TCHAR* NodeName,
		const TCHAR* Entry,
		const FString& IniFilename,
		const TCHAR* IniSection,
		FString& Host,
		FString& Namespace,
		FString& StructuredNamespace,
		FString& OAuthProvider,
		FString& OAuthClientId,
		FString& OAuthSecret,
		bool& bReadOnly)
	{
		FString ServerId;
		if (FParse::Value(Entry, TEXT("ServerID="), ServerId))
		{
			FString ServerEntry;
			const TCHAR* ServerSection = TEXT("HordeStorageServers");
			if (GConfig->GetString(ServerSection, *ServerId, ServerEntry, IniFilename))
			{
				ParseHttpCacheParams(NodeName, *ServerEntry, IniFilename, IniSection, Host, Namespace, StructuredNamespace, OAuthProvider, OAuthClientId, OAuthSecret, bReadOnly);
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Warning, TEXT("Node %s is using ServerID=%s which was not found in [%s]"), NodeName, *ServerId, ServerSection);
			}
		}

		FParse::Value(Entry, TEXT("Host="), Host);

		FString EnvHostOverride;
		if (FParse::Value(Entry, TEXT("EnvHostOverride="), EnvHostOverride))
		{
			FString HostEnv = FPlatformMisc::GetEnvironmentVariable(*EnvHostOverride);
			if (!HostEnv.IsEmpty())
			{
				Host = HostEnv;
				UE_LOG(LogDerivedDataCache, Log, TEXT("Node %s found environment variable for Host %s=%s"), NodeName, *EnvHostOverride, *Host);
			}
		}

		FString CommandLineOverride;
		if (FParse::Value(Entry, TEXT("CommandLineHostOverride="), CommandLineOverride))
		{
			if (FParse::Value(FCommandLine::Get(), *(CommandLineOverride + TEXT("=")), Host))
			{
				UE_LOG(LogDerivedDataCache, Log, TEXT("Node %s found command line override for Host %s=%s"), NodeName, *CommandLineOverride, *Host);
			}
		}

		FParse::Value(Entry, TEXT("Namespace="), Namespace);
		FParse::Value(Entry, TEXT("StructuredNamespace="), StructuredNamespace);
		FParse::Value(Entry, TEXT("OAuthProvider="), OAuthProvider);
		FParse::Value(Entry, TEXT("OAuthClientId="), OAuthClientId);
		FParse::Value(Entry, TEXT("OAuthSecret="), OAuthSecret);
		FParse::Bool(Entry, TEXT("ReadOnly="), bReadOnly);
	}

	/**
	 * Creates a HTTP data cache interface.
	 */
	FDerivedDataBackendInterface* ParseHttpCache(
		const TCHAR* NodeName,
		const TCHAR* Entry,
		const FString& IniFilename,
		const TCHAR* IniSection)
	{
		FString Host;
		FString Namespace;
		FString StructuredNamespace;
		FString OAuthProvider;
		FString OAuthClientId;
		FString OAuthSecret;
		bool bReadOnly = false;

		ParseHttpCacheParams(NodeName, Entry, IniFilename, IniSection, Host, Namespace, StructuredNamespace, OAuthProvider, OAuthClientId, OAuthSecret, bReadOnly);

		if (Host.IsEmpty())
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'Host'"), NodeName);
			return nullptr;
		}

		if (Host == TEXT("None"))
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("Node %s is disabled because Host is set to 'None'"), NodeName);
			return nullptr;
		}

		if (Namespace.IsEmpty())
		{
			Namespace = FApp::GetProjectName();
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Node %s does not specify 'Namespace', falling back to '%s'"), NodeName, *Namespace);
		}

		if (StructuredNamespace.IsEmpty())
		{
			StructuredNamespace = Namespace;
		}

		if (OAuthProvider.IsEmpty())
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'OAuthProvider'"), NodeName);
			return nullptr;
		}

		if (OAuthClientId.IsEmpty())
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'OAuthClientId'"), NodeName);
			return nullptr;
		}

		if (OAuthSecret.IsEmpty())
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("Node %s does not specify 'OAuthSecret'"), NodeName);
			return nullptr;
		}

		FDerivedDataBackendInterface::ESpeedClass ForceSpeedClass = FDerivedDataBackendInterface::ESpeedClass::Unknown;
		FString ForceSpeedClassValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("HttpForceSpeedClass="), ForceSpeedClassValue))
		{
			if (ForceSpeedClassValue == TEXT("Slow"))
			{
				ForceSpeedClass = FDerivedDataBackendInterface::ESpeedClass::Slow;
			}
			else if (ForceSpeedClassValue == TEXT("Ok"))
			{
				ForceSpeedClass = FDerivedDataBackendInterface::ESpeedClass::Ok;
			}
			else if (ForceSpeedClassValue == TEXT("Fast"))
			{
				ForceSpeedClass = FDerivedDataBackendInterface::ESpeedClass::Fast;
			}
			else if (ForceSpeedClassValue == TEXT("Local"))
			{
				ForceSpeedClass = FDerivedDataBackendInterface::ESpeedClass::Local;
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Warning, TEXT("Node %s found unknown speed class override HttpForceSpeedClass=%s"), NodeName, *ForceSpeedClassValue);
			}
		}

		if (ForceSpeedClass != FDerivedDataBackendInterface::ESpeedClass::Unknown)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("Node %s found speed class override ForceSpeedClass=%s"), NodeName, *ForceSpeedClassValue);
		}

		return Http::CreateHttpDerivedDataBackend(
			NodeName, *Host, *Namespace, *StructuredNamespace, *OAuthProvider, *OAuthClientId, *OAuthSecret,
			ForceSpeedClass == FDerivedDataBackendInterface::ESpeedClass::Unknown ? nullptr : &ForceSpeedClass, bReadOnly);
	}

	/**
	 * Creates a Zen structured data cache interface
	 */
	FDerivedDataBackendInterface* ParseZenCache(const TCHAR* NodeName, const TCHAR* Entry)
	{
		FString ServiceUrl;
		FParse::Value(Entry, TEXT("Host="), ServiceUrl);

		FString Namespace;
		if (!FParse::Value(Entry, TEXT("Namespace="), Namespace))
		{
			Namespace = FApp::GetProjectName();
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Node %s does not specify 'Namespace', falling back to '%s'"), NodeName, *Namespace);
		}

		if (FDerivedDataBackendInterface* Backend = ZenCache::CreateZenDerivedDataBackend(NodeName, *ServiceUrl, *Namespace))
		{
			return Backend;
		}
		
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Zen backend is not yet supported in the current build configuration."));
		return nullptr;
	}

	/**
	 * Creates Boot data cache interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @param OutFilename filename specified for the cache
	 * @return Boot data cache backend interface instance or NULL if unsuccessful
	 */
	FFileBackedDerivedDataBackend* ParseBootCache( const TCHAR* NodeName, const TCHAR* Entry, FString& OutFilename )
	{
		FFileBackedDerivedDataBackend* Cache = nullptr;

		// Only allow boot cache with the editor. We don't want other tools and utilities (eg. SCW) writing to the same file.
#if WITH_EDITOR
		FString Filename;
		int64 MaxCacheSize = -1; // in MB
		const int64 MaxSupportedCacheSize = 2048; // 2GB

		FParse::Value( Entry, TEXT("MaxCacheSize="), MaxCacheSize);
		FParse::Value( Entry, TEXT("Filename="), Filename );
		if ( !Filename.Len() )
		{
			UE_LOG( LogDerivedDataCache, Warning, TEXT("FDerivedDataBackendGraph:  %s filename not found in *engine.ini, will not use %s cache."), NodeName, NodeName );
		}
		else
		{
			// make sure MaxCacheSize does not exceed 2GB
			MaxCacheSize = FMath::Min(MaxCacheSize, MaxSupportedCacheSize);

			UE_LOG( LogDerivedDataCache, Display, TEXT("Max Cache Size: %d MB"), MaxCacheSize);
			Cache = Memory::CreateMemoryDerivedDataBackend(TEXT("Boot"), MaxCacheSize * 1024 * 1024, /*bCanBeDisabled*/ true);

			if( Cache && Filename.Len() )
			{
				OutFilename = Filename;

				if (MaxCacheSize > 0 && IFileManager::Get().FileSize(*Filename) >= (MaxCacheSize * 1024 * 1024))
				{
					UE_LOG( LogDerivedDataCache, Warning, TEXT("FDerivedDataBackendGraph:  %s filename exceeds max size."), NodeName );
				}

				if (IFileManager::Get().FileSize(*Filename) < 0)
				{
					UE_LOG( LogDerivedDataCache, Display, TEXT("Starting with empty %s cache"), NodeName );
				}
				else if( Cache->LoadCache( *Filename ) )
				{
					UE_LOG( LogDerivedDataCache, Display, TEXT("Loaded %s cache: %s"), NodeName, *Filename );
				}
				else
				{
					UE_LOG( LogDerivedDataCache, Warning, TEXT("Could not load %s cache: %s"), NodeName, *Filename );
				}
			}
		}
#endif
		return Cache;
	}

	/**
	 * Creates Memory data cache interface from ini settings.
	 *
	 * @param NodeName Node name.
	 * @param Entry Node definition.
	 * @return Memory data cache backend interface instance or NULL if unsuccessfull
	 */
	FFileBackedDerivedDataBackend* ParseMemoryCache( const TCHAR* NodeName, const TCHAR* Entry )
	{
		FFileBackedDerivedDataBackend* Cache = nullptr;
		FString Filename;

		FParse::Value( Entry, TEXT("Filename="), Filename );
		Cache = Memory::CreateMemoryDerivedDataBackend(NodeName, /*MaxCacheSize*/ -1, /*bCanBeDisabled*/ false);
		if( Cache && Filename.Len() )
		{
			if( Cache->LoadCache( *Filename ) )
			{
				UE_LOG( LogDerivedDataCache, Display, TEXT("Loaded %s cache: %s"), NodeName, *Filename );
			}
			else
			{
				UE_LOG( LogDerivedDataCache, Warning, TEXT("Could not load %s cache: %s"), NodeName, *Filename );
			}
		}

		return Cache;
	}

	virtual ~FDerivedDataBackendGraph()
	{
		RootCache = NULL;
		DestroyCreatedBackends();
	}

	ILegacyCacheStore& GetRoot() override
	{
		check(RootCache);
		return *RootCache;
	}

	virtual int32 GetMaxKeyLength() const override
	{
		return MaxKeyLength;
	}

	virtual void NotifyBootComplete() override
	{
		check(RootCache);
		if (BootCache)
		{
			if( !FParse::Param( FCommandLine::Get(), TEXT("DDCNOSAVEBOOT") ) && !FParse::Param( FCommandLine::Get(), TEXT("Multiprocess") ) )
			{
				BootCache->SaveCache(*BootCacheFilename);
			}
			BootCache->Disable();
		}
	}

	virtual void WaitForQuiescence(bool bShutdown) override
	{
		double StartTime = FPlatformTime::Seconds();
		double LastPrint = StartTime;

		if (bShutdown)
		{
			bIsShuttingDown.store(true, std::memory_order_relaxed);
		}

		while (AsyncCompletionCounter.GetValue())
		{
			check(AsyncCompletionCounter.GetValue() >= 0);
			FPlatformProcess::Sleep(0.1f);
			if (FPlatformTime::Seconds() - LastPrint > 5.0)
			{
				UE_LOG(LogDerivedDataCache, Log, TEXT("Waited %ds for derived data cache to finish..."), int32(FPlatformTime::Seconds() - StartTime));
				LastPrint = FPlatformTime::Seconds();
			}
		}
		if (bShutdown)
		{
			FString MergePaks;
			if(WritePakCache && WritePakCache->IsWritable() && FParse::Value(FCommandLine::Get(), TEXT("MergePaks="), MergePaks))
			{
				TArray<FString> MergePakList;
				MergePaks.FString::ParseIntoArray(MergePakList, TEXT("+"));

				for(const FString& MergePakName : MergePakList)
				{
					TUniquePtr<PakFile::IPakFileDerivedDataBackend> ReadPak(
						PakFile::CreatePakFileDerivedDataBackend(*FPaths::Combine(*FPaths::GetPath(WritePakFilename), *MergePakName), /*bWriting*/ false, /*bCompressed*/ false));
					WritePakCache->MergeCache(ReadPak.Get());
				}
			}
			for (int32 ReadPakIndex = 0; ReadPakIndex < ReadPakCache.Num(); ReadPakIndex++)
			{
				ReadPakCache[ReadPakIndex]->Close();
			}
			if (WritePakCache && WritePakCache->IsWritable())
			{
				WritePakCache->Close();
				if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*WritePakFilename))
				{
					UE_LOG(LogDerivedDataCache, Error, TEXT("Pak file %s was not produced?"), *WritePakFilename);
				}
				else
				{
					if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*ReadPakFilename))
					{
						FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ReadPakFilename, false);
						if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*ReadPakFilename))
						{
							UE_LOG(LogDerivedDataCache, Error, TEXT("Could not delete the pak file %s to overwrite it with a new one."), *ReadPakFilename);
						}
					}
					if (!PakFile::IPakFileDerivedDataBackend::SortAndCopy(WritePakFilename, ReadPakFilename))
					{
						UE_LOG(LogDerivedDataCache, Error, TEXT("Couldn't sort pak file (%s)"), *WritePakFilename);
					}
					else if (!IFileManager::Get().Delete(*WritePakFilename))
					{
						UE_LOG(LogDerivedDataCache, Error, TEXT("Couldn't delete pak file (%s)"), *WritePakFilename);
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Display, TEXT("Sucessfully wrote %s."), *ReadPakFilename);
					}
				}
			}
		}
	}

	/** Get whether a shared cache is in use */
	virtual bool GetUsingSharedDDC() const override
	{
		return bUsingSharedDDC;
	}

	virtual const TCHAR* GetGraphName() const override
	{
		return *GraphName;
	}

	virtual const TCHAR* GetDefaultGraphName() const override
	{
		return FApp::IsEngineInstalled() ? TEXT("InstalledDerivedDataBackendGraph") : TEXT("DerivedDataBackendGraph");
	}

	virtual void AddToAsyncCompletionCounter(int32 Addend) override
	{
		AsyncCompletionCounter.Add(Addend);
		check(AsyncCompletionCounter.GetValue() >= 0);
	}

	virtual bool AnyAsyncRequestsRemaining() override
	{
		return AsyncCompletionCounter.GetValue() > 0;
	}

	virtual bool IsShuttingDown() override
	{
		return bIsShuttingDown.load(std::memory_order_relaxed);
	}

	virtual void GetDirectories(TArray<FString>& OutResults) override
	{
		OutResults = Directories;
	}

	static FORCEINLINE FDerivedDataBackendGraph& Get()
	{
		static FDerivedDataBackendGraph SingletonInstance;
		return SingletonInstance;
	}

	virtual FDerivedDataBackendInterface* MountPakFile(const TCHAR* PakFilename) override
	{
		// Assumptions: there's at least one read-only pak backend in the hierarchy
		// and its parent is a hierarchical backend.
		PakFile::IPakFileDerivedDataBackend* ReadPak = nullptr;
		if (HierarchicalWrapper && FPlatformFileManager::Get().GetPlatformFile().FileExists(PakFilename))
		{
			ReadPak = PakFile::CreatePakFileDerivedDataBackend(PakFilename, /*bWriting*/ false, /*bCompressed*/ false);

			HierarchicalWrapper->AddInnerBackend(ReadPak);
			CreatedBackends.Add(ReadPak);
			ReadPakCache.Add(ReadPak);
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Failed to add %s read-only pak DDC backend. Make sure it exists and there's at least one hierarchical backend in the cache tree."), PakFilename);
		}

		return ReadPak;
	}

	virtual bool UnmountPakFile(const TCHAR* PakFilename) override
	{
		for (int PakIndex = 0; PakIndex < ReadPakCache.Num(); ++PakIndex)
		{
			PakFile::IPakFileDerivedDataBackend* ReadPak = ReadPakCache[PakIndex];
			if (ReadPak->GetFilename() == PakFilename)
			{
				check(HierarchicalWrapper);

				// Wait until all async requests are complete.
				WaitForQuiescence(false);

				HierarchicalWrapper->RemoveInnerBackend(ReadPak);
				ReadPakCache.RemoveAt(PakIndex);
				CreatedBackends.Remove(ReadPak);
				ReadPak->Close();
				delete ReadPak;
				return true;
			}
		}
		return false;
	}

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override
	{
		if (RootCache)
		{
			return RootCache->GatherUsageStats();
		}

		return MakeShared<FDerivedDataCacheStatsNode>(TEXT(""), TEXT(""), /*bIsLocal*/ true);
	}

private:

	/** Delete all created backends in the reversed order they were created. */
	void DestroyCreatedBackends()
	{
		for (int32 BackendIndex = CreatedBackends.Num() - 1; BackendIndex >= 0; --BackendIndex)
		{
			delete CreatedBackends[BackendIndex];
		}
		CreatedBackends.Empty();
	}

	/** MountPak console command handler. */
	void UnmountPakCommandHandler(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("Usage: DDC.MountPak PakFilename"));
			return;
		}
		UnmountPakFile(*Args[0]);
	}

	/** UnmountPak console command handler. */
	void MountPakCommandHandler(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("Usage: DDC.UnmountPak PakFilename"));
			return;
		}
		MountPakFile(*Args[0]);
	}

	FThreadSafeCounter								AsyncCompletionCounter;
	FString											GraphName;
	FString											BootCacheFilename;
	FString											ReadPakFilename;
	FString											WritePakFilename;

	/** Root of the graph */
	FDerivedDataBackendInterface*					RootCache;

	/** References to all created backed interfaces */
	TArray< FDerivedDataBackendInterface* > CreatedBackends;

	/** Instances of backend interfaces which exist in only one copy */
	FFileBackedDerivedDataBackend*	BootCache;
	PakFile::IPakFileDerivedDataBackend* WritePakCache;
	FDerivedDataBackendInterface*	AsyncPutWrapper;
	Hierarchical::FHierarchicalDerivedDataBackend* HierarchicalWrapper;
	/** Support for multiple read only pak files. */
	TArray<PakFile::IPakFileDerivedDataBackend*> ReadPakCache;

	/** List of directories used by the DDC */
	TArray<FString> Directories;

	int32 MaxKeyLength = 0;

	/** Whether a shared cache is in use */
	bool bUsingSharedDDC;

	/** Whether a shutdown is pending */
	std::atomic<bool> bIsShuttingDown;

	/** MountPak console command */
	FAutoConsoleCommand MountPakCommand;
	/** UnmountPak console command */
	FAutoConsoleCommand UnountPakCommand;
};

} // UE::DerivedData::CacheStore::Graph

namespace UE::DerivedData
{

void FDerivedDataBackendInterface::LegacyPut(
	const TConstArrayView<FLegacyCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCachePutComplete&& OnComplete)
{
	for (const FLegacyCachePutRequest& Request : Requests)
	{
		FCompositeBuffer CompositeValue = Request.Value;
		Request.Key.WriteValueTrailer(CompositeValue);

		checkf(CompositeValue.GetSize() < MAX_int32,
			TEXT("Value is 2 GiB or greater, which is not supported for put of '%s' from '%s'"),
			*Request.Key.GetFullKey(), *Request.Name);
	
		UE_CLOG(Request.Key.HasShortKey(), LogDerivedDataCache, VeryVerbose,
			TEXT("ShortenKey %s -> %s"), *Request.Key.GetFullKey(), *Request.Key.GetShortKey());

		FSharedBuffer Value = MoveTemp(CompositeValue).ToShared();
		const TArrayView<const uint8> Data(MakeArrayView(static_cast<const uint8*>(Value.GetData()), int32(Value.GetSize())));
		const EPutStatus Status = PutCachedData(*Request.Key.GetShortKey(), Data, /*bPutEvenIfExists*/ false);
		OnComplete({Request.Name, Request.Key, Request.UserData, Status == EPutStatus::Cached ? EStatus::Ok : EStatus::Error});
	}
}

void FDerivedDataBackendInterface::LegacyGet(
	const TConstArrayView<FLegacyCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCacheGetComplete&& OnComplete)
{
	TArray<FString, TInlineAllocator<8>> ExistsKeys;
	TArray<const FLegacyCacheGetRequest*, TInlineAllocator<8>> ExistsRequests;

	TArray<FString, TInlineAllocator<8>> PrefetchKeys;
	TArray<const FLegacyCacheGetRequest*, TInlineAllocator<8>> PrefetchRequests;

	for (const FLegacyCacheGetRequest& Request : Requests)
	{
		if (!EnumHasAnyFlags(Request.Policy, ECachePolicy::Query))
		{
			OnComplete({Request.Name, Request.Key, {}, Request.UserData, EStatus::Error});
		}
		else if (EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData))
		{
			const bool bExists = !EnumHasAnyFlags(Request.Policy, ECachePolicy::Store);
			(bExists ? ExistsKeys : PrefetchKeys).Emplace(Request.Key.GetShortKey());
			(bExists ? ExistsRequests : PrefetchRequests).Add(&Request);
		}
		else
		{
			TArray<uint8> Data;
			const bool bGetOk = GetCachedData(*Request.Key.GetShortKey(), Data);
			FCompositeBuffer Value(MakeSharedBufferFromArray(MoveTemp(Data)));
			const bool bKeyOk = bGetOk && Request.Key.ReadValueTrailer(Value);
			OnComplete({Request.Name, Request.Key, MoveTemp(Value).ToShared(), Request.UserData, bKeyOk ? EStatus::Ok : EStatus::Error});
		}
	}

	if (!PrefetchKeys.IsEmpty())
	{
		const bool bOk = TryToPrefetch(PrefetchKeys);
		for (const FLegacyCacheGetRequest* const Request : PrefetchRequests)
		{
			OnComplete({Request->Name, Request->Key, {}, Request->UserData, bOk ? EStatus::Ok : EStatus::Error});
		}
	}

	if (!ExistsKeys.IsEmpty())
	{
		int32 Index = 0;
		const TBitArray<> Exists = CachedDataProbablyExistsBatch(ExistsKeys);
		for (const FLegacyCacheGetRequest* const Request : ExistsRequests)
		{
			OnComplete({Request->Name, Request->Key, {}, Request->UserData, Exists[Index] ? EStatus::Ok : EStatus::Error});
			++Index;
		}
	}
}

void FDerivedDataBackendInterface::LegacyDelete(
	const TConstArrayView<FLegacyCacheDeleteRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCacheDeleteComplete&& OnComplete)
{
	for (const FLegacyCacheDeleteRequest& Request : Requests)
	{
		RemoveCachedData(*Request.Key.GetShortKey(), Request.bTransient);
		OnComplete({Request.Name, Request.Key, Request.UserData, EStatus::Ok});
	}
}

FDerivedDataBackend& FDerivedDataBackend::Get()
{
	return CacheStore::Graph::FDerivedDataBackendGraph::Get();
}

/**
 * Parse debug options for the provided node name. Returns true if any options were specified
 */
bool FDerivedDataBackendInterface::FBackendDebugOptions::ParseFromTokens(FDerivedDataBackendInterface::FBackendDebugOptions& OutOptions, const TCHAR* InNodeName, const TCHAR* InInputTokens)
{
	// check if the input stream has any ddc options for this node
	FString PrefixKey = FString(TEXT("-ddc-")) + InNodeName;

	if (FCString::Stristr(InInputTokens, *PrefixKey) == nullptr)
	{
		// check if it has any -ddc-all- args
		PrefixKey = FString(TEXT("-ddc-all"));

		if (FCString::Stristr(InInputTokens, *PrefixKey) == nullptr)
		{
			return false;
		}
	}

	// turn -arg= into arg= for parsing
	PrefixKey.RightChopInline(1);

	/** types that can be set to ignored (-ddc-<name>-misstypes="foo+bar" etc) */
	// look for -ddc-local-misstype=AnimSeq+Audio -ddc-shared-misstype=AnimSeq+Audio 
	FString ArgName = FString::Printf(TEXT("%s-misstypes="), *PrefixKey);

	FString TempArg;
	FParse::Value(InInputTokens, *ArgName, TempArg);
	TempArg.ParseIntoArray(OutOptions.SimulateMissTypes, TEXT("+"), true);

	// look for -ddc-local-missrate=, -ddc-shared-missrate= etc
	ArgName = FString::Printf(TEXT("%s-missrate="), *PrefixKey);
	int MissRate = 0;
	FParse::Value(InInputTokens, *ArgName, OutOptions.RandomMissRate);

	// look for -ddc-local-speed=, -ddc-shared-speed= etc
	ArgName = FString::Printf(TEXT("%s-speed="), *PrefixKey);
	if (FParse::Value(InInputTokens, *ArgName, TempArg))
	{
		if (!TempArg.IsEmpty())
		{
			LexFromString(OutOptions.SpeedClass, *TempArg);
		}
	}

	return true;
}

/* Convenience function for backends that check if the key should be missed or not */
bool FDerivedDataBackendInterface::FBackendDebugOptions::ShouldSimulateMiss(const TCHAR* InCacheKey)
{
	if (!SimulateMissTypes.IsEmpty())
	{
		FStringView TypeStr(InCacheKey);
		TypeStr.LeftInline(TypeStr.Find(TEXT("_")));
		if (SimulateMissTypes.Contains(TypeStr))
		{
			return true;
		}
	}

	return RandomMissRate > 0 && FMath::RandHelper(100) < RandomMissRate;
}

/* Convenience function for backends that check if the key should be missed or not */
bool FDerivedDataBackendInterface::FBackendDebugOptions::ShouldSimulateMiss(const UE::DerivedData::FCacheKey& InCacheKey)
{
	if (!SimulateMissTypes.IsEmpty())
	{
		TStringBuilder<256> Type;
		Type << InCacheKey.Bucket;
		if (SimulateMissTypes.Contains(Type.ToView()))
		{
			return true;
		}
	}

	return RandomMissRate > 0 && FMath::RandHelper(100) < RandomMissRate;
}

} // UE::DerivedData

#undef LOCTEXT_NAMESPACE
