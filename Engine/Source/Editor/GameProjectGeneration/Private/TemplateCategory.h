// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

struct FTemplateCategory
{
	/** Localised name of this category */
	FText DisplayName;
	
	/** A description of the templates contained within this category */
	FText Description;

	/** A thumbnail to help identify this category (on the tab) */
	const FSlateBrush* Icon;

	/** A unique key for this category */
	FName Key;

	/** Is this a major or minor category? Controls where in the UI it will show up. */
	bool IsMajor;

	/** Is this an enterprise project? Will end up including Datasmith modules if true. */
	bool IsEnterprise;
};
