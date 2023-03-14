// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamTypeHandle.h"

class UAnimNextGraph;
class UAnimNextGraph_EditorData;
class UAnimNextGraph_EdGraph;
class URigVMController;
class URigVMGraph;
struct FEdGraphPinType;

namespace UE::AnimNext::GraphUncookedOnly
{

struct ANIMNEXTGRAPHUNCOOKEDONLY_API FUtils
{
	static void Compile(UAnimNextGraph* InGraph);
	
	static UAnimNextGraph_EditorData* GetEditorData(const UAnimNextGraph* InAnimNextInterfaceGraph);
	
	static void RecreateVM(UAnimNextGraph* InGraph);

	/**
	 * Get an AnimNext parameter type handle from an FEdGraphPinType.
	 * Note that the returned handle may not be valid, so should be checked using IsValid() before use.
	 **/
	static FParamTypeHandle GetParameterHandleFromPin(const FEdGraphPinType& InPinType);
};

}