// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectPtr.h"
#include "Misc/StringBuilder.h"

FString FObjectPtr::GetPath() const
{
	FObjectRef Ref = MakeObjectRef(Handle);
	if (Ref.PackageName == NAME_None)
	{
		return FString();
	}
	FObjectPathId::ResolvedNameContainerType ResolvedNames;
	Ref.ObjectPath.Resolve(ResolvedNames);
	TStringBuilder<FName::StringBufferSize> CompletePath;
	CompletePath << Ref.PackageName;
	for (int32 ResolvedNameIndex = 0; ResolvedNameIndex < ResolvedNames.Num(); ++ResolvedNameIndex)
	{
		CompletePath << (ResolvedNameIndex == 1 ? SUBOBJECT_DELIMITER_CHAR : '.') << ResolvedNames[ResolvedNameIndex];
	}
	return FString(CompletePath);
}
