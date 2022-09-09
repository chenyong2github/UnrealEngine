// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectChooser_Asset.h"

UObject* UObjectChooser_Asset::ChooseObject(const UObject* ContextObject) const
{
	return Asset;
}