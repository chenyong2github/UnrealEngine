// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "VirtualizationManager.h"

class SVirtualAssetsStatisticsDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVirtualAssetsStatisticsDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	SVirtualAssetsStatisticsDialog();
	virtual ~SVirtualAssetsStatisticsDialog();

private:

	TSharedRef<SWidget> GetGridPanel();

	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	FText GetNotificationText() const;

	void OnNotificationEvent(UE::Virtualization::IVirtualizationSystem::ENotification Notification, const FIoHash& PayloadId);

	SVerticalBox::FSlot* GridSlot = nullptr;

	FCriticalSection NotificationCS;

	TSharedPtr<SNotificationItem> PullRequestNotificationItem;

	TSharedPtr<class SScrollBox> ScrollBox;

	bool	IsPulling = false;
	uint32	NumPullRequests = 0;
	float	PullNotificationTimer = 0.0f;
};