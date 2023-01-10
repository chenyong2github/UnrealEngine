// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/PCGExtraCapture.h"

#include "PCGComponent.h"

#if WITH_EDITOR

void PCGUtils::FExtraCapture::ResetTimers()
{
	Timers.Empty();
}

void PCGUtils::FExtraCapture::ResetCapturedMessages()
{
	CapturedMessages.Empty();
}

void PCGUtils::FExtraCapture::Update(const PCGUtils::FScopedCall& InScopedCall)
{
	if (!InScopedCall.Context->Node)
	{
		return;
	}

	FScopeLock ScopedLock(&Lock);

	TArray<FCallTime>* TimesPtr = Timers.Find(InScopedCall.Context->Node);
	TArray<FCapturedMessage>* CapturedMessagesPtr = CapturedMessages.Find(InScopedCall.Context->Node);

	if (!TimesPtr)
	{
		TimesPtr = &Timers.Add(InScopedCall.Context->Node);
	}

	if (!CapturedMessagesPtr)
	{
		CapturedMessagesPtr = &CapturedMessages.Add(InScopedCall.Context->Node);
	}

	check(TimesPtr && CapturedMessagesPtr);

	const double ThisFrameTime = FPlatformTime::Seconds() - InScopedCall.StartTime;

	constexpr int32 MaxNumberOfTrackedTimers = 100;

	FCallTime* Time = !TimesPtr->IsEmpty() ? &TimesPtr->Last() : nullptr;
	check(Time || InScopedCall.Phase == EPCGExecutionPhase::NotExecuted);

	switch (InScopedCall.Phase)
	{
	case EPCGExecutionPhase::NotExecuted:
		TimesPtr->Emplace();
		break;
	case EPCGExecutionPhase::PrepareData:
		Time->PrepareDataTime = ThisFrameTime;
		break;
	case EPCGExecutionPhase::Execute:
		Time->ExecutionTime += ThisFrameTime;
		Time->ExecutionFrameCount++;

		Time->MaxExecutionFrameTime = FMath::Max(Time->MaxExecutionFrameTime, ThisFrameTime);
		Time->MinExecutionFrameTime = FMath::Min(Time->MinExecutionFrameTime, ThisFrameTime);
		break;
	case EPCGExecutionPhase::PostExecute:
		Time->PostExecuteTime = ThisFrameTime;
		break;
	}

	CapturedMessagesPtr->Append(InScopedCall.CapturedMessages);
}

PCGUtils::FScopedCall::FScopedCall(const IPCGElement& InOwner, FPCGContext* InContext)
	: Owner(InOwner)
	, Context(InContext)
	, Phase(InContext->CurrentPhase)
	, ThreadID(FPlatformTLS::GetCurrentThreadId())
{
	StartTime = FPlatformTime::Seconds();

	GLog->AddOutputDevice(this);
}

PCGUtils::FScopedCall::~FScopedCall()
{
	GLog->RemoveOutputDevice(this);
	if (Context && Context->SourceComponent.IsValid())
	{
		Context->SourceComponent->ExtraCapture.Update(*this);
	}
}

void PCGUtils::FScopedCall::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category)
{
	// TODO: this thread id check will also filter out messages spawned from threads spawned inside of nodes. To improve that,
	// perhaps set at TLS bit on things from here and inside of PCGAsync spawned jobs. If this was done, CapturedMessages below also will
	// need protection
	if (Verbosity > ELogVerbosity::Warning || FPlatformTLS::GetCurrentThreadId() != ThreadID)
	{
		// ignore
		return;
	}

	// this is a dumb counter just so messages can be sorted in a similar order as when they were logged
	static volatile int32 MessageCounter = 0;

	CapturedMessages.Add(PCGUtils::FCapturedMessage{ MessageCounter++, Category, V, Verbosity });
}

#endif // WITH_EDITOR