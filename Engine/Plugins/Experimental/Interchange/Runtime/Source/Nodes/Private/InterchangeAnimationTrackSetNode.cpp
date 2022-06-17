// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeAnimationTrackSetNode.h"

// UInterchangeAnimationTrackSetNode

UInterchangeAnimationTrackSetNode::UInterchangeAnimationTrackSetNode()
{
	CustomAnimationTrackUids.Initialize(Attributes, TEXT("AnimationTrackUids"));
}

int32 UInterchangeAnimationTrackSetNode::GetCustomAnimationTrackUidCount() const
{
	return CustomAnimationTrackUids.GetCount();
}

void UInterchangeAnimationTrackSetNode::GetCustomAnimationTrackUids(TArray<FString>& OutAnimationTrackUids) const
{
	CustomAnimationTrackUids.GetItems(OutAnimationTrackUids);
}

void UInterchangeAnimationTrackSetNode::GetCustomAnimationTrackUid(const int32 Index, FString& OutAnimationTrackUid) const
{
	CustomAnimationTrackUids.GetItem(Index, OutAnimationTrackUid);
}

bool UInterchangeAnimationTrackSetNode::AddCustomAnimationTrackUid(const FString& AnimationTrackUid)
{
	return CustomAnimationTrackUids.AddItem(AnimationTrackUid);
}

bool UInterchangeAnimationTrackSetNode::RemoveCustomAnimationTrackUid(const FString& AnimationTrackUid)
{
	return CustomAnimationTrackUids.RemoveItem(AnimationTrackUid);
}

bool UInterchangeAnimationTrackSetNode::GetCustomFrameRate(float& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(FrameRate, float);
}

bool UInterchangeAnimationTrackSetNode::SetCustomFrameRate(const float& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(FrameRate, float);
}

// UInterchangeAnimationTrackBaseNode

bool UInterchangeAnimationTrackBaseNode::GetCustomCompletionMode(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(CompletionMode, int32);
	AttributeValue = FMath::Clamp(AttributeValue, (int32)EInterchangeAimationCompletionMode::KeepState, (int32)EInterchangeAimationCompletionMode::ProjectDefault);
}

bool UInterchangeAnimationTrackBaseNode::SetCustomCompletionMode(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CompletionMode, int32);
}

// UInterchangeAnimationTrackNode

bool UInterchangeAnimationTrackNode::GetCustomActorDependencyUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ActorDependency, FString);
}

bool UInterchangeAnimationTrackNode::SetCustomActorDependencyUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ActorDependency, FString);
}

bool UInterchangeAnimationTrackNode::GetCustomAnimationPayloadKey(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(AnimationPayload, FString);
}

bool UInterchangeAnimationTrackNode::SetCustomAnimationPayloadKey(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AnimationPayload, FString);
}

bool UInterchangeAnimationTrackNode::GetCustomFrameCount(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(FrameCount, int32);
}

bool UInterchangeAnimationTrackNode::SetCustomFrameCount(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(FrameCount, int32);
}

bool UInterchangeAnimationTrackNode::GetCustomTargetedProperty(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TargetedProperty, int32);
}

bool UInterchangeAnimationTrackNode::SetCustomTargetedProperty(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TargetedProperty, int32);
}

// UInterchangeAnimationTrackSetInstanceNode

bool UInterchangeAnimationTrackSetInstanceNode::SetCustomTimeScale(const float& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TimeScale, float);
}

bool UInterchangeAnimationTrackSetInstanceNode::GetCustomTimeScale(float& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TimeScale, float);
}

bool UInterchangeAnimationTrackSetInstanceNode::SetCustomDuration(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(Duration, int32);
}

bool UInterchangeAnimationTrackSetInstanceNode::GetCustomDuration(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(Duration, int32);
}

bool UInterchangeAnimationTrackSetInstanceNode::SetCustomStartFrame(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(StartFrame, int32);
}

bool UInterchangeAnimationTrackSetInstanceNode::GetCustomStartFrame(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(StartFrame, int32);
}

bool UInterchangeAnimationTrackSetInstanceNode::SetCustomTrackSetDependencyUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TrackSetDependencyUid, FString);
}

bool UInterchangeAnimationTrackSetInstanceNode::GetCustomTrackSetDependencyUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TrackSetDependencyUid, FString);
}

// UInterchangeTransformAnimationTrackNode

bool UInterchangeTransformAnimationTrackNode::SetCustomUsedChannels(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(UsedChannels, int32);
}

bool UInterchangeTransformAnimationTrackNode::GetCustomUsedChannels(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(UsedChannels, int32);
}
