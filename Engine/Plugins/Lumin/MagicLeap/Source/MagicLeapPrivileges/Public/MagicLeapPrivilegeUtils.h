// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MagicLeapPrivilegeTypes.h"
#include "Lumin/CAPIShims/LuminAPIPrivileges.h"

namespace MagicLeap
{
#if WITH_MLSDK
	MLPrivilegeID MAGICLEAPPRIVILEGES_API UnrealToMLPrivilege(EMagicLeapPrivilege Privilege);

	FString MAGICLEAPPRIVILEGES_API MLPrivilegeToString(MLPrivilegeID PrivilegeID);

	FString MAGICLEAPPRIVILEGES_API MLPrivilegeToString(EMagicLeapPrivilege PrivilegeID);
#endif //WITH_MLSDK
}
