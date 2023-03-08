// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationDataRegistryTypes.h"
#include "AnimationDataRegistry.h"
#include "AnimationReferencePose.h"

namespace UE::AnimNext::Interface
{

FAnimationDataHandle::~FAnimationDataHandle()
{
	if (AllocatedBlock != nullptr)
	{
		check(AllocatedBlock->GetRefCount() > 0);

		const int32 CurrentCount = AllocatedBlock->Release();
		check(CurrentCount >= 0);

		if (CurrentCount == 0)
		{
			FAnimationDataRegistry::Get()->FreeAllocatedBlock(AllocatedBlock);
		}
	}
}

} // namespace UE::AnimNext::Interface
