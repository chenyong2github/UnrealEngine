// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/SessionService.h"
#include "SessionServicePrivate.h"
#include "Trace/DataStream.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceRedirector.h"
#include "SocketSubsystem.h"
#include "Trace/ControlClient.h"
#include "IPAddress.h"
#include "AddressInfoTypes.h"

#if PLATFORM_WINDOWS
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include "Windows/MinWindows.h"
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace Trace
{

FSessionService::FSessionService(FModuleService& InModuleService)
	: ModuleService(InModuleService)
{
	LocalSessionDirectory = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
	TraceStore = Store_Create(*LocalSessionDirectory);
	TraceRecorder = Recorder_Create(TraceStore.ToSharedRef());

	FTickerDelegate TickDelegate = FTickerDelegate::CreateRaw(this, &FSessionService::Tick);
	TickHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate, 0.5f);
}

FSessionService::~FSessionService()
{
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool FSessionService::StartRecorderServer()
{
	bool bOk = TraceRecorder->StartRecording();
#if PLATFORM_WINDOWS
	// Create a named event that other processes can use detect a running
	// recorder and connect to it automatically
	if (bOk && RecorderEvent == nullptr)
	{
		RecorderEvent = ::CreateEvent(nullptr, true, false, TEXT("Local\\UnrealInsightsRecorder"));
	}
#endif // PLATFORM_WINDOWS
	return bOk;
}

bool FSessionService::IsRecorderServerRunning() const
{
	return (TraceRecorder == nullptr) ? false : TraceRecorder->IsRunning();
}

void FSessionService::StopRecorderServer()
{
#if PLATFORM_WINDOWS
	if (RecorderEvent != nullptr)
	{
		::CloseHandle(RecorderEvent);
		RecorderEvent = nullptr;
	}
#endif // PLATFORM_WINDOWS
	TraceRecorder->StopRecording();
}

void FSessionService::GetAvailableSessions(TArray<FSessionHandle>& OutSessions) const
{
	FScopeLock Lock(&SessionsCS);
	OutSessions.Reserve(OutSessions.Num() + Sessions.Num());
	for (const auto& KV : Sessions)
	{
		OutSessions.Add(static_cast<FSessionHandle>(KV.Key));
	}
}

void FSessionService::GetLiveSessions(TArray<FSessionHandle>& OutSessions) const
{
	FScopeLock Lock(&SessionsCS);
	OutSessions.Reserve(OutSessions.Num() + Sessions.Num());
	for (const auto& KV : Sessions)
	{
		if (KV.Value.bIsLive)
		{
			OutSessions.Add(static_cast<FSessionHandle>(KV.Key));
		}
	}
}

bool FSessionService::GetSessionInfo(FSessionHandle SessionHandle, FSessionInfo& OutSessionInfo) const
{
	FScopeLock Lock(&SessionsCS);
	const FSessionInfoInternal* FindIt = Sessions.Find(SessionHandle);
	if (!FindIt)
	{
		return false;
	}
	OutSessionInfo.Uri = FindIt->Uri;
	OutSessionInfo.Name = FindIt->Name;
	OutSessionInfo.bIsLive = FindIt->bIsLive;
	return true;
}

Trace::IInDataStream* FSessionService::OpenSessionStream(FSessionHandle SessionHandle)
{
	return TraceStore->OpenSessionStream(SessionHandle);
}

Trace::IInDataStream* FSessionService::OpenSessionFromFile(const TCHAR* FilePath)
{
	return Trace::DataStream_ReadFile(FilePath);
}

void FSessionService::SetModuleEnabled(FSessionHandle SessionHandle, const FName& ModuleName, bool bState)
{
	FScopeLock Lock(&SessionsCS);
	FSessionInfoInternal* FindIt = Sessions.Find(SessionHandle);
	if (!FindIt)
	{
		return;
	}
	if (bState)
	{
		TArray<const TCHAR*> Loggers;
		if (!ModuleService.GetModuleLoggers(ModuleName, Loggers))
		{
			return;
		}
		TSet<FString>& EnabledLoggers = FindIt->EnabledModuleLoggersMap.FindOrAdd(ModuleName);
		for (const TCHAR* Logger : Loggers)
		{
			EnabledLoggers.Add(Logger);
		}
		if (FindIt->RecorderSessionHandle)
		{
			for (const FString& Logger : EnabledLoggers)
			{
				TraceRecorder->ToggleEvent(FindIt->RecorderSessionHandle, *Logger, true);
			}
		}
	}
	else
	{
		TSet<FString>* EnabledLoggers = FindIt->EnabledModuleLoggersMap.Find(ModuleName);
		if (EnabledLoggers)
		{
			if (FindIt->RecorderSessionHandle)
			{
				for (const FString& Logger : *EnabledLoggers)
				{
					TraceRecorder->ToggleEvent(FindIt->RecorderSessionHandle, *Logger, false);
				}
			}
			FindIt->EnabledModuleLoggersMap.Remove(ModuleName);
		}
	}
}

bool FSessionService::IsModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName) const
{
	FScopeLock Lock(&SessionsCS);
	const FSessionInfoInternal* FindIt = Sessions.Find(SessionHandle);
	if (!FindIt)
	{
		return false;
	}
	return FindIt->EnabledModuleLoggersMap.Contains(ModuleName);
}

bool FSessionService::ConnectSession(const TCHAR* ControlClientAddress)
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get();
	if (!Sockets)
	{
		return false;
	}
	bool bCanBindAll = false;
	TSharedPtr<FInternetAddr> RecorderAddr = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	if (!RecorderAddr.IsValid())
	{
		return false;
	}

	uint16 Port = 1985;
	FString AddressString = ControlClientAddress;
	const int32 LastColonIndex = AddressString.Find(":", ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (INDEX_NONE != LastColonIndex)
	{
		FString PortString = AddressString.RightChop(LastColonIndex + 1);
		Port = FCString::Atoi(*PortString);
		AddressString = AddressString.Left(LastColonIndex);
	}
	
	TSharedPtr<FInternetAddr> ClientAddr = Sockets->GetAddressFromString(AddressString);
	if (!ClientAddr.IsValid() || !ClientAddr->IsValid())
	{
		FAddressInfoResult GAIRequest = Sockets->GetAddressInfo(*AddressString, nullptr, EAddressInfoFlags::Default, NAME_None);
		if (GAIRequest.ReturnCode != SE_NO_ERROR || GAIRequest.Results.Num() == 0)
		{
			return false;
		}
		
		ClientAddr = GAIRequest.Results[0].Address;
	}
	
	ClientAddr->SetPort(Port);
	FControlClient ControlClient;
	if (!ControlClient.Connect(*ClientAddr))
	{
		return false;
	}
	ControlClient.SendConnect(*RecorderAddr->ToString(false));
	ControlClient.Disconnect();
	return true;
}

bool FSessionService::Tick(float DeltaTime)
{
	UpdateSessions();
	return true;
}

void FSessionService::UpdateSessions()
{
	TArray<FStoreSessionInfo> StoreSessions;
	TraceStore->GetAvailableSessions(StoreSessions);
	TArray<FRecorderSessionInfo> RecorderSessions;
	TraceRecorder->GetActiveSessions(RecorderSessions);

	FScopeLock Lock(&SessionsCS);
	for (const FStoreSessionInfo& StoreSession : StoreSessions)
	{
		FSessionInfoInternal& Session = Sessions.FindOrAdd(StoreSession.Handle);
		Session.Uri = StoreSession.Uri;
		Session.Name = StoreSession.Name;
		Session.bIsLive = StoreSession.bIsLive;
		Session.RecorderSessionHandle = 0;
	}

	for (const FRecorderSessionInfo& RecorderSession : RecorderSessions)
	{
		FSessionInfoInternal* FindIt = Sessions.Find(RecorderSession.StoreSessionHandle);
		if (FindIt)
		{
			FindIt->RecorderSessionHandle = RecorderSession.Handle;
		}
	}
}

}
