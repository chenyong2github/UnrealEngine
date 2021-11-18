// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Animation/CurveSequence.h"
#include "Framework/Commands/Commands.h"

class FUICommandList;

class FDerivedDataStatusBarMenuCommands : public TCommands<FDerivedDataStatusBarMenuCommands>
{
public:

	FDerivedDataStatusBarMenuCommands();

	virtual void RegisterCommands() override;

private:


	static void ChangeSettings_Clicked();
	static void ViewCacheStatistics_Clicked();
	static void ViewResourceUsage_Clicked();
	static void ViewVirtualAssetsStatistics_Clicked();


public:

	TSharedPtr< FUICommandInfo > ChangeSettings;
	TSharedPtr< FUICommandInfo > ViewResourceUsage;
	TSharedPtr< FUICommandInfo > ViewCacheStatistics;
	TSharedPtr< FUICommandInfo > ViewVirtualAssetsStatistics;

	static TSharedRef<FUICommandList> ActionList;
};


class SDerivedDataStatusBarWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataStatusBarWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	FText						GetTitleToolTipText() const;
	FText						GetRemoteCacheToolTipText() const;
	FText						GetTitleText() const;
	const FSlateBrush*			GetRemoteCacheStateBackgroundIcon() const;
	const FSlateBrush*			GetRemoteCacheStateBadgeIcon() const;
	TSharedRef<SWidget>			CreateStatusBarMenu();
	EActiveTimerReturnType		UpdateBusyIndicator(double InCurrentTime, float InDeltaTime);
	EActiveTimerReturnType		UpdateWarnings(double InCurrentTime, float InDeltaTime);

	double ElapsedDownloadTime = 0;
	double ElapsedUploadTime = 0;
	double ElapsedBusyTime = 0;

	bool bBusy = false;

	TSharedPtr<SNotificationItem> NotificationItem;
};
