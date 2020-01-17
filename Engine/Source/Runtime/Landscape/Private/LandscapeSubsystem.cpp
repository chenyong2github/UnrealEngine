// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/EngineBaseTypes.h"
#include "LandscapeInfoMap.h"
#include "LandscapeInfo.h"

DECLARE_CYCLE_STAT(TEXT("LandscapeSubsystem Tick"), STAT_LandscapeSubsystemTick, STATGROUP_Landscape);

ULandscapeSubsystem::ULandscapeSubsystem()
	: TickFunction(this), LandscapeInfoMap(nullptr)
{}

ULandscapeSubsystem::~ULandscapeSubsystem()
{

}

void ULandscapeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Register Tick 
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = true;
	TickFunction.bStartWithTickEnabled = true;
	TickFunction.TickGroup = TG_DuringPhysics;
	TickFunction.bAllowTickOnDedicatedServer = false;
	TickFunction.RegisterTickFunction(GetWorld()->PersistentLevel);
}

void ULandscapeSubsystem::Deinitialize()
{
	TickFunction.UnRegisterTickFunction();
	LandscapeInfoMap = nullptr;
}

void ULandscapeSubsystem::Tick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeSubsystemTick);
	TRACE_CPUPROFILER_EVENT_SCOPE(ULandscapeSubsystem::Tick);
	UWorld* World = GetWorld();
	if (!LandscapeInfoMap)
	{
		LandscapeInfoMap = &ULandscapeInfoMap::GetLandscapeInfoMap(World);
	}
		
	for (const auto& Pair : LandscapeInfoMap->Map)
	{
		Pair.Value->Tick(World, DeltaTime);
	}
}

void FLandscapeSubsystemTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	Subsystem->Tick(DeltaTime, TickType, CurrentThread, MyCompletionGraphEvent);
}

FString FLandscapeSubsystemTickFunction::DiagnosticMessage()
{
	static const FString Message(TEXT("FLandscapeSubsystemTickFunction"));
	return Message;
}

FName FLandscapeSubsystemTickFunction::DiagnosticContext(bool bDetailed)
{
	static const FName Context(TEXT("FLandscapeSubsystemTickFunction"));
	return Context;
}