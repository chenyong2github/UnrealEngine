// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "TickableEditorObject.h"
#include "DistanceFieldAtlas.h"
#include "Templates/UniquePtr.h"
#include "GlobalEditorNotification.h"

/** Notification class for asynchronous distance field building. */
class FDistanceFieldBuildNotificationImpl : public FGlobalEditorProgressNotification
{
public:

	FDistanceFieldBuildNotificationImpl()
		: FGlobalEditorProgressNotification(NSLOCTEXT("DistanceFieldBuild", "DistanceFieldBuildInProgress", "Building Mesh Distance Fields"))
	{
	}

protected:
	/** FGlobalEditorProgressNotification interface */
	virtual int32 UpdateProgress()
	{
		const int32 RemainingJobs = GDistanceFieldAsyncQueue ? GDistanceFieldAsyncQueue->GetNumOutstandingTasks() : 0;
		if (RemainingJobs > 0)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("BuildTasks"), FText::AsNumber(RemainingJobs));
			UpdateProgressMessage(FText::Format(NSLOCTEXT("DistanceFieldBuild", "DistanceFieldBuildInProgressFormat", "Building Mesh Distance Fields ({BuildTasks})"), Args));
		}

		return RemainingJobs;
	}
};

/** Global notification object. */
TUniquePtr<FDistanceFieldBuildNotificationImpl> GDistanceFieldBuildNotification;

void SetupDistanceFieldBuildNotification()
{
	// Create explicitly to avoid relying on static initialization order
	GDistanceFieldBuildNotification = MakeUnique<FDistanceFieldBuildNotificationImpl>();
}

void TearDownDistanceFieldBuildNotification()
{
	GDistanceFieldBuildNotification = nullptr;
}
