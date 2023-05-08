// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalizableMessageParameter.h"

FLocalizableMessageParameter* FLocalizableMessageParameter::AllocateType(const UScriptStruct* Type)
{
	FLocalizableMessageParameter* NewEntry = (FLocalizableMessageParameter*)FMemory::Malloc(Type->GetCppStructOps()->GetSize());
	Type->InitializeStruct(NewEntry);
	return NewEntry;
}

void FLocalizableMessageParameter::FCustomDeleter::operator()(FLocalizableMessageParameter* Object) const
{
	if (Object)
	{
		UScriptStruct* ScriptStruct = Object->GetScriptStruct();
		check(ScriptStruct);
		ScriptStruct->DestroyStruct(Object);
		FMemory::Free(Object);
	}
}
