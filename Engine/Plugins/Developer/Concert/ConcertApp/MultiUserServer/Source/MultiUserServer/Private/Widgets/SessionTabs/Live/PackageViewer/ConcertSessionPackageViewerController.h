// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FConcertSyncActivity;
struct FConcertClientInfo;
struct FConcertSyncPackageEventMetaData;
struct FConcertSessionActivity;
class IConcertSyncServer;
class IConcertServerSession;
class SConcertSessionPackageViewer;

/** Manages SConcertSessionPackageViewer for a server. */
class FConcertSessionPackageViewerController
{
public:

	FConcertSessionPackageViewerController(TSharedRef<IConcertServerSession> InspectedSession, TSharedRef<IConcertSyncServer> SyncServer);
	~FConcertSessionPackageViewerController();
	
	void ReloadActivities() const;
	TSharedRef<SConcertSessionPackageViewer> GetPackageViewer() const { return PackageViewer; }

private:
	
	TSharedRef<IConcertServerSession> InspectedSession;
	TSharedRef<IConcertSyncServer> SyncServer;
	
	/** The widget being managed */
	TSharedRef<SConcertSessionPackageViewer> PackageViewer;
	
	TSharedRef<SConcertSessionPackageViewer> MakePackageViewer() const;
	
	TOptional<FConcertClientInfo> GetClientInfo(FGuid ClientId) const;
	bool GetPackageEvent(const FConcertSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) const;
	TOptional<int64> GetSizeOfPackageEvent(const FConcertSessionActivity& Activity) const;
	void OnSessionProduced(const FConcertSyncActivity& ProducedActivity) const;
};
