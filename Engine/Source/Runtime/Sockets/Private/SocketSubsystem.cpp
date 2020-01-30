// Copyright Epic Games, Inc. All Rights Reserved.

#include "SocketSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreMisc.h"
#include "SocketSubsystemModule.h"
#include "Modules/ModuleManager.h"
#include "Async/AsyncWork.h"
#include "SocketTypes.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "Templates/UniquePtr.h"

DEFINE_LOG_CATEGORY(LogSockets);

IMPLEMENT_MODULE( FSocketSubsystemModule, Sockets );

/** Values for the Protocol Typenames */
namespace FNetworkProtocolTypes
{
	const FLazyName IPv4(TEXT("IPv4"));
	const FLazyName IPv6(TEXT("IPv6"));
}

/** Each platform will implement these functions to construct/destroy socket implementations */
extern FName CreateSocketSubsystem(FSocketSubsystemModule& SocketSubsystemModule);
extern void DestroySocketSubsystem(FSocketSubsystemModule& SocketSubsystemModule);

/** Helper function to turn the friendly subsystem name into the module name */
static inline FName GetSocketModuleName(const FString& SubsystemName)
{
	FName ModuleName;
	FString SocketBaseName(TEXT("Sockets"));

	if (!SubsystemName.StartsWith(SocketBaseName, ESearchCase::CaseSensitive))
	{
		ModuleName = FName(*(SocketBaseName + SubsystemName));
	}
	else
	{
		ModuleName = FName(*SubsystemName);
	}

	return ModuleName;
}

/**
 * Helper function that loads a given platform service module if it isn't already loaded
 *
 * @param SubsystemName Name of the requested platform service to load
 * @return The module interface of the requested platform service, NULL if the service doesn't exist
 */
static IModuleInterface* LoadSubsystemModule(const FString& SubsystemName)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_SHIPPING_WITH_EDITOR
	// Early out if we are overriding the module load
	bool bAttemptLoadModule = !FParse::Param(FCommandLine::Get(), *FString::Printf(TEXT("no%s"), *SubsystemName));
	if (bAttemptLoadModule)
#endif
	{
		FName ModuleName;
		FModuleManager& ModuleManager = FModuleManager::Get();

		ModuleName = GetSocketModuleName(SubsystemName);
		if (!ModuleManager.IsModuleLoaded(ModuleName))
		{
			// Attempt to load the module
			ModuleManager.LoadModule(ModuleName);
		}

		return ModuleManager.GetModule(ModuleName);
	}

	return nullptr;
}

/**
 * Shutdown all registered subsystems
 */
void ISocketSubsystem::ShutdownAllSystems()
{
	if (IsInGameThread() && FModuleManager::Get().IsModuleLoaded(TEXT("Sockets")) == true)
	{
		// Unloading the Sockets module will call FSocketSubsystemModule::ShutdownSocketSubsystem()
		const bool bIsShutdown = true;
		FModuleManager::Get().UnloadModule(TEXT("Sockets"), bIsShutdown);
	}
}


/**
 * Called right after the module DLL has been loaded and the module object has been created
 * Overloaded to allow the default subsystem a chance to load
 */
void FSocketSubsystemModule::StartupModule()
{
	FString InterfaceString;

	// Initialize the platform defined socket subsystem first
	DefaultSocketSubsystem = CreateSocketSubsystem( *this );
}

/**
 * Called before the module is unloaded, right before the module object is destroyed.
 * Overloaded to shut down all loaded online subsystems
 */
void FSocketSubsystemModule::ShutdownModule()
{
	ShutdownSocketSubsystem();
}

void FSocketSubsystemModule::ShutdownSocketSubsystem()
{
	// Destroy the platform defined socket subsystem first
	DestroySocketSubsystem( *this );

	FModuleManager& ModuleManager = FModuleManager::Get();
	// Unload all the supporting factories
	for (TMap<FName, ISocketSubsystem*>::TIterator It(SocketSubsystems); It; ++It)
	{
		It.Value()->Shutdown();
		// Unloading the module will do proper cleanup
		FName ModuleName = GetSocketModuleName(It.Key().ToString());

		const bool bIsShutdown = true;
		ModuleManager.UnloadModule(ModuleName, bIsShutdown);
	} 
	//ensure(SocketSubsystems.Num() == 0);
}


/** 
 * Register a new socket subsystem interface with the base level factory provider
 *
 * @param FactoryName - name of subsystem as referenced by consumers
 * @param Factory - instantiation of the socket subsystem interface, this will take ownership
 * @param bMakeDefault - make this subsystem the default
 */
void FSocketSubsystemModule::RegisterSocketSubsystem(const FName FactoryName, ISocketSubsystem* Factory, bool bMakeDefault)
{
	if (!SocketSubsystems.Contains(FactoryName))
	{
		SocketSubsystems.Add(FactoryName, Factory);
	}

	if (bMakeDefault)
	{
		DefaultSocketSubsystem = FactoryName;
	}
}

/** 
 * Unregister an existing online subsystem interface from the base level factory provider
 * @param FactoryName - name of subsystem as referenced by consumers
 */
void FSocketSubsystemModule::UnregisterSocketSubsystem(const FName FactoryName)
{
	if (SocketSubsystems.Contains(FactoryName))
	{
		SocketSubsystems.Remove(FactoryName);
	}
}


/** 
 * Main entry point for accessing a socket subsystem by name
 * Will load the appropriate module if the subsystem isn't currently loaded
 * It's possible that the subsystem doesn't exist and therefore can return NULL
 *
 * @param SubsystemName - name of subsystem as referenced by consumers
 * @return Requested socket subsystem, or NULL if that subsystem was unable to load or doesn't exist
 */
ISocketSubsystem* FSocketSubsystemModule::GetSocketSubsystem(const FName InSubsystemName)
{
	FName SubsystemName = InSubsystemName;
	if (SubsystemName == NAME_None)
	{
		SubsystemName = DefaultSocketSubsystem;
	}

	ISocketSubsystem** SocketSubsystemFactory = SocketSubsystems.Find(SubsystemName);
	if (SocketSubsystemFactory == nullptr)
	{
		// Attempt to load the requested factory
		IModuleInterface* NewModule = LoadSubsystemModule(SubsystemName.ToString());
		if (NewModule)
		{
			// If the module loaded successfully this should be non-NULL;
			SocketSubsystemFactory = SocketSubsystems.Find(SubsystemName);
		}

		if (SocketSubsystemFactory == nullptr)
		{
			UE_LOG(LogSockets, Warning, TEXT("Unable to load SocketSubsystem module %s"), *InSubsystemName.ToString());
		}
	}

	return (SocketSubsystemFactory == nullptr) ? nullptr : *SocketSubsystemFactory;
}



//////////////////////////////////
// ISocketSubsystem
/////////////////////////////////


ISocketSubsystem* ISocketSubsystem::Get(const FName& SubsystemName)
{
	// wrap the platform file with a logger
	//	static TUniquePtr<FLoggedPlatformFile> AutoDestroyLog;
	//	AutoDestroyLog = MakeUnique<FLoggedPlatformFile>(*CurrentPlatformFile);

	struct FStatic
	{
		FSocketSubsystemModule& SSSModule;

		FStatic()
			: SSSModule( FModuleManager::LoadModuleChecked<FSocketSubsystemModule>("Sockets") )
		{}

		~FStatic()
		{
			ISocketSubsystem::ShutdownAllSystems();
		}
	};

	static FStatic StaticSockets;
	return StaticSockets.SSSModule.GetSocketSubsystem(SubsystemName); 
}

int32 ISocketSubsystem::BindNextPort(FSocket* Socket, FInternetAddr& Addr, int32 PortCount, int32 PortIncrement)
{
	// go until we reach the limit (or we succeed)
	for (int32 Index = 0; Index < PortCount; Index++)
	{
		// try to bind to the current port
		if (Socket->Bind(Addr) == true)
		{
			// if it succeeded, return the port
			if (Addr.GetPort() != 0)
			{
				return Addr.GetPort();
			}
			else
			{
				return Socket->GetPortNo();
			}
		}
		// if the address had no port, we are done
		if( Addr.GetPort() == 0 )
		{
			break;
		}

		// increment to the next port, and loop!
		Addr.SetPort(Addr.GetPort() + PortIncrement);
	}

	return 0;
}

/** Async task support for GetAddressInfo */
class FGetAddressInfoTask : public FNonAbandonableTask
{
public:
	friend class FAutoDeleteAsyncTask<FGetAddressInfoTask>;

	FGetAddressInfoTask(class ISocketSubsystem* InSocketSubsystem, const FString& InQueryHost,
		const FString& InQueryService, EAddressInfoFlags InQueryFlags, const FName& InQueryProtocol,
		ESocketType InQuerySocketType, FAsyncGetAddressInfoCallback InCallbackFunction) :

		SocketSubsystem(InSocketSubsystem),
		QueryHost(InQueryHost),
		QueryService(InQueryService),
		QueryFlags(InQueryFlags),
		QueryProtocol(InQueryProtocol),
		QuerySocketType(InQuerySocketType),
		CallbackFunction(InCallbackFunction)
	{

	}

	void DoWork()
	{
		CallbackFunction(SocketSubsystem->GetAddressInfo(*QueryHost, *QueryService, QueryFlags, QueryProtocol, QuerySocketType));
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FGetAddressInfoTask, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	ISocketSubsystem* SocketSubsystem;
	const FString QueryHost;
	const FString QueryService;
	EAddressInfoFlags QueryFlags;
	const FName QueryProtocol;
	ESocketType QuerySocketType;
	FAsyncGetAddressInfoCallback CallbackFunction;
};

void ISocketSubsystem::GetAddressInfoAsync(FAsyncGetAddressInfoCallback Callback, const TCHAR* HostName, const TCHAR* ServiceName,
	EAddressInfoFlags QueryFlags, const FName ProtocolTypeName, ESocketType SocketType)
{
	(new FAutoDeleteAsyncTask<FGetAddressInfoTask>(this, HostName, ServiceName, QueryFlags, ProtocolTypeName, SocketType, Callback))->StartBackgroundTask();
}

TSharedRef<FInternetAddr> ISocketSubsystem::GetLocalBindAddr(FOutputDevice& Out)
{
	bool bCanBindAll;
	// look up the local host address
	TSharedRef<FInternetAddr> BindAddr = GetLocalHostAddr(Out, bCanBindAll);

	// If we can bind to all addresses, return the any address
	if (bCanBindAll)
	{
		BindAddr->SetAnyAddress();
	}

	// return it
	return BindAddr;

}

FResolveInfo* ISocketSubsystem::GetHostByName(const ANSICHAR* HostName)
{
	FResolveInfo* Result = NULL;
	TSharedPtr<FInternetAddr> Addr;
	// See if we have it cached or not
	if (GetHostByNameFromCache(HostName, Addr))
	{
		Result = CreateResolveInfoCached(Addr);
	}
	else
	{
		// Create an async resolve info
		FResolveInfoAsync* AsyncResolve = new FResolveInfoAsync(HostName);
		AsyncResolve->StartAsyncTask();
		Result = AsyncResolve;
	}
	return Result;
}

TSharedRef<FInternetAddr> ISocketSubsystem::GetLocalHostAddr(FOutputDevice& Out, bool& bCanBindAll)
{
	TSharedRef<FInternetAddr> HostAddr = CreateInternetAddr();
	FString HostName;
	bCanBindAll = false;

	if (GetMultihomeAddress(HostAddr))
	{
		return HostAddr;
	}

	if (GetHostName(HostName) == false)
	{
		Out.Logf(TEXT("%s: gethostname failed (%s)"), GetSocketAPIName(), GetSocketError());
	}

	// Failing to find the host is not considered an error and we just bind to any address
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ESocketErrors FindHostResult = GetHostByName(TCHAR_TO_ANSI(*HostName), *HostAddr);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (FindHostResult == SE_NO_ERROR || FindHostResult == SE_HOST_NOT_FOUND 
		|| FindHostResult == SE_EWOULDBLOCK || FindHostResult == SE_TRY_AGAIN)
	{
		if( !FParse::Param(FCommandLine::Get(),TEXT("PRIMARYNET")) )
		{
			bCanBindAll = true;
		}
		static bool First;
		if( !First )
		{
			First = true;
			UE_LOG(LogInit, Log, TEXT("%s: I am %s (%s)"), GetSocketAPIName(), *HostName, *HostAddr->ToString(true) );
		}
	}
	else
	{
		Out.Logf(TEXT("GetHostByName failed (%s)"), GetSocketError(FindHostResult));
		HostAddr->SetAnyAddress();
	}

	// return the newly created address
	return HostAddr;
}

bool ISocketSubsystem::GetMultihomeAddress(TSharedRef<FInternetAddr>& Addr)
{
	TCHAR Home[256] = TEXT("");
	if (FParse::Value(FCommandLine::Get(), TEXT("MULTIHOME="), Home, UE_ARRAY_COUNT(Home)))
	{
		TSharedPtr<FInternetAddr> MultiHomeQuery = GetAddressFromString(Home);
		if (Home == NULL || !MultiHomeQuery.IsValid())
		{
			UE_LOG(LogSockets, Log, TEXT("Invalid multihome IP address %s"), Home);
			return false;
		}
		else
		{
			Addr = MultiHomeQuery.ToSharedRef();
		}

		return true;
	}
	return false;
}

bool ISocketSubsystem::GetHostByNameFromCache(const ANSICHAR* HostName, TSharedPtr<FInternetAddr>& Addr)
{
	// Lock for thread safety
	FScopeLock sl(&HostNameCacheSync);
	// Now search for the entry
	TSharedPtr<FInternetAddr>* FoundAddr = HostNameCache.Find(FString(HostName));
	if (FoundAddr)
	{
		Addr = *FoundAddr;
	}
	return FoundAddr != NULL;
}

void ISocketSubsystem::AddHostNameToCache(const ANSICHAR* HostName, TSharedPtr<FInternetAddr> Addr)
{
	// Lock for thread safety
	FScopeLock sl(&HostNameCacheSync);
	HostNameCache.Add(FString(HostName), Addr);
}

void ISocketSubsystem::RemoveHostNameFromCache(const ANSICHAR* HostName)
{
	// Lock for thread safety
	FScopeLock sl(&HostNameCacheSync);
	HostNameCache.Remove(FString(HostName));
}

ESocketProtocolFamily ISocketSubsystem::GetProtocolFamilyFromName(const FName& InProtocolName) const
{
	if (InProtocolName == FNetworkProtocolTypes::IPv6)
	{
		return ESocketProtocolFamily::IPv6;
	}
	else if (InProtocolName == FNetworkProtocolTypes::IPv4)
	{
		return ESocketProtocolFamily::IPv4;
	}

	return ESocketProtocolFamily::None;
}

FName ISocketSubsystem::GetProtocolNameFromFamily(ESocketProtocolFamily InProtocolFamily) const
{
	switch (InProtocolFamily)
	{
	default:
		return NAME_None;
	case ESocketProtocolFamily::IPv4:
		return FNetworkProtocolTypes::IPv4;
	case ESocketProtocolFamily::IPv6:
		return FNetworkProtocolTypes::IPv6;
	}
}

FResolveInfoCached* ISocketSubsystem::CreateResolveInfoCached(TSharedPtr<FInternetAddr> Addr) const
{
	return new FResolveInfoCached(*Addr);
}

/**
 * Returns a human readable string from an error code
 *
 * @param Code the error code to check
 */
const TCHAR* ISocketSubsystem::GetSocketError(ESocketErrors Code)
{
	if (Code == SE_GET_LAST_ERROR_CODE)
	{
		Code = GetLastErrorCode();
	}
	switch (Code)
	{
		case SE_NO_ERROR: return TEXT("SE_NO_ERROR");
		case SE_EINTR: return TEXT("SE_EINTR");
		case SE_EBADF: return TEXT("SE_EBADF");
		case SE_EACCES: return TEXT("SE_EACCES");
		case SE_EFAULT: return TEXT("SE_EFAULT");
		case SE_EINVAL: return TEXT("SE_EINVAL");
		case SE_EMFILE: return TEXT("SE_EMFILE");
		case SE_EWOULDBLOCK: return TEXT("SE_EWOULDBLOCK");
		case SE_EINPROGRESS: return TEXT("SE_EINPROGRESS");
		case SE_EALREADY: return TEXT("SE_EALREADY");
		case SE_ENOTSOCK: return TEXT("SE_ENOTSOCK");
		case SE_EDESTADDRREQ: return TEXT("SE_EDESTADDRREQ");
		case SE_EMSGSIZE: return TEXT("SE_EMSGSIZE");
		case SE_EPROTOTYPE: return TEXT("SE_EPROTOTYPE");
		case SE_ENOPROTOOPT: return TEXT("SE_ENOPROTOOPT");
		case SE_EPROTONOSUPPORT: return TEXT("SE_EPROTONOSUPPORT");
		case SE_ESOCKTNOSUPPORT: return TEXT("SE_ESOCKTNOSUPPORT");
		case SE_EOPNOTSUPP: return TEXT("SE_EOPNOTSUPP");
		case SE_EPFNOSUPPORT: return TEXT("SE_EPFNOSUPPORT");
		case SE_EAFNOSUPPORT: return TEXT("SE_EAFNOSUPPORT");
		case SE_EADDRINUSE: return TEXT("SE_EADDRINUSE");
		case SE_EADDRNOTAVAIL: return TEXT("SE_EADDRNOTAVAIL");
		case SE_ENETDOWN: return TEXT("SE_ENETDOWN");
		case SE_ENETUNREACH: return TEXT("SE_ENETUNREACH");
		case SE_ENETRESET: return TEXT("SE_ENETRESET");
		case SE_ECONNABORTED: return TEXT("SE_ECONNABORTED");
		case SE_ECONNRESET: return TEXT("SE_ECONNRESET");
		case SE_ENOBUFS: return TEXT("SE_ENOBUFS");
		case SE_EISCONN: return TEXT("SE_EISCONN");
		case SE_ENOTCONN: return TEXT("SE_ENOTCONN");
		case SE_ESHUTDOWN: return TEXT("SE_ESHUTDOWN");
		case SE_ETOOMANYREFS: return TEXT("SE_ETOOMANYREFS");
		case SE_ETIMEDOUT: return TEXT("SE_ETIMEDOUT");
		case SE_ECONNREFUSED: return TEXT("SE_ECONNREFUSED");
		case SE_ELOOP: return TEXT("SE_ELOOP");
		case SE_ENAMETOOLONG: return TEXT("SE_ENAMETOOLONG");
		case SE_EHOSTDOWN: return TEXT("SE_EHOSTDOWN");
		case SE_EHOSTUNREACH: return TEXT("SE_EHOSTUNREACH");
		case SE_ENOTEMPTY: return TEXT("SE_ENOTEMPTY");
		case SE_EPROCLIM: return TEXT("SE_EPROCLIM");
		case SE_EUSERS: return TEXT("SE_EUSERS");
		case SE_EDQUOT: return TEXT("SE_EDQUOT");
		case SE_ESTALE: return TEXT("SE_ESTALE");
		case SE_EREMOTE: return TEXT("SE_EREMOTE");
		case SE_EDISCON: return TEXT("SE_EDISCON");
		case SE_SYSNOTREADY: return TEXT("SE_SYSNOTREADY");
		case SE_VERNOTSUPPORTED: return TEXT("SE_VERNOTSUPPORTED");
		case SE_NOTINITIALISED: return TEXT("SE_NOTINITIALISED");
		case SE_HOST_NOT_FOUND: return TEXT("SE_HOST_NOT_FOUND");
		case SE_TRY_AGAIN: return TEXT("SE_TRY_AGAIN");
		case SE_NO_RECOVERY: return TEXT("SE_NO_RECOVERY");
		case SE_NO_DATA: return TEXT("SE_NO_DATA");
		case SE_UDP_ERR_PORT_UNREACH: return TEXT("SE_UDP_ERR_PORT_UNREACH");
		case SE_ADDRFAMILY: return TEXT("SE_ADDRFAMILY");
		case SE_SYSTEM: return TEXT("SE_SYSTEM");
		case SE_NODEV: return TEXT("SE_NODEV");
		default: return TEXT("Unknown Error");
	};
}

TUniquePtr<FRecvMulti> ISocketSubsystem::CreateRecvMulti(int32 MaxNumPackets, int32 MaxPacketSize,
															ERecvMultiFlags Flags/*=ERecvMultiFlags::None*/)
{
	UE_LOG(LogSockets, Warning, TEXT("RecvMulti is not supported by current socket subsystem."));

	return nullptr;
}

bool ISocketSubsystem::IsSocketRecvMultiSupported() const
{
	return false;
}

double ISocketSubsystem::TranslatePacketTimestamp(const FPacketTimestamp& Timestamp,
													ETimestampTranslation Translation/*=ETimestampTranslation::LocalTimestamp*/)
{
	UE_LOG(LogSockets, Warning, TEXT("TranslatePacketTimestamp is not supported by current socket subsystem."));

	return 0.0;
}

//////////////////////////////////
// SocketSubsystem Testing
/////////////////////////////////
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

static void DebugPrintGaiResults(ISocketSubsystem* SocketSub, const FAddressInfoResult& GAIResult)
{
	UE_LOG(LogSockets, Log, TEXT("Got %d GAI Results for hostname %s. Error Code: %s [%d]"), GAIResult.Results.Num(), *GAIResult.QueryHostName,
		SocketSub->GetSocketError(GAIResult.ReturnCode), GAIResult.ReturnCode);

	for (int i = 0; i < GAIResult.Results.Num(); ++i)
	{
		UE_LOG(LogSockets, Log, TEXT("Result #%d Address: %s Type: %s"), i, *GAIResult.Results[i].Address->ToString(false),
			*GAIResult.Results[i].Address->GetProtocolType().ToString());
	}
}

static void RunGAIQuery(const FString& HostStr)
{
	if (HostStr.IsEmpty())
	{
		UE_LOG(LogSockets, Warning, TEXT("SOCKETSUB GAI requires an input string to test with."));
		return;
	}

	ISocketSubsystem* SocketSub = ISocketSubsystem::Get();
	if (SocketSub)
	{
		FAddressInfoResult GAIResult = SocketSub->GetAddressInfo(*HostStr, nullptr, EAddressInfoFlags::AllResultsWithMapping | EAddressInfoFlags::OnlyUsableAddresses, NAME_None);
		if (GAIResult.Results.Num() > 0)
		{
			DebugPrintGaiResults(SocketSub, GAIResult);
		}
		else
		{
			UE_LOG(LogSockets, Warning, TEXT("Did not get results!"));
		}
		return;
	}

	UE_LOG(LogSockets, Warning, TEXT("Failed to get socket subsystem!"));
}

static void RunAsyncGAIQuery(const FString& HostStr)
{
	if (HostStr.IsEmpty())
	{
		UE_LOG(LogSockets, Warning, TEXT("SOCKETSUB ASYNCGAI requires an input string to test with."));
		return;
	}

	ISocketSubsystem* SocketSub = ISocketSubsystem::Get();
	if (SocketSub)
	{
		double AsyncRequestStartTime = FPlatformTime::Seconds();
		FAsyncGetAddressInfoCallback CallbackFunc = [SocketSub, AsyncRequestStartTime](FAddressInfoResult Results)
		{
			UE_LOG(LogSockets, Log, TEXT("Async GAI Request returned after %f seconds, started at %f"), FPlatformTime::Seconds() - AsyncRequestStartTime, AsyncRequestStartTime);
			DebugPrintGaiResults(SocketSub, Results);
		};
		SocketSub->GetAddressInfoAsync(CallbackFunc, *HostStr, nullptr, EAddressInfoFlags::Default, NAME_None);
		return;
	}

	UE_LOG(LogSockets, Warning, TEXT("Failed to get socket subsystem!"));
}

static void RunAddressSerialize(const FString& InputStr)
{
	if (InputStr.IsEmpty())
	{
		UE_LOG(LogSockets, Warning, TEXT("SOCKETSUB Serialize requires an ip address to test with."));
		return;
	}

	ISocketSubsystem* SocketSub = ISocketSubsystem::Get();
	if (SocketSub)
	{
		TSharedPtr<FInternetAddr> NewAddr = SocketSub->GetAddressFromString(InputStr);
		if (NewAddr.IsValid())
		{
			UE_LOG(LogSockets, Log, TEXT("Result Address: %s Type: %s"), *NewAddr->ToString(false), *NewAddr->GetProtocolType().ToString());
		}
		else
		{
			UE_LOG(LogSockets, Warning, TEXT("Did not get results!"));
		}
		return;
	}

	UE_LOG(LogSockets, Warning, TEXT("Failed to get socket subsystem!"));
}

static bool SocketSubsystemCommandHandler(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("SOCKETSUB")))
	{
		if (FParse::Command(&Cmd, TEXT("GAI")))
		{
			RunGAIQuery(FParse::Token(Cmd, true));
		}
		else if (FParse::Command(&Cmd, TEXT("SERIALIZE")))
		{
			RunAddressSerialize(FParse::Token(Cmd, true));
		}
		else if (FParse::Command(&Cmd, TEXT("ASYNCGAI")))
		{
			RunAsyncGAIQuery(FParse::Token(Cmd, true));
		}

		return true;
	}

	return false;
}

FStaticSelfRegisteringExec FSocketSubsystemExecs(SocketSubsystemCommandHandler);
#endif // WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
