// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpOptions.h"


FDatasmithSketchUpOptions& FDatasmithSketchUpOptions::GetSingleton()
{
	static FDatasmithSketchUpOptions Singleton;

	return Singleton;
}
