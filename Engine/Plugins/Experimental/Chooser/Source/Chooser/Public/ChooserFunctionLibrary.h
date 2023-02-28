// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Chooser.h"
#include "Templates/SubclassOf.h"
#include "ChooserFunctionLibrary.generated.h"

/**
 * Morpheus Extensions Function Library
 */
UCLASS()
class CHOOSER_API UChooserFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/**
	* Evaluate a chooser table and return the selected UObject, or null
	*
	* @param ContextObject			(in) An Object from which the parameters to the Chooser Table will be read
	* @param ChooserTable			(in) The ChooserTable asset
	* @param ObjectClass			(in) Expected type of result objects
	*/
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, DeterminesOutputType = "ObjectClass"), Category = "Animation")
	static UObject* EvaluateChooser(const UObject* ContextObject, const UChooserTable* ChooserTable, TSubclassOf<UObject> ObjectClass);

	/**
    * Evaluate a chooser table and return the list of all selected UObjects
    *
    * @param ContextObject			(in) An Object from which the parameters to the Chooser Table will be read
    * @param ChooserTable			(in) The ChooserTable asset
    * @param ObjectClass			(in) Expected type of result objects
    */
    UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, DeterminesOutputType = "ObjectClass"), Category = "Animation")
    static TArray<UObject*> EvaluateChooserMulti(const UObject* ContextObject, const UChooserTable* ChooserTable, TSubclassOf<UObject> ObjectClass);
};
