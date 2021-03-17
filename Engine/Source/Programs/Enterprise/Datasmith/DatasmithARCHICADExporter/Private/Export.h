// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class FExport
{
  public:
	static GSErrCode Register();

	static GSErrCode Initialize();

	static GSErrCode SaveDatasmithFile(void* inIOParams, void* InSight);
};

END_NAMESPACE_UE_AC
