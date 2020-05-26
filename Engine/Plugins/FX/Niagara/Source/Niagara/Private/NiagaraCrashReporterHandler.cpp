// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCrashReporterHandler.h"

#include "CoreMinimal.h"
#include "HAL/ThreadManager.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Misc/ScopeLock.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemSimulation.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

#if WITH_NIAGARA_CRASHREPORTER

static int32 GbEnableNiagaraCRHandler = 0;
static FAutoConsoleVariableRef CVarEnableNiagaraCRHandler(
	TEXT("fx.EnableNiagaraCRHandler"),
	GbEnableNiagaraCRHandler,
	TEXT("If > 0 Niagara will push some state into the crash reporter. This is not free so should not be used unless actively tracking a crash in the wild. Even then it should only be enabled on the platforms needed etc. \n"),
	ECVF_Default);

class FNiagaraCrashReporterHandler
{
public:
	FNiagaraCrashReporterHandler()
	{
		NullString = TEXT("nullptr");
	}

	~FNiagaraCrashReporterHandler()
	{
	}

	static FNiagaraCrashReporterHandler& Get()
	{
		static TUniquePtr<FNiagaraCrashReporterHandler> Instance = MakeUnique<FNiagaraCrashReporterHandler>();
		return *Instance.Get();
	}

	void PushInfo(const FString& Info)
	{
		const uint32 ThreadID = FPlatformTLS::GetCurrentThreadId();

		FScopeLock LockGuard(&RWGuard);
		ThreadScopeInfoStack.FindOrAdd(ThreadID).Push(Info);
		UpdateInfo();
	}

	void PushInfo(FNiagaraSystemInstance* Inst)
	{
		PushInfo(Inst ? Inst->GetCrashReporterTag() : NullString);
	}

	void PushInfo(FNiagaraSystemSimulation* SystemSim)
	{
		PushInfo(SystemSim ? SystemSim->GetCrashReporterTag() : NullString);
	}

	void PushInfo(UNiagaraSystem* System)
	{
		PushInfo(System ? System->GetCrashReporterTag() : NullString);
	}

	void PopInfo()
	{
		const uint32 ThreadID = FPlatformTLS::GetCurrentThreadId();

		FScopeLock LockGuard(&RWGuard);
		ThreadScopeInfoStack[ThreadID].Pop();
		UpdateInfo();
	}

private:
	void UpdateInfo()
	{
		static const FString CrashReportKey = TEXT("NiagaraCRInfo");
		static const FString GameThreadString = TEXT("GameThread");
		static const FString RenderThreadString = TEXT("RenderThread");
		static const FString OtherThreadString = TEXT("OtherThread");

		CurrentInfo.Empty();
		for (auto it = ThreadScopeInfoStack.CreateIterator(); it; ++it)
		{
			if (it->Value.Num() == 0)
			{
				continue;
			}

			if (it->Key == GGameThreadId)
			{
				CurrentInfo.Append(GameThreadString);
			}
			else if (it->Key == GRenderThreadId)
			{
				CurrentInfo.Append(RenderThreadString);
			}
			else
			{
				CurrentInfo.Append(OtherThreadString);
			}
			CurrentInfo.AppendChar('(');
			CurrentInfo.AppendInt(it->Key);
			CurrentInfo.AppendChars(") ", 2);
			CurrentInfo.Append(it->Value.Last());
			CurrentInfo.AppendChar('\n');
		}

		FGenericCrashContext::SetEngineData(CrashReportKey, CurrentInfo);
	}

private:
	FCriticalSection				RWGuard;
	TMap<uint32, TArray<FString>>	ThreadScopeInfoStack;
	FString							CurrentInfo;
	FString							NullString;
};

FNiagaraCrashReporterScope::FNiagaraCrashReporterScope(FNiagaraSystemInstance* Inst)
{
	bWasEnabled = GbEnableNiagaraCRHandler != 0;
	if (bWasEnabled)
	{
		FNiagaraCrashReporterHandler::Get().PushInfo(Inst);
	}
}

FNiagaraCrashReporterScope::FNiagaraCrashReporterScope(FNiagaraSystemSimulation* Sim)
{
	bWasEnabled = GbEnableNiagaraCRHandler != 0;
	if (bWasEnabled)
	{
		FNiagaraCrashReporterHandler::Get().PushInfo(Sim);
	}
}

FNiagaraCrashReporterScope::FNiagaraCrashReporterScope(UNiagaraSystem* System)
{
	bWasEnabled = GbEnableNiagaraCRHandler != 0;
	if (bWasEnabled)
	{
		FNiagaraCrashReporterHandler::Get().PushInfo(System);
	}
}

FNiagaraCrashReporterScope::~FNiagaraCrashReporterScope()
{
	if(bWasEnabled)
	{
		FNiagaraCrashReporterHandler::Get().PopInfo();
	}
}

#endif //WITH_NIAGARA_CRASHREPORTER
