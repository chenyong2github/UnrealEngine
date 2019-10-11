// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapPrivilegeTypes.h"
#include "MagicLeapPrivilegesFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPPRIVILEGES_API UMagicLeapPrivilegesFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	  Check whether the application has the specified privilege.
	  This does not solicit consent from the end-user and is non-blocking.
	  @param Privilege The privilege to check.
	  @return True if the privilege is granted, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Privileges|MagicLeap")
	static bool CheckPrivilege(EMagicLeapPrivilege Privilege);

	/**
	  Request the specified privilege.
	  This may possibly solicit consent from the end-user; if so it will block.
	  @param Privilege The privilege to request.
	  @return True if the privilege is granted, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Privileges|MagicLeap")
	static bool RequestPrivilege(EMagicLeapPrivilege Privilege);

	/**
	  Request the specified privilege asynchronously.
	  This may possibly solicit consent from the end-user. Result will be delivered
	  to the specified delegate.
	  @param Privilege The privilege to request.
	  @param ResultDelegate Callback which reports the result of the request.
	  @return True if the privilege request was successfully dispatched, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Privileges|MagicLeap")
	static bool RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestDelegate& ResultDelegate);
};
