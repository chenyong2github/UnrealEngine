// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncAnimCompressionUI.h"
#include "GlobalEditorNotification.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Animation/AnimCompressionDerivedDataPublic.h"
#include "Animation/AnimSequence.h"

/** Notification class for asynchronous shader compiling. */
class FAnimCompressionNotificationImpl : public FGlobalEditorNotification
{
protected:
	/** FGlobalEditorNotification interface */
	virtual bool ShouldShowNotification(const bool bIsNotificationAlreadyActive) const override;
	virtual void SetNotificationText(const TSharedPtr<SNotificationItem>& InNotificationItem) const override;
};

/** Global notification object. */
FAnimCompressionNotificationImpl GAnimCompressionNotification;

bool FAnimCompressionNotificationImpl::ShouldShowNotification(const bool bIsNotificationAlreadyActive) const
{
	const uint32 RemainingJobsThreshold = 0;
	const uint32 ActiveJobs = GAsyncCompressedAnimationsTracker ? GAsyncCompressedAnimationsTracker->GetNumRemainingJobs() : 0;

	return ActiveJobs > RemainingJobsThreshold;
}

void FAnimCompressionNotificationImpl::SetNotificationText(const TSharedPtr<SNotificationItem>& InNotificationItem) const
{
	if(GAsyncCompressedAnimationsTracker)
	{
		const int32 RemainingJobs = GAsyncCompressedAnimationsTracker->GetNumRemainingJobs();
		if (RemainingJobs > 0)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("AnimsToCompress"), FText::AsNumber(RemainingJobs));
			const FText ProgressMessage = FText::Format(NSLOCTEXT("AsyncAnimCompression", "AnimCompressionInProgressFormat", "Compressing Animations ({AnimsToCompress})"), Args);

			InNotificationItem->SetText(ProgressMessage);
		}
	}
}