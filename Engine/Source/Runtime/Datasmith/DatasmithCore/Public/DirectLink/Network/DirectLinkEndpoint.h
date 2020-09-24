// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/Network/DirectLinkStreamDescription.h"
#include "DirectLink/Network/DirectLinkStreamDestination.h"
#include "DirectLink/Network/DirectLinkStreamSource.h"

#include "Async/Future.h"
#include <atomic>

struct FDirectLinkMsg_EndpointLifecycle;
struct FDirectLinkMsg_EndpointState;
struct FDirectLinkMsg_QueryEndpointState;
struct FDirectLinkMsg_DeltaMessage;


namespace DirectLink
{
class ISceneGraphNode;

struct FRawInfo
{
	struct FEndpointInfo
	{
		FEndpointInfo() = default;
		FEndpointInfo(const FDirectLinkMsg_EndpointState& Msg);
		FString Name;
		TArray<FNamedId> Destinations;
		TArray<FNamedId> Sources;
		FString UserName;
		FString ExecutableName;
		FString ComputerName;
		bool bIsLocal = false;
		uint32 ProcessId = 0;
	};

	struct FDataPointInfo
	{
		FMessageAddress EndpointAddress;
		FString Name;
		bool bIsSource = false; // as opposed to a destination
		bool bIsOnThisEndpoint = false;
		bool bIsPublic = false; // if public, can be displayed as candidate for connection
	};

	struct FStreamInfo
	{
		FStreamPort StreamId = InvalidStreamPort;
		FGuid Source;
		FGuid Destination;
		bool bIsActive = false;
		FCommunicationStatus CommunicationStatus;
	};
	FMessageAddress ThisEndpointAddress;
	TMap<FMessageAddress, FEndpointInfo> EndpointsInfo;
	TMap<FGuid, FDataPointInfo> DataPointsInfo;
	TArray<FStreamInfo> StreamsInfo;
};


class IEndpointObserver
{
public:
	virtual ~IEndpointObserver() = default;

	virtual void OnStateChanged(const FRawInfo& RawInfo) {}
};


// niy, placeholder.
// could be a class with accessors and a ref on the endpoint to delegate
using FSourceHandle = FGuid;
using FDestinationHandle = FGuid;



class DATASMITHCORE_API FEndpoint
	: public FNoncopyable
{
public:
	enum class EOpenStreamResult
	{
		Opened,
		AlreadyOpened,
		SourceAndDestinationNotFound,
		RemoteEndpointNotFound,
		Unsuppported,
		CannotConnectToPrivate,
	};

public:
	FEndpoint(const FString& InName);
	void SetVerbose(bool bVerbose=true) { SharedState.bDebugLog = bVerbose; }
	~FEndpoint();

	/**
	 * Add a Source that host content (a scene snapshot) and is able to stream
	 * it to remote destinations.
	 * @param Name            Public, user facing name for this source.
	 * @param Visibility      Whether that Source is visible to remote endpoints
	 * @return A Handle required by other Source related methods
	 */
	FSourceHandle AddSource(const FString& Name, EVisibility Visibility);
	void RemoveSource(const FSourceHandle& Source);
	void SetSourceRoot(const FSourceHandle& Source, ISceneGraphNode* InRoot, bool bSnapshot);
	void SnapshotSource(const FSourceHandle& Source);

	FDestinationHandle AddDestination(const FString& Name, EVisibility Visibility, const TSharedPtr<ISceneProvider>& Provider);
	void RemoveDestination(const FDestinationHandle& Destination);

	FRawInfo GetRawInfoCopy() const;
	void AddEndpointObserver(IEndpointObserver* Observer);
	void RemoveEndpointObserver(IEndpointObserver* Observer);

	EOpenStreamResult OpenStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId);
	void CloseStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId);

private:
	// internal version: Require locked StreamsLock in Write mode.
	void CloseStreamInternal(FStreamDescription& Stream, const FRWScopeLock& _, bool bNotifyRemote=true);

	struct FSharedState
	{
		FSharedState(const FString& NiceName) : NiceName(NiceName) {}

		mutable FRWLock SourcesLock;
		TArray<TSharedPtr<FStreamSource>> Sources;
		std::atomic<bool> bDirtySources{false};

		mutable FRWLock DestinationsLock;
		TArray<TSharedPtr<FStreamDestination>> Destinations;
		std::atomic<bool> bDirtyDestinations{false};

		mutable FRWLock StreamsLock;
		FStreamPort StreamPortIdGenerator = InvalidStreamPort;
		TArray<FStreamDescription> Streams; // map streamportId -> stream ? array of N ports ?

		// cleared on inner thread loop start
		mutable FRWLock ObserversLock;
		TArray<IEndpointObserver*> Observers;

		mutable FRWLock RawInfoCopyLock;
		FRawInfo RawInfo;

		std::atomic<bool> bInnerThreadShouldRun{false};
		bool bDebugLog = false;
		const FString NiceName; // not locked (wrote once)
		TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

		FStreamDescription* GetStreamByLocalPort(FStreamPort LocalPort, const FRWScopeLock& _);
	};
	FSharedState SharedState;

private:
	class FInternalThreadState
	{
	public:
		FInternalThreadState(FEndpoint& Owner) : Owner(Owner), SharedState(Owner.SharedState) {}
		void Init(); // once, any thread
		void Run(); // once, blocking, inner thread only

	private:
		void Handle_DeltaMessage(const FDirectLinkMsg_DeltaMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_HaveListMessage(const FDirectLinkMsg_HaveListMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_EndpointLifecycle(const FDirectLinkMsg_EndpointLifecycle& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_QueryEndpointState(const FDirectLinkMsg_QueryEndpointState& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_EndpointState(const FDirectLinkMsg_EndpointState& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_OpenStreamRequest(const FDirectLinkMsg_OpenStreamRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_OpenStreamAnswer(const FDirectLinkMsg_OpenStreamAnswer& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
		void Handle_CloseStreamRequest(const FDirectLinkMsg_CloseStreamRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

		/** Check if a received message is sent by 'this' endpoint.
		 * Can be useful to skip handling of own messages. Makes sense in handlers of subscribed messages.
		 * @param MaybeRemoteAddress Address of the sender (see Context->GetSender())
		 * @returns whether given address is this address */
		bool IsMine(const FMessageAddress& MaybeRemoteAddress) const;

		/** Note on state replication:
		 * On local state edition (eg. when a source is added) the new state is broadcasted.
		 * On top of that, the state revision is broadcasted on heartbeats every few seconds.
		 * This allow other endpoint to detect when a replicated state is no longer valid, and query an update.
		 * This covers all failure case, and is lightweight as only the revision number is frequently broadcasted. */
		void ReplicateState(const FMessageAddress& RemoteEndpointAddress) const;
		void ReplicateState_Broadcast() const;

		FString ToString_dbg() const;

		void UpdateSourceDescription();
		void UpdateDestinationDescription();

		TUniquePtr<FStreamReceiver> MakeReceiver(FGuid SourceGuid, FGuid DestinationGuid, FMessageAddress RemoteAddress, FStreamPort RemotePort);
		TSharedPtr<FStreamSender> MakeSender(FGuid SourceGuid, FMessageAddress RemoteAddress, FStreamPort RemotePort);

		void RemoveEndpoint(const FMessageAddress& RemoteEndpointAddress);
		void MarkRemoteAsSeen(const FMessageAddress& RemoteEndpointAddress);
		void CleanupTimedOutEndpoint();

	private:
		FEndpoint& Owner;
		FSharedState& SharedState;

		TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;
		TMap<FMessageAddress, FDirectLinkMsg_EndpointState> RemoteEndpointDescriptions;
		FDirectLinkMsg_EndpointState ThisDescription;

		// state replication
		double Now_s = 0;
		double LastHeartbeatTime_s = 0;
		double LastEndpointCleanupTime_s = 0;
		mutable uint32 LastBroadcastedStateRevision = 0;
		TMap<FMessageAddress, double> RemoteLastSeenTime;
	};

	/** Inner thread allows async network communication, which avoids user thread to be locked on every sync. */
	FInternalThreadState Internal;
	FEvent* InnerThreadEvent;
	TFuture<void> InnerThreadResult; // allow to join() in the dtr
	uint32 InnerThreadId = 0;
};

} // namespace DirectLink

