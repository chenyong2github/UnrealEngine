// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithRuntimeBlueprintLibrary.h"

#include "DirectLink/DatasmithSceneReceiver.h"
#include "DirectLink/Network/DirectLinkEndpoint.h"
#include "DirectLink/Network/DirectLinkISceneProvider.h"

#include "Misc/ScopeRWLock.h"
#include "Tickable.h"

#include <atomic>

class ADatasmithRuntimeActor;

namespace DatasmithRuntime
{
	class FDestinationProxy : public DirectLink::ISceneProvider, public TSharedFromThis<FDestinationProxy>
	{
	public:
		FDestinationProxy(FDatasmithSceneReceiver::ISceneChangeListener* InChangeListener);

		virtual bool CanOpenNewConnection(const FSourceInformation& SourceInfo) override
		{
			return true/*ConnectedSource != SourceInfo.Id*/;
		}

		virtual TSharedPtr<DirectLink::ISceneReceiver> GetSceneReceiver(const FSourceInformation& SourceInfo) override
		{
			// DirectLink server has received messages. Start receiving on actor's side
			if (ChangeListener /* && ConnectedSource == SourceInfo.Id*/)
			{
				//ChangeListener->StartReceivingDelta();
				return SceneReceiver;
			}

			return nullptr;
		}

		bool RegisterDestination(const TCHAR* StreamName);

		void UnregisterDestination();

		bool CanConnect() { return Destination.IsValid(); }

		bool IsConnected() { return Destination.IsValid() && ConnectedSource.IsValid(); }

		bool OpenConnection(uint32 SourceHash);

		bool OpenConnection(const DirectLink::FSourceHandle& SourceId);

		void CloseConnection();

		FString GetSourceName();

		TSharedPtr<IDatasmithScene> GetScene()
		{
			return SceneReceiver.IsValid() ? SceneReceiver->GetScene() : TSharedPtr<IDatasmithScene>();
		}

		const DirectLink::FDestinationHandle& GetDestinationHandle() const { return Destination; }
		DirectLink::FDestinationHandle& GetDestinationHandle() { return Destination; }

		const DirectLink::FSourceHandle& GetConnectedSourceHandle() const { return ConnectedSource; }

		void ResetConnection()
		{
			ConnectedSource = DirectLink::FSourceHandle();
			SceneReceiver.Reset();
		}

	private:
		FDatasmithSceneReceiver::ISceneChangeListener* ChangeListener;

		TSharedPtr<FDatasmithSceneReceiver> SceneReceiver;

		DirectLink::FDestinationHandle Destination;
		DirectLink::FSourceHandle ConnectedSource;

		class FDirectLinkProxyImpl& DirectLinkProxy;
	};
}
