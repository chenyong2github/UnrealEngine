// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"


class FOptimusToolsStyle
    : public FSlateStyleSet
{
public:
	static FOptimusToolsStyle& Get();

protected:
	friend class FOptimusToolsModule;

	static void Register();
	static void Unregister();

private:
	FOptimusToolsStyle();
};
