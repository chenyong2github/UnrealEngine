// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UFunction;
class UStruct;

namespace UE::PropertyViewer
{

/** */
class ADVANCEDWIDGETS_API IFieldExpander
{
public:
	virtual bool CanExpandObject(const UClass*) const = 0;
	virtual bool CanExpandFunction(const UFunction*) const = 0;
	virtual ~IFieldExpander() = default;
};

class ADVANCEDWIDGETS_API FFieldExpander_NoExpand : public IFieldExpander
{
	virtual bool CanExpandObject(const UClass*) const override { return true; }
	virtual bool CanExpandFunction(const UFunction*) const override { return false; }
};

} //namespace
