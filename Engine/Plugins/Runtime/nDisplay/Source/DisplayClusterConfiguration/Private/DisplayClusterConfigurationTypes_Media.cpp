// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterConfigurationTypes_Media.h"


bool FDisplayClusterConfigurationMediaICVFX::IsMediaInputAssigned(const FString& NodeId) const
{
	return GetMediaSource(NodeId) != nullptr;
}

bool FDisplayClusterConfigurationMediaICVFX::IsMediaOutputAssigned(const FString& NodeId) const
{
	return GetMediaOutput(NodeId) != nullptr;
}

UMediaSource* FDisplayClusterConfigurationMediaICVFX::GetMediaSource(const FString& NodeId) const
{
	// Look up for a group that contains node ID specified
	for (const FDisplayClusterConfigurationMediaInputGroup& MediaInputGroup : MediaInputGroups)
	{
		const bool bFound = MediaInputGroup.ClusterNodes.ItemNames.ContainsByPredicate([NodeId](const FString& Item)
			{
				return Item.Equals(NodeId, ESearchCase::IgnoreCase);
			});

		if (bFound)
		{
			return MediaInputGroup.MediaSource;
		}
	}

	return nullptr;
}

UMediaOutput* FDisplayClusterConfigurationMediaICVFX::GetMediaOutput(const FString& NodeId) const
{
	// Look up for a group that contains node ID specified
	for (const FDisplayClusterConfigurationMediaOutputGroup& MediaOutputGroup : MediaOutputGroups)
	{
		const bool bFound = MediaOutputGroup.ClusterNodes.ItemNames.ContainsByPredicate([NodeId](const FString& Item)
			{
				return Item.Equals(NodeId, ESearchCase::IgnoreCase);
			});

		if (bFound)
		{
			return MediaOutputGroup.MediaOutput;
		}
	}

	return nullptr;
}

UDisplayClusterMediaOutputSynchronizationPolicy* FDisplayClusterConfigurationMediaICVFX::GetOutputSyncPolicy(const FString& NodeId) const
{
	// Look up for a group that contains node ID specified
	for (const FDisplayClusterConfigurationMediaOutputGroup& MediaOutputGroup : MediaOutputGroups)
	{
		const bool bFound = MediaOutputGroup.ClusterNodes.ItemNames.ContainsByPredicate([NodeId](const FString& Item)
			{
				return Item.Equals(NodeId, ESearchCase::IgnoreCase);
			});

		if (bFound)
		{
			return MediaOutputGroup.OutputSyncPolicy;
		}
	}

	return nullptr;
}
