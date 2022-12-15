// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMExecuteContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMExecuteContext)

bool FRigVMExecuteContext::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	static const FName ControlRigExecuteContextName("ControlRigExecuteContext");
	if (Tag.Type == NAME_StructProperty && Tag.StructName == ControlRigExecuteContextName)
	{
		static const FString CRExecuteContextPath = TEXT("/Script/ControlRig.ControlRigExecuteContext");
		UScriptStruct* OldStruct = FindFirstObject<UScriptStruct>(*CRExecuteContextPath, EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous);
		checkf(OldStruct, TEXT("FControlRigExecuteContext was not found."));

		TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(OldStruct));
		OldStruct->SerializeItem(Slot, StructOnScope->GetStructMemory(), nullptr);		
		return true;
	}

	return false;
}
