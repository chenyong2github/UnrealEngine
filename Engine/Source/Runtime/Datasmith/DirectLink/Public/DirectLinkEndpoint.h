// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLinkMessages.h"

#include "Async/Future.h"
#include "IMessageContext.h"
#include <atomic>

class FMessageEndpoint;

namespace DirectLink
{
class ISceneGraphNode;

struct FRawInfo
{
	struct DIRECTLINK_API FEndpointInfo
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



class DIRECTLINK_API FEndpoint
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
	~FEndpoint();

	void SetVerbose(bool bVerbose=true);

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

	FDestinationHandle AddDestination(const FString& Name, EVisibility Visibility, const TSharedPtr<class IConnectionRequestHandler>& Provider);
	void RemoveDestination(const FDestinationHandle& Destination);

	FRawInfo GetRawInfoCopy() const;
	void AddEndpointObserver(IEndpointObserver* Observer);
	void RemoveEndpointObserver(IEndpointObserver* Observer);

	EOpenStreamResult OpenStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId);
	void CloseStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId);

private:
	TUniquePtr<class FSharedState> SharedStatePtr;
	class FSharedState& SharedState;

	TUniquePtr<class FInternalThreadState> InternalPtr;
	class FInternalThreadState& Internal;
};

} // namespace DirectLink

