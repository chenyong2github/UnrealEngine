// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SchemaActions/DataprepSchemaAction.h"

#include "CoreMinimal.h"
#include "UObject/Class.h"

namespace DataprepMenuActionCollectorUtils
{
	DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<FDataprepSchemaAction>, FOnCreateMenuAction,	UClass&);

	/**
	 * Utils to gather the menu action from a base class 
	 * @param Class The base class from which we want to create the actions
	 * @param OnValidClassFound Callback to generate the menu action from the class
	 */
	TArray<TSharedPtr<FDataprepSchemaAction>> GatherMenuActionForDataprepClass(UClass& Class, FOnCreateMenuAction OnValidClassFound);

	TArray<UClass*> GetNativeChildClasses(UClass&);

	constexpr EClassFlags NonDesiredClassFlags = CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract; 
}
