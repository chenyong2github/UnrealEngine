// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/Network/DirectLinkScenePipe.h"
#include "DirectLink/SceneSnapshot.h"

#include "CoreTypes.h"
#include "Containers/SortedMap.h"


namespace DirectLink
{
class FRemoteSceneView;
class FHaveListReceiver;

/**
 * This is used to sync a Stream over MessagBus. See also: FStreamReceiver
 *
 * It keeps an Hash table of what the remote receiver already have, and diff with that.
 * There is no handling of bad connection in this class. We accept arbitrary delays
 * that can arise with remote slow operation (file load, breakpoint...).
 * Some requests messages can be sent multiple times though, but with a unique
 * 'SyncCycle' value so that the receiver is able to ignore duplicated requests.
 */
class FStreamSender
{
public:
	FStreamSender(TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> ThisEndpoint, const FMessageAddress& DestinationAddress, FStreamPort ReceiverStreamPort);
	~FStreamSender();
	void SetSceneSnapshot(TSharedPtr<FSceneSnapshot> SceneSnapshot);
	void Tick();
	void HandleHaveListMessage(const FDirectLinkMsg_HaveListMessage& Message); // update RemoteView

private:
	enum class EStep
	{
		Idle,
		SetupScene,
		ReceiveHaveList,
		SendDelta,
		Synced,
	};

	EStep NextStep = EStep::Idle;
	int32 SyncCycle = 0;

	FScenePipeToNetwork PipeToNetwork;
	TUniquePtr<FHaveListReceiver> HaveListReceiver;
	double LastHaveListMessage_s = 0;

	TSharedPtr<FSceneSnapshot> Snapshot;
	FCriticalSection NextSnapshotLock;
	TSharedPtr<FSceneSnapshot> NextSnapshot;

	TUniquePtr<FRemoteSceneView> RemoteScene;
};


} // namespace DirectLink
