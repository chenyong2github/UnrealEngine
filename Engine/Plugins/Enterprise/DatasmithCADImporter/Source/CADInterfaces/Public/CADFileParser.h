// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace CADLibrary
{

class ICADFileParser
{
public:
	virtual ~ICADFileParser() = default;
	virtual ECADParsingResult Process() = 0;
};

}