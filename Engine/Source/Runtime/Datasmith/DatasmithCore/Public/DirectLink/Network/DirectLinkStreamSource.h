// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/Network/DirectLinkStream.h"


namespace DirectLink
{
class FSceneSnapshot;
class FStreamSender;
class ISceneGraphNode;


/**
 * Define a content source.
 * A source is linked to N Destinations through Streams, and uses Senders to write on them.
 */
class FStreamSource : public FStreamEndpoint
{
public:
	FStreamSource(const FString& Name, EVisibility Visibility)
		: FStreamEndpoint(Name, Visibility)
	{}

	// Defines the content, which is a root node and its referenced tree.
	void SetRoot(ISceneGraphNode* InRoot);

	// Snapshot the current state of the scene
	void Snapshot();

	// Link a stream to this source (via a sender)
	void LinkSender(const TSharedPtr<FStreamSender>& Sender);

private:
	ISceneGraphNode* Root = nullptr;
	FRWLock SendersLock;
	TArray<TSharedPtr<FStreamSender>> Senders;
	FRWLock CurrentSnapshotLock;
	TSharedPtr<FSceneSnapshot> CurrentSnapshot;
};


} // namespace DirectLink
