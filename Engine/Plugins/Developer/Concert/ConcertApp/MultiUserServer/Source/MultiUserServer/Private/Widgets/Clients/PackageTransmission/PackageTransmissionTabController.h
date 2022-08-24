// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Clients/Logging/LogScrollingDelegates.h"

class FEndpointToUserNameCache;
class FSpawnTabArgs;
class FTabManager;
class IConcertSyncServer;
class SDockTab;

namespace UE::MultiUserServer
{
	class FPackageTransmissionEntryTokenizer;
	class FPackageTransmissionModel;

	/** Manages a tab that displays a package transmission model. The model */
	class FPackageTransmissionTabController
	{
	public:
		
		FPackageTransmissionTabController(
			FName TabId,
			TSharedRef<FTabManager> OwningTabManager,
			TSharedRef<FPackageTransmissionModel> TransmissionModel,
			TSharedRef<FEndpointToUserNameCache> EndpointToUserNameCache,
			FCanScrollToLog CanScrollToLogDelegate,
			FScrollToLog ScrollToLogDelegate
			);
		~FPackageTransmissionTabController();

	private:
		
		FName TabId;
		TSharedRef<FTabManager> OwningTabManager;
		TSharedRef<FPackageTransmissionModel> TransmissionModel;
		TSharedRef<FEndpointToUserNameCache> EndpointToUserNameCache;
		
		FCanScrollToLog CanScrollToLogDelegate;
		FScrollToLog ScrollToLogDelegate;
		
		TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer;
		TSharedPtr<SDockTab> DockTab;

		TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& SpawnTabArgs);
	};
}

