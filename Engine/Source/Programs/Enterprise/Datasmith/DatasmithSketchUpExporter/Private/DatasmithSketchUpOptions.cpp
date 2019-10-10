// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpOptions.h"


FDatasmithSketchUpOptions& FDatasmithSketchUpOptions::GetSingleton()
{
	static FDatasmithSketchUpOptions Singleton;

	return Singleton;
}
