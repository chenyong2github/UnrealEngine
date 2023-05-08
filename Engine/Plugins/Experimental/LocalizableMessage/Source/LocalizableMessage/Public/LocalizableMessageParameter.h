// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

#include "LocalizableMessageParameter.generated.h"

USTRUCT()
struct FLocalizableMessageParameter
{
	GENERATED_BODY()

	FLocalizableMessageParameter() = default;
	virtual ~FLocalizableMessageParameter() = default;

	virtual UScriptStruct* GetScriptStruct() const PURE_VIRTUAL(FLocalizableMessageParameter::GetScriptStruct, return FLocalizableMessageParameter::StaticStruct();)

	template <typename ChildType>
	static ChildType* AllocateType()
	{
		return static_cast<ChildType*>(AllocateType(ChildType::StaticStruct()));
	}

	LOCALIZABLEMESSAGE_API static FLocalizableMessageParameter* AllocateType(const UScriptStruct* Type);

	struct FCustomDeleter
	{
		LOCALIZABLEMESSAGE_API void operator()(FLocalizableMessageParameter* Object) const;
	};
};