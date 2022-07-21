// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVMPropertyPath.h"

class UClass;
class UEdGraph;
class UMVVMBlueprintView;

namespace UE::MVVM::ConversionFunctionHelper
{

	/** Find all BlueprintPropertyPath used in the Graph. */
	MODELVIEWVIEWMODELBLUEPRINT_API TArray<FMVVMBlueprintPropertyPath> FindAllPropertyPathInGraph(const UEdGraph* Graph, const UMVVMBlueprintView* View, UClass* Class);

} //namespace