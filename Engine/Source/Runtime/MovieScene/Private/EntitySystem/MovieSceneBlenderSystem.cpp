// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"



uint16 UMovieSceneBlenderSystem::AllocateBlendChannel()
{
	int32 NewBlendChannel = AllocatedBlendChannels.FindAndSetFirstZeroBit();
	if (NewBlendChannel == INDEX_NONE)
	{
		NewBlendChannel = AllocatedBlendChannels.Add(true);
	}

	checkf(NewBlendChannel < TNumericLimits<uint16>::Max(), TEXT("Maximum number of active blends reached - this indicates either a leak, or more than 65535 blend channels are genuinely required"));
	return static_cast<uint16>(NewBlendChannel);
}


void UMovieSceneBlenderSystem::ReleaseBlendChannel(uint16 BlendID)
{
	AllocatedBlendChannels[BlendID] = false;
}


bool UMovieSceneBlenderSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	return AllocatedBlendChannels.Find(true) != INDEX_NONE;
}