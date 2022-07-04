// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookTypes.h"

#include "Cooker/CookPackageData.h"
#include "HAL/PlatformTime.h"
#include "Math/NumericLimits.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace UE::Cook
{

const TCHAR* LexToString(EReleaseSaveReason Reason)
{
	switch (Reason)
	{
	case EReleaseSaveReason::Completed: return TEXT("Completed");
	case EReleaseSaveReason::DoneForNow: return TEXT("DoneForNow");
	case EReleaseSaveReason::Demoted: return TEXT("Demoted");
	case EReleaseSaveReason::AbortSave: return TEXT("AbortSave");
	case EReleaseSaveReason::RecreateObjectCache: return TEXT("RecreateObjectCache");
	default: return TEXT("Invalid");
	}
}

FCookerTimer::FCookerTimer(float InTimeSlice)
	: StartTime(FPlatformTime::Seconds()), TimeSlice(InTimeSlice)
{
}

FCookerTimer::FCookerTimer(EForever)
	: FCookerTimer(MAX_flt)
{
}

FCookerTimer::FCookerTimer(ENoWait)
	: FCookerTimer(0.0f)
{
}

double FCookerTimer::GetTimeTillNow() const
{
	return FPlatformTime::Seconds() - StartTime;
}

double FCookerTimer::GetEndTimeSeconds() const
{
	return FMath::Min(StartTime + TimeSlice,  MAX_flt);
}

bool FCookerTimer::IsTimeUp() const
{
	return IsTimeUp(FPlatformTime::Seconds());
}

bool FCookerTimer::IsTimeUp(double CurrentTimeSeconds) const
{
	return CurrentTimeSeconds - StartTime > TimeSlice;
}

double FCookerTimer::GetTimeRemain() const
{
	return TimeSlice - (FPlatformTime::Seconds() - StartTime);
}

static uint32 SchedulerThreadTlsSlot = 0;
void InitializeTls()
{
	if (SchedulerThreadTlsSlot == 0)
	{
		SchedulerThreadTlsSlot = FPlatformTLS::AllocTlsSlot();
		SetIsSchedulerThread(true);
	}
}

bool IsSchedulerThread()
{
	return FPlatformTLS::GetTlsValue(SchedulerThreadTlsSlot) != 0;
}

void SetIsSchedulerThread(bool bValue)
{
	FPlatformTLS::SetTlsValue(SchedulerThreadTlsSlot, bValue ? (void*)0x1 : (void*)0x0);
}

FCookSavePackageContext::FCookSavePackageContext(const ITargetPlatform* InTargetPlatform,
	ICookedPackageWriter* InPackageWriter, FStringView InWriterDebugName)
	: SaveContext(InTargetPlatform, InPackageWriter)
	, WriterDebugName(InWriterDebugName)
	, PackageWriter(InPackageWriter)
{
	PackageWriterCapabilities = InPackageWriter->GetCookCapabilities();
}

FCookSavePackageContext::~FCookSavePackageContext()
{
	// SaveContext destructor deletes the PackageWriter, so if we passed our writer into SaveContext, we do not delete it
	if (!SaveContext.PackageWriter)
	{
		delete PackageWriter;
	}
}

FBuildDefinitions::FBuildDefinitions()
{
	bTestPendingBuilds = FParse::Param(FCommandLine::Get(), TEXT("CookTestPendingBuilds"));
}

FBuildDefinitions::~FBuildDefinitions()
{
	Cancel();
}

void FBuildDefinitions::AddBuildDefinitionList(FName PackageName, const ITargetPlatform* TargetPlatform, TConstArrayView<UE::DerivedData::FBuildDefinition> BuildDefinitionList)
{
	using namespace UE::DerivedData;

	// TODO_BuildDefinitionList: Trigger the builds
	if (!bTestPendingBuilds)
	{
		return;
	}

	FPendingBuildData& BuildData = PendingBuilds.FindOrAdd(PackageName);
	BuildData.bTryRemoved = false; // overwrite any previous value
}

bool FBuildDefinitions::TryRemovePendingBuilds(FName PackageName)
{
	FPendingBuildData* BuildData = PendingBuilds.Find(PackageName);
	if (BuildData)
	{
		if (!bTestPendingBuilds || BuildData->bTryRemoved)
		{
			PendingBuilds.Remove(PackageName);
			return true;
		}
		else
		{
			BuildData->bTryRemoved = true;
			return false;
		}
	}

	return true;
}

void FBuildDefinitions::Wait()
{
	PendingBuilds.Empty();
}

void FBuildDefinitions::Cancel()
{
	PendingBuilds.Empty();
}

bool IsCookIgnoreTimeouts()
{
	static bool bIsIgnoreCookTimeouts = FParse::Param(FCommandLine::Get(), TEXT("CookIgnoreTimeouts"));
	return bIsIgnoreCookTimeouts;
}

}
