// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprintGeneratedClass.h"
#include "Units/Control/RigUnit_Control.h"

UControlRigBlueprintGeneratedClass::UControlRigBlueprintGeneratedClass()
{
}

void UControlRigBlueprintGeneratedClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

#if WITH_EDITORONLY_DATA
	ControlUnitProperties.Empty();
	RigUnitProperties.Empty();

	for (TFieldIterator<FProperty> It(this); It; ++It)
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(*It))
		{
			if (StructProp->Struct->IsChildOf(FRigUnit::StaticStruct()))
			{
				RigUnitProperties.Add(StructProp);

				if (StructProp->Struct->IsChildOf(FRigUnit_Control::StaticStruct()))
				{
					ControlUnitProperties.Add(StructProp);
				}
			}
		}
	}
#endif
}

void UControlRigBlueprintGeneratedClass::PurgeClass(bool bRecompilingOnLoad)
{
	Super::PurgeClass(bRecompilingOnLoad);
}

uint8* UControlRigBlueprintGeneratedClass::GetPersistentUberGraphFrame(UObject* Obj, UFunction* FuncToCheck) const
{
	if(!IsInGameThread())
	{
		// we cant use the persistent frame if we are executing in parallel (as we could potentially thunk to BP)
		return nullptr;
	}

	return Super::GetPersistentUberGraphFrame(Obj, FuncToCheck);
}

