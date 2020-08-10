// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Network/DirectLinkStream.h"

#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/Network/DirectLinkScenePipe.h"
#include "DirectLink/SceneIndexBuilder.h"

namespace DirectLink
{

TSharedPtr<FLocalSceneIndex> FStreamSource::Snapshot()
{
	FSceneIndexBuilder Builder;
	Builder.InitFromRootElement(Root);

	TSharedPtr<FLocalSceneIndex> CurrentIndex;
	{
// 		FRWScopeLock _(CurrentIndexLock, SLT_Write);
		CurrentIndex = MakeShared<FLocalSceneIndex>(MoveTemp(Builder.GetIndex()));
	}

	return CurrentIndex;
}


void FStreamSender::SetSceneIndex(TSharedPtr<FLocalSceneIndex> SceneIndex)
{
// 	lock index
	Index = SceneIndex;

// 	async transmit todo
	if (Index)
	{
		DoDiff(*Index, RemoteScene);
	}
}

void FStreamSender::SetRemoteInfo(const FMessageAddress& Dest, FStreamPort ReceiverStreamPort)
{
	check(PipeToNetwork);
	PipeToNetwork->SetDestinationInfo(Dest, ReceiverStreamPort);
}

FStreamSender::FStreamSender(TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint)
	: PipeToNetwork(MakeShared<FScenePipeToNetwork>(MessageEndpoint, FSceneIdentifier{}))
{
	RemoteScene.SetDeltaConsumer(PipeToNetwork);
}

void FStreamReceiver::SetConsumer(TSharedPtr<IDeltaConsumer> param1)
{
	PipeFromNetwork = MakeShared<FScenePipeFromNetwork>(param1, FSceneIdentifier{});
}

void FStreamReceiver::HandleDeltaMessage(const FDirectLinkMsg_DeltaMessage& Message)
{
	check(PipeFromNetwork);
	PipeFromNetwork->HandleDeltaMessage(Message);
}

} // namespace DirectLink

