// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Trace/Trace.h"

#if UE_TRACE_ENABLED

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "String/ParseTokens.h"
#include "Templates/UnrealTemplate.h"
#include "Trace/Trace.inl"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

////////////////////////////////////////////////////////////////////////////////
class FTraceAuxiliaryImpl
{
public:
	void					ParseCommandLine(const TCHAR* CommandLine);
	bool 					Start(const TCHAR* ChannelSet);
	bool 					Stop();
	const TCHAR*			GetPath() const;

private:
	enum class EState : uint8
	{
		None,
		Tracing,
		Stopped,
	};

	bool					SendToHost(const TCHAR* Host);
	bool					WriteToFile(const TCHAR* Path=nullptr);
	void					ToggleChannels(const TCHAR* Channels);
	FString					GetChannels(const TCHAR* ChannelSet) const;
	TMap<uint32, FString>	ActiveChannels;
	FString					TracePath;
	EState					State = EState::None;
};

static FTraceAuxiliaryImpl GTraceAuxiliary;

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::ToggleChannels(const TCHAR* Channels)
{
	UE::String::ParseTokens(Channels, TEXT(","), [this] (const FStringView& Token)
	{
		TCHAR ChannelName[64];
		const size_t ChannelNameSize = Token.CopyString(ChannelName, 63);
		ChannelName[ChannelNameSize] = '\0';

		uint32 ChannelHash = 5381;
		for (const TCHAR* c = ChannelName; *c; ++c)
		{
            ChannelHash = ((ChannelHash << 5) + ChannelHash) + *c;
		}

		if (ActiveChannels.Find(ChannelHash) != nullptr)
		{
			return;
		}

		ActiveChannels.Add(ChannelHash, ChannelName);

		Trace::ToggleChannel(ChannelName, true);
	});
}

////////////////////////////////////////////////////////////////////////////////
FString FTraceAuxiliaryImpl::GetChannels(const TCHAR* ChannelSet) const
{
	FString Value;

	if (ChannelSet == nullptr)
	{
		if (!GConfig->GetString(TEXT("Trace.ChannelPresets"), TEXT("Default"), Value, GEngineIni))
		{
			Value = TEXT("cpu,frame,log,bookmark");
		}
	}
	else if (!GConfig->GetString(TEXT("Trace.ChannelPresets"), ChannelSet, Value, GEngineIni))
	{
		Value = ChannelSet;
	}

	return Value;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::ParseCommandLine(const TCHAR* CommandLine)
{
	if (State >= EState::Tracing)
	{
		return;
	}

	bool bOk = false;
	FString Parameter;

	// Start tracing if it isn't already
	if (FParse::Value(CommandLine, TEXT("-tracehost="), Parameter))
	{
		bOk = SendToHost(*Parameter);
	}

	else if (FParse::Value(CommandLine, TEXT("-tracefile="), Parameter))
	{
		bOk = WriteToFile(*Parameter);
	}

	else if (FParse::Param(CommandLine, TEXT("tracefile")))
	{
		bOk = WriteToFile();
	}

	const TCHAR* ChannelSet = nullptr;
	if (FParse::Value(CommandLine, TEXT("-trace="), Parameter, false))
	{
		ChannelSet = *Parameter;
	}

	if (!bOk && ChannelSet == nullptr)
	{
		return;
	}

	FString Channels = GetChannels(ChannelSet);
	ToggleChannels(*Channels);

	State = bOk ? EState::Tracing : EState::None;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::Start(const TCHAR* ChannelSet)
{
	if (State < EState::Tracing)
	{
		if (!WriteToFile())
		{
			return false;
		}
	}

	FString Channels = GetChannels(ChannelSet);
	ToggleChannels(*Channels);

	State = EState::Tracing;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::Stop()
{
	if (State < EState::Tracing)
	{
		return false;
	}

	for (const auto& ChannelPair : ActiveChannels)
	{
		const FString& Name = ChannelPair.Value;
		Trace::ToggleChannel(*Name, false);
	}
	ActiveChannels.Reset();

	State = EState::Stopped;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::SendToHost(const TCHAR* Host)
{
	if (!Trace::SendTo(Host))
	{
		UE_LOG(LogCore, Warning, TEXT("Unable to trace to host '%s'"), Host);
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::WriteToFile(const TCHAR* Path)
{
	if (Path == nullptr)
	{
		FString Name = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S.utrace"));
		return WriteToFile(*Name);
	}

	FString WritePath;

	// If there's no slash in the path, we'll put it in the profiling directory
	if (FCString::Strchr(Path, '\\') == nullptr && FCString::Strchr(Path, '/') == nullptr)
	{
		WritePath = FPaths::ProfilingDir();
		WritePath += Path;
	}
	else
	{
		WritePath = Path;
	}

	// The user may not have provided a suitable extension
	if (!WritePath.EndsWith(".utrace"))
	{
		WritePath += ".utrace";
	}

	IFileManager& FileManager = IFileManager::Get();

	// Ensure we can write the trace file appropriately
	FString WriteDir = FPaths::GetPath(WritePath);
	if (!FileManager.MakeDirectory(*WriteDir, true))
	{
		UE_LOG(LogCore, Warning, TEXT("Failed to create directory '%s'"), *WriteDir);
		return false;
	}

	if (FileManager.FileExists(*WritePath))
	{
		UE_LOG(LogCore, Warning, TEXT("Trace file '%s' already exists"), *WritePath);
		return false;
	}

	// Finally, tell trace to write the trace to a file.
	FString NativePath = FileManager.ConvertToAbsolutePathForExternalAppForWrite(*WritePath);
	if (!Trace::WriteTo(*NativePath))
	{
		UE_LOG(LogCore, Warning, TEXT("Unable to trace to file '%s'"), *WritePath);
		return false;
	}

	TracePath = MoveTemp(WritePath);
	return true;
}

////////////////////////////////////////////////////////////////////////////////
const TCHAR* FTraceAuxiliaryImpl::GetPath() const
{
	return *TracePath;
}



////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryStart(const TArray<FString>& Args)
{
	const TCHAR* Channels = (Args.Num() > 0) ? *(Args[0]) : nullptr;

	if (!GTraceAuxiliary.Start(Channels))
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("Failed to start tracing to a file"));
		return;
	}

	// Give the user some feedback that everything's underway.
	Channels = (Channels != nullptr) ? Channels : TEXT("[default]");
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing to; %s"), GTraceAuxiliary.GetPath());
	UE_LOG(LogConsoleResponse, Log, TEXT("Trace channels; %s"), Channels);
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryStop()
{
	if (!GTraceAuxiliary.Stop())
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("Unable to stop tracing"));
		return;
	}

	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing paused. Use 'Trace.Start' to resume"));
}

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryStartCmd(
	TEXT("Trace.Start"),
	TEXT(
		"Begin tracing profiling events to a file; Trace.Start [ChannelSet]"
		" where ChannelSet is either comma-separated list of trace channels, a Config/Trace.ChannelPresets key, or optional."
	),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceAuxiliaryStart)
);

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryStopCmd(
	TEXT("Trace.Stop"),
	TEXT("Stops tracing profiling events"),
	FConsoleCommandDelegate::CreateStatic(TraceAuxiliaryStop)
);

#endif // UE_TRACE_ENABLED



////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::Initialize(const TCHAR* CommandLine)
{
#if UE_TRACE_ENABLED
	// Initialize Trace
	Trace::FInitializeDesc Desc;
	Desc.bUseWorkerThread = FPlatformProcess::SupportsMultithreading();
	Trace::Initialize(Desc);

	FCoreDelegates::OnEndFrame.AddStatic(Trace::Update);

	GTraceAuxiliary.ParseCommandLine(CommandLine);

	// Trace out information about this session
	{
		uint8 Payload[1024];
		int32 PayloadSize = 0;

		auto AddToPayload = [&] (const TCHAR* String) -> uint8
		{
			int32 Length = FCString::Strlen(String);
			Length = FMath::Min<int32>(Length, sizeof(Payload) - PayloadSize - 1);
			for (int32 i = 0, n = Length; i < n; ++i)
			{
				Payload[PayloadSize] = uint8(String[i] & 0x7f);
				++PayloadSize;
			}
			return uint8(PayloadSize - Length);
		};

		AddToPayload(FGenericPlatformMisc::GetUBTPlatform());
		uint8 AppNameOffset = AddToPayload(TEXT(UE_APP_NAME));
		uint8 CommandLineOffset = AddToPayload(CommandLine);

		UE_TRACE_EVENT_BEGIN(Diagnostics, Session, Important)
			UE_TRACE_EVENT_FIELD(uint8, AppNameOffset)
			UE_TRACE_EVENT_FIELD(uint8, CommandLineOffset)
			UE_TRACE_EVENT_FIELD(uint8, ConfigurationType)
			UE_TRACE_EVENT_FIELD(uint8, TargetType)
		UE_TRACE_EVENT_END()

		UE_TRACE_LOG(Diagnostics, Session, TraceLogChannel, uint16(PayloadSize))
			<< Session.AppNameOffset(AppNameOffset)
			<< Session.CommandLineOffset(CommandLineOffset)
			<< Session.ConfigurationType(uint8(FApp::GetBuildConfiguration()))
			<< Session.TargetType(uint8(FApp::GetBuildTargetType()))
			<< Session.Attachment(Payload, PayloadSize);
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::TryAutoConnect()
{
#if UE_TRACE_ENABLED
	#if PLATFORM_WINDOWS
	// If we can detect a named event then we can try and auto-connect to UnrealInsights.
	HANDLE KnownEvent = ::OpenEvent(EVENT_ALL_ACCESS, false, TEXT("Local\\UnrealInsightsRecorder"));
	if (KnownEvent != nullptr)
	{
		const TCHAR* Params = TEXT("-tracehost=127.0.0.1 -trace=log");
		if (FParse::Param(FCommandLine::Get(), TEXT("trace")))
		{
			Params = TEXT("-tracehost=127.0.0.1");
		}

		GTraceAuxiliary.ParseCommandLine(Params);
		::CloseHandle(KnownEvent);
	}
	#endif
#endif
}
