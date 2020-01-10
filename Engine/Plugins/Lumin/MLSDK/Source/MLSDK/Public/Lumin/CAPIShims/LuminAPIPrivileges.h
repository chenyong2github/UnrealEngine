// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_privilege_ids.h>
#include <ml_privilege_functions.h>
#include <ml_privileges.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesStartup)
#define MLPrivilegesStartup ::MLSDK_API::MLPrivilegesStartupShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesShutdown)
#define MLPrivilegesShutdown ::MLSDK_API::MLPrivilegesShutdownShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesCheckPrivilege)
#define MLPrivilegesCheckPrivilege ::MLSDK_API::MLPrivilegesCheckPrivilegeShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesRequestPrivilege)
#define MLPrivilegesRequestPrivilege ::MLSDK_API::MLPrivilegesRequestPrivilegeShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesRequestPrivilegeAsync)
#define MLPrivilegesRequestPrivilegeAsync ::MLSDK_API::MLPrivilegesRequestPrivilegeAsyncShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesRequestPrivilegeTryGet)
#define MLPrivilegesRequestPrivilegeTryGet ::MLSDK_API::MLPrivilegesRequestPrivilegeTryGetShim
CREATE_FUNCTION_SHIM(ml_privileges, const char*, MLPrivilegesGetResultString)
#define MLPrivilegesGetResultString ::MLSDK_API::MLPrivilegesGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
