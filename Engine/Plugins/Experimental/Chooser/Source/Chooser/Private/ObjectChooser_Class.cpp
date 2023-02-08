// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectChooser_Class.h"

UObject* FClassChooser::ChooseObject(const UObject* ContextObject) const
{
	return Class;
}