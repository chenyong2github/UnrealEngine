// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/TVariant.h"
#include "UObject/FieldPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"

namespace UE::PropertyViewer
{

struct ADVANCEDWIDGETS_API FPropertyPath
{
public:
	using FPropertyArray = TArray<const FProperty*, TInlineAllocator<1>>;

	FPropertyPath() = default;
	FPropertyPath(UObject* Object, const FProperty* Property);
	FPropertyPath(UObject* Object, FPropertyArray Properties);
	FPropertyPath(const UScriptStruct* ScriptStruct, void* Data, const FProperty* Property);
	FPropertyPath(const UScriptStruct* ScriptStruct, void* Data, FPropertyArray Properties);

public:
	bool HasProperty() const
	{
		return Properties.Num() != 0;
	}

	const FProperty* GetLastProperty() const
	{
		return HasProperty() ? Properties.Last() : nullptr;
	}

	TArrayView<const FProperty* const> GetProperties() const
	{
		return MakeArrayView(Properties);
	}

	void* GetContainerPtr();
	const void* GetContainerPtr() const;

private:
	TWeakObjectPtr<UObject> TopLevelContainer_Object = nullptr;
	TWeakObjectPtr<const UScriptStruct> TopLevelContainer_ScriptStruct = nullptr;
	void* TopLevelContainer_ScriptStructData = nullptr;

	FPropertyArray Properties;
};

} //namespace
