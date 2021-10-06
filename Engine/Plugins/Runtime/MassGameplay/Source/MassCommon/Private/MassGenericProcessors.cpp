// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassGenericProcessors.h"
#include "MassCommonFragments.h"

//----------------------------------------------------------------------//
// URandomizeVectorProcessor
//----------------------------------------------------------------------//
URandomizeVectorProcessor::URandomizeVectorProcessor()
{
	ComponentType = FVectorFragment::StaticStruct();
	bAutoRegisterWithProcessingPhases = false;
}

void URandomizeVectorProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(RandomizeVectorProcessor_Run);
	
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			for (FVector& Vector : ComponentList)
			{
				const FRotator Rotation(0.0f, FMath::FRand() * 360.0f, 0.0f);
				Vector = Rotation.Vector() * (MinMagnitude + FMath::FRand() * (MaxMagnitude - MinMagnitude));
			}
		});
}

#if WITH_EDITOR
void URandomizeVectorProcessor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName ComponentTypeName = GET_MEMBER_NAME_CHECKED(URandomizeVectorProcessor, ComponentType);
	static const int32 SupportedSize = FVectorFragment::StaticStruct()->GetStructureSize();

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == ComponentTypeName)
		{
			if (ComponentType == nullptr
				|| ComponentType->IsChildOf(FMassFragment::StaticStruct()) == false
				|| ComponentType->GetStructureSize() != SupportedSize)
			{
				ComponentType = FVectorFragment::StaticStruct();
			}
		}
	}
}
#endif // WITH_EDITOR
