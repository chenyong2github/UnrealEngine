// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/SessionService.h"
#include "SessionServicePrivate.h"
#include "ModuleServicePrivate.h"
#include "AnalysisServicePrivate.h"
#include "Trace/DataStream.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceRedirector.h"
#include "SocketSubsystem.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
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

struct FDiagnosticsSessionAnalyzer
	: public Trace::IAnalyzer
{
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override
	{
		//if (Context.SessionContext.Version < 2)
		//{
		//	return;
		//}

		Context.InterfaceBuilder.RouteEvent(0, "Diagnostics", "Session");
	}

	virtual bool OnEvent(uint16, const FOnEventContext& Context) override
	{
		const FEventData& EventData = Context.EventData;
		const uint8* Attachment = EventData.GetAttachment();
		if (Attachment == nullptr)
		{
			return false;
		}

		uint8 AppNameOffset = EventData.GetValue<uint8>("AppNameOffset");
		uint8 CommandLineOffset = EventData.GetValue<uint8>("CommandLineOffset");

		Platform = FString(AppNameOffset, (const ANSICHAR*)Attachment);

		Attachment += AppNameOffset;
		int32 AppNameLength = CommandLineOffset - AppNameOffset;
		AppName = FString(AppNameLength, (const ANSICHAR*)Attachment);

		Attachment += AppNameLength;
		int32 CommandLineLength = EventData.GetAttachmentSize() - CommandLineOffset;
		CommandLine = FString(CommandLineLength, (const ANSICHAR*)Attachment);

		ConfigurationType = EventData.GetValue<int8>("ConfigurationType");
		TargetType = EventData.GetValue<int8>("TargetType");

		return false;
	}

	FString Platform;
	FString AppName;
	FString CommandLine;
	int8 ConfigurationType = 0;
	int8 TargetType = 0;
};

FSessionService::FSessionService(FModuleService& InModuleService, FAnalysisService& InAnalysisService)
	: FSessionService(InModuleService, InAnalysisService, nullptr)
{

}

FSessionService::FSessionService(FModuleService& InModuleService, FAnalysisService& InAnalysisService, const TCHAR* OverrideSessionDirectory)
	: ModuleService(InModuleService)
	, AnalysisService(InAnalysisService)
{
	if (OverrideSessionDirectory)
	{
		LocalSessionDirectory = FString(OverrideSessionDirectory);
	}
	else
	{
		LocalSessionDirectory = FPaths::ProjectSavedDir() / TEXT("TraceSessions");
	}
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
	OutSessionInfo.TimeStamp = FindIt->TimeStamp;
	OutSessionInfo.Size = FindIt->Size;
	OutSessionInfo.bIsLive = FindIt->bIsLive;
	OutSessionInfo.Platform = *FindIt->Platform;
	OutSessionInfo.AppName = *FindIt->AppName;
	OutSessionInfo.CommandLine = *FindIt->CommandLine;
	OutSessionInfo.ConfigurationType = EBuildConfiguration(FindIt->ConfigurationType);
	OutSessionInfo.TargetType = EBuildTargetType(FindIt->TargetType);
	return true;
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
		TArray<const TCHAR*> Loggers = ModuleService.GetModuleLoggers(ModuleName);
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

bool FSessionService::ToggleChannels(Trace::FSessionHandle SessionHandle, const TCHAR* Channels, bool bState)
{
	FScopeLock Lock(&SessionsCS);
	const FSessionInfoInternal* FindIt = Sessions.Find(SessionHandle);
	if (!FindIt)
	{
		return false;
	}
	return TraceRecorder->ToggleChannels(FindIt->RecorderSessionHandle, Channels, bState);
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
		AddressString.LeftInline(LastColonIndex);
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
	ControlClient.SendSendTo(*RecorderAddr->ToString(false));
	ControlClient.Disconnect();
	return true;
}

TSharedPtr<const IAnalysisSession> FSessionService::StartAnalysis(FSessionHandle SessionHandle)
{
	FScopeLock Lock(&SessionsCS);

	FSessionInfoInternal* FindIt = Sessions.Find(SessionHandle);
	if (!FindIt)
	{
		return nullptr;
	}
	
	TUniquePtr<IInDataStream> DataStream(TraceStore->OpenSessionStream(SessionHandle));
	if (!DataStream)
	{
		return nullptr;
	}

	if (!FindIt->CommandLine.IsEmpty())
	{
		TSet<FName> CommandLineEnabledModules = ModuleService.GetEnabledModulesFromCommandLine(*FindIt->CommandLine);
		for (const FName& EnabledModuleName : CommandLineEnabledModules)
		{
			TSet<FString>& EnabledLoggers = FindIt->EnabledModuleLoggersMap.FindOrAdd(EnabledModuleName);
			for (const TCHAR* Logger : ModuleService.GetModuleLoggers(EnabledModuleName))
			{
				EnabledLoggers.Add(Logger);
			}
		}
	}

	return AnalysisService.StartAnalysis(FindIt->Name, MoveTemp(DataStream));
}

bool FSessionService::Tick(float DeltaTime)
{
	UpdateSessions();
	return true;
}

void FSessionService::UpdateSessionContext(FStoreSessionHandle StoreHandle, FSessionInfoInternal& Info)
{
	if (Info.bIsUpdated)
	{
		return;
	}

	IInDataStream* Stream = TraceStore->OpenSessionStream(StoreHandle);

	struct FDataStream
		: public IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) override
		{
			if (BytesRead >= 48 * 1024)
			{
				return 0;
			}

			int32 InnerBytesRead = Inner->Read(Data, Size);
			BytesRead += InnerBytesRead;
			return InnerBytesRead;
		}

		int32 BytesRead = 0;
		IInDataStream* Inner;
	};

	FDataStream DataStream;
	DataStream.Inner = Stream;

	FDiagnosticsSessionAnalyzer Analyzer;
	FAnalysisContext Context;
	Context.AddAnalyzer(Analyzer);
	Context.Process(DataStream).Wait();

	delete Stream;

	if (Analyzer.Platform.Len() != 0)
	{
		Info.Platform = MoveTemp(Analyzer.Platform);
		Info.AppName = MoveTemp(Analyzer.AppName);
		Info.CommandLine = MoveTemp(Analyzer.CommandLine);
		Info.ConfigurationType = Analyzer.ConfigurationType;
		Info.TargetType = Analyzer.TargetType;
	}

	Info.bIsUpdated = true;
}

void FSessionService::UpdateSessions()
{
	TArray<FStoreSessionInfo> StoreSessions;
	TraceStore->GetAvailableSessions(StoreSessions);
	TArray<FRecorderSessionInfo> RecorderSessions;
	TraceRecorder->GetActiveSessions(RecorderSessions);

	TSet<FSessionHandle> SessionsToRemove;
	for (const auto& KV : Sessions)
	{
		SessionsToRemove.Add(KV.Key);
	}

	FScopeLock Lock(&SessionsCS);
	for (const FStoreSessionInfo& StoreSession : StoreSessions)
	{
		FSessionInfoInternal& Session = Sessions.FindOrAdd(StoreSession.Handle);
		SessionsToRemove.Remove(StoreSession.Handle);
		Session.Uri = StoreSession.Uri;
		Session.Name = StoreSession.Name;
		Session.TimeStamp = StoreSession.TimeStamp;
		Session.Size = StoreSession.Size;
		Session.bIsLive = StoreSession.bIsLive;
		Session.RecorderSessionHandle = 0;
	}

	for (FSessionHandle Session : SessionsToRemove)
	{
		Sessions.Remove(Session);
	}

	for (const FRecorderSessionInfo& RecorderSession : RecorderSessions)
	{
		FSessionInfoInternal* FindIt = Sessions.Find(RecorderSession.StoreSessionHandle);
		if (FindIt)
		{
			FindIt->RecorderSessionHandle = RecorderSession.Handle;
		}
	}

	for (auto& KeyValue : Sessions)
	{
		UpdateSessionContext(KeyValue.Key, KeyValue.Value);
	}
}

}
