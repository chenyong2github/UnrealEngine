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

	Name = FString::Printf(TEXT("NiagaraCRInfo | %s(%u)"), *ThreadName, ThreadId);
	
	NoneInfoString = FString::Printf(TEXT("%s | None |"), *Name);

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
	ScopeInfoStack.Empty();

	FScopeLock ScopeLock(&CritSec);
	FString Empty;
	UE_LOG(LogNiagara, Log, TEXT("SetEngineData: %s"), *Empty);
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
	FScopeLock ScopeLock(&CritSec);
	//UE_LOG(LogNiagara, Log, TEXT("SetEngineData: %s"), *Info);
	FGenericCrashContext::SetEngineData(Name, Info);
}

void FNiagaraCrashReporterHandler::PushInfo(FNiagaraSystemInstance* Inst)
{
	check(Inst && Inst->GetComponent() && Inst->GetComponent()->GetAsset());
	UNiagaraComponent* Comp = Inst->GetComponent();
	USceneComponent* AttachParent = Comp->GetAttachParent();

	PushInfo(FString::Printf(TEXT("%s | SystemInstance | System: %s | bSolo: %s | Component: %s | AttachedTo: %s |"), 
	*Name, *Comp->GetAsset()->GetFullName(), Inst->IsSolo() ? TEXT("true") : TEXT("false"), *Comp->GetFullName(), AttachParent ? *AttachParent->GetFullName() : TEXT("nullptr")));

	//TODO: More info?
}

void FNiagaraCrashReporterHandler::PushInfo(FNiagaraSystemSimulation* SystemSim)
{
	check(SystemSim && SystemSim->GetSystem());
	
	UNiagaraSystem* System = SystemSim->GetSystem();

	PushInfo(FString::Printf(TEXT("%s | SystemSimulation | System: %s | bSolo: %s |"), *Name, *System->GetFullName(), SystemSim->GetIsSolo() ? TEXT("true") : TEXT("false")));

	//TODO: More info?
}

void FNiagaraCrashReporterHandler::PushInfo(const FString& Info)
{
	ScopeInfoStack.Push(Info);
	SetInfo(Info);
}

void FNiagaraCrashReporterHandler::PopInfo()
{
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
