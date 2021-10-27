// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookTypes.h"

#include "Cooker/CookPackageData.h"
#include "HAL/PlatformTime.h"
#include "Math/NumericLimits.h"
#include "Misc/CommandLine.h"

namespace UE::Cook
{

FCookerTimer::FCookerTimer(const float& InTimeSlice, bool bInIsRealtimeMode, int InMaxNumPackagesToSave)
	: bIsRealtimeMode(bInIsRealtimeMode), StartTime(FPlatformTime::Seconds()), TimeSlice(InTimeSlice)
	, MaxNumPackagesToSave(InMaxNumPackagesToSave), NumPackagesSaved(0)
{
}

FCookerTimer::FCookerTimer(EForever)
	:bIsRealtimeMode(false), StartTime(FPlatformTime::Seconds()), TimeSlice(ForeverTimeSlice), MaxNumPackagesToSave(TNumericLimits<int>::Max()), NumPackagesSaved(0)
{
}

FCookerTimer::FCookerTimer(ENoWait)
	:bIsRealtimeMode(false), StartTime(FPlatformTime::Seconds()), TimeSlice(ZeroTimeSlice), MaxNumPackagesToSave(TNumericLimits<int>::Max()), NumPackagesSaved(0)
{
}

double FCookerTimer::GetTimeTillNow() const
{
	return FPlatformTime::Seconds() - StartTime;
}
	
bool FCookerTimer::IsTimeUp() const
{
	if (bIsRealtimeMode)
	{
		if ((FPlatformTime::Seconds() - StartTime) > TimeSlice)
		{
			return true;
		}
	}
	if (NumPackagesSaved >= MaxNumPackagesToSave)
	{
		return true;
	}
	return false;
}

void FCookerTimer::SavedPackage()
{
	++NumPackagesSaved;
}

double FCookerTimer::GetTimeRemain() const
{
	return TimeSlice - (FPlatformTime::Seconds() - StartTime);
}

float FCookerTimer::ZeroTimeSlice = 0.0f;
float FCookerTimer::ForeverTimeSlice = TNumericLimits<float>::Max();

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

}
