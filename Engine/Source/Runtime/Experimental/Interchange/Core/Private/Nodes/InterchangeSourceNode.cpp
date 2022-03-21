// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeSourceNode.h"

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

UInterchangeSourceNode::UInterchangeSourceNode()
{
}

void UInterchangeSourceNode::InitializeSourceNode(const FString& UniqueID, const FString& DisplayLabel)
{
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::TranslatedAsset);
}

FString UInterchangeSourceNode::GetTypeName() const
{
	const FString TypeName = TEXT("SourceNode");
	return TypeName;
}

bool UInterchangeSourceNode::GetCustomSourceFrameRateNumerator(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SourceFrameRateNumerator, int32);
}

bool UInterchangeSourceNode::SetCustomSourceFrameRateNumerator(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SourceFrameRateNumerator, int32);
}

bool UInterchangeSourceNode::GetCustomSourceFrameRateDenominator(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SourceFrameRateDenominator, int32);
}

bool UInterchangeSourceNode::SetCustomSourceFrameRateDenominator(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SourceFrameRateDenominator, int32);
}

bool UInterchangeSourceNode::GetCustomSourceTimelineStart(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SourceTimelineStart, double);
}

bool UInterchangeSourceNode::SetCustomSourceTimelineStart(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SourceTimelineStart, double);
}

bool UInterchangeSourceNode::GetCustomSourceTimelineEnd(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SourceTimelineEnd, double);
}

bool UInterchangeSourceNode::SetCustomSourceTimelineEnd(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SourceTimelineEnd, double);
}

bool UInterchangeSourceNode::GetCustomAnimatedTimeStart(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(AnimatedTimeStart, double);
}

bool UInterchangeSourceNode::SetCustomAnimatedTimeStart(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AnimatedTimeStart, double);
}

bool UInterchangeSourceNode::GetCustomAnimatedTimeEnd(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(AnimatedTimeEnd, double);
}

bool UInterchangeSourceNode::SetCustomAnimatedTimeEnd(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AnimatedTimeEnd, double);
}