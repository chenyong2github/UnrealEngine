// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProxyTable.h"
#include "ProxyTableFunctionLibrary.generated.h"

/**
 * Proxy Table Function Library
 */
UCLASS()
class PROXYTABLE_API UProxyTableFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	* Evaluate a proxy table and return the selected UObject, or null
	*
	* @param ContextObject			(in) An Object from which the parameters to the Chooser Table will be read
	* @param ProxyTable				(in) The ProxyTable asset
	* @param Key					(in) The Key from the ProxyTable asset
	*/
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe), Category = "Animation")
	static UObject* EvaluateProxyTable(const UObject* ContextObject, const UProxyTable* ProxyTable, FName Key);

};