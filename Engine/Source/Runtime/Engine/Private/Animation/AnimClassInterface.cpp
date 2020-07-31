// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"

void IAnimClassInterface::ForEachAnimInstanceSubsystemData(UAnimInstance* InAnimInstance, TFunctionRef<void(UAnimBlueprintClassSubsystem*, FAnimInstanceSubsystemData&)> InFunction)
{
	IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(InAnimInstance->GetClass());
	if(AnimClassInterface)
	{
		const TArray<UAnimBlueprintClassSubsystem*>& Subsystems = AnimClassInterface->GetSubsystems();
		const TArray<FStructProperty*>& SubsystemProperties = AnimClassInterface->GetSubsystemProperties();
		check(Subsystems.Num() == SubsystemProperties.Num());

		for(int32 SubsystemIndex = 0; SubsystemIndex < Subsystems.Num(); ++SubsystemIndex)
		{
			UAnimBlueprintClassSubsystem* Subsystem = Subsystems[SubsystemIndex];
			FStructProperty* SubsystemProperty = SubsystemProperties[SubsystemIndex];
			check(Subsystem->GetInstanceDataType() == SubsystemProperty->Struct);

			FAnimInstanceSubsystemData* InstanceData = SubsystemProperty->ContainerPtrToValuePtr<FAnimInstanceSubsystemData>(InAnimInstance);
			InFunction(Subsystem, *InstanceData);
		}
	}
}