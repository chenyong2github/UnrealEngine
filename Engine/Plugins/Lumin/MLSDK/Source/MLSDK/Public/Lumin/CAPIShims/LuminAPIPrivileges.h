// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_privilege_ids.h>
#include <ml_privilege_functions.h>
#include <ml_privileges.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesStartup)
#define MLPrivilegesStartup ::LUMIN_MLSDK_API::MLPrivilegesStartupShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesShutdown)
#define MLPrivilegesShutdown ::LUMIN_MLSDK_API::MLPrivilegesShutdownShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesCheckPrivilege)
#define MLPrivilegesCheckPrivilege ::LUMIN_MLSDK_API::MLPrivilegesCheckPrivilegeShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesRequestPrivilege)
#define MLPrivilegesRequestPrivilege ::LUMIN_MLSDK_API::MLPrivilegesRequestPrivilegeShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesRequestPrivilegeAsync)
#define MLPrivilegesRequestPrivilegeAsync ::LUMIN_MLSDK_API::MLPrivilegesRequestPrivilegeAsyncShim
CREATE_FUNCTION_SHIM(ml_privileges, MLResult, MLPrivilegesRequestPrivilegeTryGet)
#define MLPrivilegesRequestPrivilegeTryGet ::LUMIN_MLSDK_API::MLPrivilegesRequestPrivilegeTryGetShim
CREATE_FUNCTION_SHIM(ml_privileges, const char*, MLPrivilegesGetResultString)
#define MLPrivilegesGetResultString ::LUMIN_MLSDK_API::MLPrivilegesGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
