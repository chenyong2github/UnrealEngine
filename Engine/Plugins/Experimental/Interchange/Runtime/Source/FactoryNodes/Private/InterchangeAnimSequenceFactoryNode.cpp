// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeAnimSequenceFactoryNode.h"
#include "Animation/AnimSequence.h"

UInterchangeAnimSequenceFactoryNode::UInterchangeAnimSequenceFactoryNode()
{
}

void UInterchangeAnimSequenceFactoryNode::InitializeAnimSequenceNode(const FString& UniqueID, const FString& DisplayLabel)
{
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);
}

FString UInterchangeAnimSequenceFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("AnimSequenceNode");
	return TypeName;
}

FString UInterchangeAnimSequenceFactoryNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	if (NodeAttributeKey == Macro_CustomSkeletonFactoryNodeUidKey)
	{
		KeyDisplayName = TEXT("Skeleton Uid");
	}
	else if (NodeAttributeKey == Macro_CustomSkeletonSoftObjectPathKey)
	{
		KeyDisplayName = TEXT("Specified Existing Skeleton");
	}
	else
	{
		KeyDisplayName = Super::GetKeyDisplayName(NodeAttributeKey);
	}
	return KeyDisplayName;
}

UClass* UInterchangeAnimSequenceFactoryNode::GetObjectClass() const
{
	return UAnimSequence::StaticClass();
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomSkeletonFactoryNodeUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonFactoryNodeUid, FString);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomSkeletonFactoryNodeUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonFactoryNodeUid, FString);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracks(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracks, bool);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracks(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracks, bool);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracksSampleRate(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracksSampleRate, double);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracksSampleRate(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracksSampleRate, double);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracksRangeStart(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracksRangeStart, double);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracksRangeStart(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracksRangeStart, double);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomImportBoneTracksRangeStop(double& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportBoneTracksRangeStop, double);
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomImportBoneTracksRangeStop(const double& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportBoneTracksRangeStop, double);
}

bool UInterchangeAnimSequenceFactoryNode::GetCustomSkeletonSoftObjectPath(FSoftObjectPath& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonSoftObjectPath, FSoftObjectPath)
}

bool UInterchangeAnimSequenceFactoryNode::SetCustomSkeletonSoftObjectPath(const FSoftObjectPath& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonSoftObjectPath, FSoftObjectPath)
}
