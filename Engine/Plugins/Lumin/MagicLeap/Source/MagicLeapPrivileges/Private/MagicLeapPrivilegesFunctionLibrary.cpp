// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapPrivilegesFunctionLibrary.h"
#include "MagicLeapPrivilegesModule.h"
#include "MagicLeapPrivilegeUtils.h"

bool UMagicLeapPrivilegesFunctionLibrary::CheckPrivilege(EMagicLeapPrivilege Privilege)
{
#if WITH_MLSDK
	return GetMagicLeapPrivilegesModule().CheckPrivilege(Privilege);
#endif //WITH_MLSDK
	return false;
}

bool UMagicLeapPrivilegesFunctionLibrary::RequestPrivilege(EMagicLeapPrivilege Privilege)
{
#if WITH_MLSDK
	return GetMagicLeapPrivilegesModule().RequestPrivilege(Privilege);
#endif //WITH_MLSDK
	return false;
}

bool UMagicLeapPrivilegesFunctionLibrary::RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestDelegate& ResultDelegate)
{
#if WITH_MLSDK
	return GetMagicLeapPrivilegesModule().RequestPrivilegeAsync(Privilege, ResultDelegate);
#else
	return false;
#endif //WITH_MLSDK
}
