// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "TickableEditorObject.h"
#include "MeshCardRepresentation.h"
#include "Templates/UniquePtr.h"
#include "GlobalEditorNotification.h"

/** Notification class for asynchronous card representation building. */
class FCardRepresentationBuildNotificationImpl : public FGlobalEditorProgressNotification
{

public:
	FCardRepresentationBuildNotificationImpl()
		: FGlobalEditorProgressNotification(NSLOCTEXT("CardRepresentationBuild", "CardRepresentationBuildInProgress", "Building Cards"))
	{
	}

private:
	virtual bool AllowedToStartNotification() const override
	{
		return GCardRepresentationAsyncQueue ? GCardRepresentationAsyncQueue->GetNumOutstandingTasks() >= 10 : false;
	}

	virtual int32 UpdateProgress()
	{
		const int32 RemainingJobs = GCardRepresentationAsyncQueue ? GCardRepresentationAsyncQueue->GetNumOutstandingTasks() : 0;
		if (RemainingJobs > 0)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("BuildTasks"), FText::AsNumber(GCardRepresentationAsyncQueue->GetNumOutstandingTasks()));
			UpdateProgressMessage(FText::Format(NSLOCTEXT("CardRepresentationBuild", "CardRepresentationBuildInProgressFormat", "Building Cards ({BuildTasks})"), Args));

		}

		return RemainingJobs;
	}
};

/** Global notification object. */
TUniquePtr<FCardRepresentationBuildNotificationImpl> GCardRepresentationBuildNotification;

void SetupCardRepresentationBuildNotification()
{
	// Create explicitly to avoid relying on static initialization order
	GCardRepresentationBuildNotification = MakeUnique<FCardRepresentationBuildNotificationImpl>();
}

void TearDownCardRepresentationBuildNotification()
{
	GCardRepresentationBuildNotification = nullptr;
}
