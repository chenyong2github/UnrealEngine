// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/Network/DirectLinkISceneProvider.h"
#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/SceneIndex.h"

#include "CoreTypes.h"
#include "IMessageContext.h"
#include "MessageEndpoint.h" // #ue_directlink_cleanup -> cpp


namespace DirectLink
{

class FEndpoint;
class FScenePipeFromNetwork;
class FScenePipeToNetwork;
class ISceneGraphNode;



class FStreamEndpoint
{
public:
	FStreamEndpoint(const FString& Name, EVisibility Visibility)
		: Name(Name)
		, Id(FGuid::NewGuid())
		, Visibility(Visibility)
	{}
	const FString GetName() const { return Name; }
	const FGuid& GetId() const { return Id; }
	EVisibility GetVisibility() const { return Visibility; }

private:
	FString Name;
	FGuid Id;
	EVisibility Visibility;
};


class FStreamSource : public FStreamEndpoint
{
public:
	FStreamSource(const FString& Name, EVisibility Visibility)
		: FStreamEndpoint(Name, Visibility)
	{}

	TSharedPtr<FLocalSceneIndex> Snapshot();
	void SetRoot(ISceneGraphNode* InRoot) { Root = InRoot; }

private:
	ISceneGraphNode* Root = nullptr;
};



class FStreamDestination : public FStreamEndpoint
{
public:
	FStreamDestination(const FString& Name, EVisibility Visibility)
		: FStreamEndpoint(Name, Visibility)
	{}

	// #ue_directlink_cleanup rewrite ISceneProvider, then private member
	TSharedPtr<ISceneProvider> Provider;
};



class FStreamSender
{
public:
	// #ue_directlink_cleanup An FEndpoint is not needed here. Just the msgbus endpoint
	FStreamSender(TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint);

	void SetSceneIndex(TSharedPtr<FLocalSceneIndex> SceneIndex);
	void SetRemoteInfo(const FMessageAddress& Dest, FStreamPort ReceiverStreamPort);

private:
	TSharedPtr<FLocalSceneIndex> Index = nullptr;
	FRemoteScene RemoteScene;

	TSharedPtr<FScenePipeToNetwork> PipeToNetwork;
	// #ue_directlink_cleanup pipes classes and Stream classes could be merged
};



class DATASMITHCORE_API FStreamReceiver
{
public:
	// #ue_directlink_cleanup  -> ctr
	void SetConsumer(TSharedPtr<IDeltaConsumer> param1);
	void HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message);

private:
	TSharedPtr<FScenePipeFromNetwork> PipeFromNetwork;
};



struct FStreamDescription
{
	enum class EConnectionState
	{
		Uninitialized,
		RequestSent,
		Active,
		Closed,
	};

	bool bThisIsSource = false;

	FGuid SourcePoint;
	FGuid DestinationPoint;
	FStreamPort LocalStreamPort = 0; // works like an Id within that endpoint
	FMessageAddress RemoteAddress;
	FStreamPort RemoteStreamPort = 0;
	EConnectionState Status = EConnectionState::Uninitialized;
	double LastRemoteLifeSign = 0; // as returned by FPlatformTime::Seconds()
	 // #ue_directlink_streams implement old connection pruning
	TUniquePtr<FStreamReceiver> Receiver;
	TUniquePtr<FStreamSender> Sender;
};

} // namespace DirectLink
