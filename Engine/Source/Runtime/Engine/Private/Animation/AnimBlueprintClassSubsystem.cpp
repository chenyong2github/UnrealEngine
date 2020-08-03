// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimBlueprintClassSubsystem.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceSubsystemData.h"

const UScriptStruct* UAnimBlueprintClassSubsystem::GetInstanceDataType() const
{ 
	return FAnimInstanceSubsystemData::StaticStruct();
}

void UAnimBlueprintClassSubsystem::PostLoad()
{
	Super::PostLoad();

	PostLoadSubsystem();
}