// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesCommon.h"

#include "Online/AuthCommon.h"
#include "Online/FriendsCommon.h"

DEFINE_LOG_CATEGORY(LogOnlineServices);

namespace UE::Online {

uint32 FOnlineServicesCommon::NextInstanceIndex = 0;

FOnlineServicesCommon::FOnlineServicesCommon(const FString& InConfigName)
	: OpCache(InConfigName, *this)
	, InstanceIndex(NextInstanceIndex++)
	, ConfigProvider(MakeUnique<FOnlineConfigProviderGConfig>(GEngineIni))
	, ConfigName(InConfigName)
	, SerialQueue(ParallelQueue)
{
}

void FOnlineServicesCommon::Init()
{
	OpCache.SetLoadConfigFn(
		[this](FOperationConfig& OperationConfig, const TArray<FString>& SectionHeiarchy)
		{
			return LoadConfig(OperationConfig, SectionHeiarchy);
		});

	RegisterComponents();
	Initialize();
	PostInitialize();
}

void FOnlineServicesCommon::Destroy()
{
	PreShutdown();
	Shutdown();
}

IAuthPtr FOnlineServicesCommon::GetAuthInterface()
{
	return IAuthPtr(AsShared(), Get<IAuth>());
}

IFriendsPtr FOnlineServicesCommon::GetFriendsInterface()
{
	return IFriendsPtr(AsShared(), Get<IFriends>());
}

IPresencePtr FOnlineServicesCommon::GetPresenceInterface()
{
	return IPresencePtr(AsShared(), Get<IPresence>());
}

IExternalUIPtr FOnlineServicesCommon::GetExternalUIInterface()
{
	return IExternalUIPtr(AsShared(), Get<IExternalUI>());
}

ILobbiesPtr FOnlineServicesCommon::GetLobbiesInterface()
{
	return ILobbiesPtr(AsShared(), Get<ILobbies>());
}

IConnectivityPtr FOnlineServicesCommon::GetConnectivityInterface()
{
	return IConnectivityPtr(AsShared(), Get<IConnectivity>());
}

IPrivilegesPtr FOnlineServicesCommon::GetPrivilegesInterface()
{
	return IPrivilegesPtr(AsShared(), Get<IPrivileges>());
}

TOnlineResult<FGetResolvedConnectString> FOnlineServicesCommon::GetResolvedConnectString(FGetResolvedConnectString::Params&& Params)
{
	return TOnlineResult<FGetResolvedConnectString>(Errors::NotImplemented());
}

void FOnlineServicesCommon::RegisterComponents()
{
}

void FOnlineServicesCommon::Initialize()
{
	Components.Visit(&IOnlineComponent::Initialize);
}

void FOnlineServicesCommon::PostInitialize()
{
	Components.Visit(&IOnlineComponent::PostInitialize);
}

void FOnlineServicesCommon::LoadConfig()
{
	Components.Visit(&IOnlineComponent::LoadConfig);
}

bool FOnlineServicesCommon::Tick(float DeltaSeconds)
{
	Components.Visit(&IOnlineComponent::Tick, DeltaSeconds);

	ParallelQueue.Tick(DeltaSeconds);

	return true;
}

void FOnlineServicesCommon::PreShutdown()
{
	Components.Visit(&IOnlineComponent::PreShutdown);
}

void FOnlineServicesCommon::Shutdown()
{
	Components.Visit(&IOnlineComponent::Shutdown);
}

FOnlineAsyncOpQueueParallel& FOnlineServicesCommon::GetParallelQueue()
{
	return ParallelQueue;
}

FOnlineAsyncOpQueue& FOnlineServicesCommon::GetSerialQueue()
{
	return SerialQueue;
}

FOnlineAsyncOpQueue& FOnlineServicesCommon::GetSerialQueue(const FOnlineAccountIdHandle& AccountId)
{
	TUniquePtr<FOnlineAsyncOpQueueSerial>* Queue = PerUserSerialQueue.Find(AccountId);
	if (Queue == nullptr)
	{
		Queue = &PerUserSerialQueue.Emplace(AccountId, MakeUnique<FOnlineAsyncOpQueueSerial>(ParallelQueue));
	}

	return **Queue;
}

void FOnlineServicesCommon::RegisterExecHandler(const FString& Name, TUniquePtr<IOnlineExecHandler>&& Handler)
{
	ExecCommands.Emplace(Name, MoveTemp(Handler));
}

bool FOnlineServicesCommon::Exec(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("OnlineServices")))
	{
		int Index = 0;
		if (FParse::Value(Cmd, TEXT("Index="), Index) && Index == InstanceIndex)
		{
			FParse::Token(Cmd, false); // skip over Index=#

			FString Command;
			if (FParse::Token(Cmd, Command, false))
			{
				if (TUniquePtr<IOnlineExecHandler>* ExecHandler = ExecCommands.Find(Command))
				{
					return (*ExecHandler)->Exec(World, Cmd, Ar);
				}
			}
		}
		else if (FParse::Command(&Cmd, TEXT("List")))
		{
			Ar.Logf(TEXT("%u: %s"), InstanceIndex, *GetConfigName());
		}
	}
	return false;
}

/* UE::Online */ }
