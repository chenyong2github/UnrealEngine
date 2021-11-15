// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassGameplayDebugTypes.h"
#include "MassCommonTypes.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"


#if WITH_MASSGAMEPLAY_DEBUG

namespace UE::Mass::Debug
{

void GetDebugEntitiesAndLocations(const UMassEntitySubsystem& EntitySystem, TArray<FMassEntityHandle>& OutEntities, TArray<FVector>& OutLocations)
{
	int32 DebugEntityEnd, DebugEntityBegin;
	if (GetDebugEntitiesRange(DebugEntityEnd, DebugEntityBegin) == false)
	{
		return;
	}

	OutEntities.Reserve(DebugEntityEnd - DebugEntityBegin);
	OutLocations.Reserve(DebugEntityEnd - DebugEntityBegin);

	for (int32 i = DebugEntityBegin; i <= DebugEntityEnd; ++i)
	{
		const FMassEntityHandle EntityHandle = ConvertEntityIndexToHandle(EntitySystem, i);
		if (EntityHandle.IsSet())
		{
			if (const FDataFragment_Transform* TransformFragment = EntitySystem.GetFragmentDataPtr<FDataFragment_Transform>(EntityHandle))
			{
				OutEntities.Add(EntityHandle);
				OutLocations.Add(TransformFragment->GetTransform().GetLocation());
			}
		}
	}
}

FMassEntityHandle ConvertEntityIndexToHandle(const UMassEntitySubsystem& EntitySystem, const int32 EntityIndex)
{
	return EntitySystem.DebugGetEntityIndexHandle(EntityIndex);
}

} // namespace UE::Mass::Debug
#endif // WITH_MASSGAMEPLAY_DEBUG