// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCrashReporterHandler.h"

#include "CoreMinimal.h"
#include "HAL/ThreadManager.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Misc/ScopeLock.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemSimulation.h"
#include "NiagaraComponent.h"

static int32 GbEnableNiagaraCRHandler = 0;
static FAutoConsoleVariableRef CVarEnableNiagaraCRHandler(
	TEXT("fx.EnableNiagaraCRHandler"),
	GbEnableNiagaraCRHandler,
	TEXT("If > 0 Niagara will push some state into the crash reporter. This is not free so should not be used unless actively tracking a crash in the wild. Even then it should only be enabled on the platforms needed etc. \n"),
	FConsoleVariableDelegate::CreateStatic(&FNiagaraCrashReporterHandler::OnEnabledChanged),
	ECVF_Default);

FCriticalSection FNiagaraCrashReporterHandler::CritSec;
TArray<FNiagaraCrashReporterHandler*> FNiagaraCrashReporterHandler::AllHandlers;

FNiagaraCrashReporterHandler::FNiagaraCrashReporterHandler()
{
	FString ThreadName;
	if (ThreadId == GGameThreadId)
	{
		ThreadName = TEXT("GameThread");
	}
	else if (ThreadId == GRenderThreadId)
	{
		ThreadName = TEXT("RenderThread");
	}
	else
	{
		ThreadName = FThreadManager::Get().GetThreadName(ThreadId);
	}

	Name = FString::Printf(TEXT("NiagaraCRInfo_%s_%u"), *ThreadName, ThreadId);
	Name = Name.Replace(TEXT(" "), TEXT("_"));
	NoneInfoString = TEXT("None");

	FScopeLock Lock(&CritSec);
	AllHandlers.AddUnique(this);
}

FNiagaraCrashReporterHandler::~FNiagaraCrashReporterHandler()
{
	FScopeLock Lock(&CritSec);
	AllHandlers.Remove(this);
}

void FNiagaraCrashReporterHandler::Clear()
{
	FScopeLock ScopeLock(&CritSec);

	ScopeInfoStack.Empty();
	FString Empty;
	//UE_LOG(LogNiagara, Log, TEXT("SetEngineData: %s"), *Empty);
	FGenericCrashContext::SetEngineData(Name, Empty);
}

void FNiagaraCrashReporterHandler::OnEnabledChanged(IConsoleVariable* InVariable)
{
	if (InVariable->GetInt() == 0)
	{
		for (FNiagaraCrashReporterHandler* Handler : AllHandlers)
		{
			Handler->Clear();
		}
	}
}

void FNiagaraCrashReporterHandler::SetInfo(const FString& Info)
{
	//UE_LOG(LogNiagara, Log, TEXT("SetEngineData: %s"), *Info);
	FGenericCrashContext::SetEngineData(Name, Info);
}

static const FString NullStr(TEXT("nullptr"));
void FNiagaraCrashReporterHandler::PushInfo(FNiagaraSystemInstance* Inst)
{
	FScopeLock ScopeLock(&CritSec);
	if (Inst)
	{
		PushInfo_Internal(Inst->GetCrashReporterTag());
	}
	else
	{
		PushInfo_Internal(NullStr);
	}
}

void FNiagaraCrashReporterHandler::PushInfo(FNiagaraSystemSimulation* SystemSim)
{
	FScopeLock ScopeLock(&CritSec);
	if (SystemSim)
	{
		PushInfo_Internal(SystemSim->GetCrashReporterTag());
	}
	else
	{
		PushInfo_Internal(NullStr);
	}
}

void FNiagaraCrashReporterHandler::PushInfo_Internal(const FString& Info)
{
	ScopeInfoStack.Push(Info);
	SetInfo(Info);
}

void FNiagaraCrashReporterHandler::PopInfo()
{
	FScopeLock ScopeLock(&CritSec);
	ScopeInfoStack.Pop();
	if (ScopeInfoStack.Num())
	{
		SetInfo(ScopeInfoStack.Top());
	}
	else
	{
		SetInfo(NoneInfoString);
	}
}

//////////////////////////////////////////////////////////////////////////

FNiagaraCrashReporterScope::FNiagaraCrashReporterScope(FNiagaraSystemInstance* Inst)
{
	bWasEnabled = GbEnableNiagaraCRHandler != 0;
	if (GbEnableNiagaraCRHandler)
	{
		FNiagaraCrashReporterHandler::Get().PushInfo(Inst);
	}
}

FNiagaraCrashReporterScope::FNiagaraCrashReporterScope(FNiagaraSystemSimulation* Sim)
{
	bWasEnabled = GbEnableNiagaraCRHandler != 0;
	if (GbEnableNiagaraCRHandler)
	{
		FNiagaraCrashReporterHandler::Get().PushInfo(Sim);
	}
}

FNiagaraCrashReporterScope::~FNiagaraCrashReporterScope()
{
	if(GbEnableNiagaraCRHandler)
	{
		FNiagaraCrashReporterHandler::Get().PopInfo();
	}
	else
	{
		//Cache this and clear in case GbEnableNiagaraCRHandler changes mid scope.
		if(bWasEnabled)
		{
			FNiagaraCrashReporterHandler::Get().Clear();
		}
	}
}
