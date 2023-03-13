// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamTypeHandle.h"

class UAnimNextInterfaceGraph;
class UAnimNextInterfaceGraph_EditorData;
class UAnimNextInterfaceGraph_EdGraph;
class URigVMController;
class URigVMGraph;
struct FEdGraphPinType;

namespace UE::AnimNext::InterfaceGraphUncookedOnly
{

struct ANIMNEXTINTERFACEGRAPHUNCOOKEDONLY_API FUtils
{
	static void Compile(UAnimNextInterfaceGraph* InGraph);
	
	static UAnimNextInterfaceGraph_EditorData* GetEditorData(const UAnimNextInterfaceGraph* InAnimNextInterfaceGraph);
	
	static void RecreateVM(UAnimNextInterfaceGraph* InGraph);

	/**
	 * Get an AnimNext parameter type handle from an FEdGraphPinType.
	 * Note that the returned handle may not be valid, so should be checked using IsValid() before use.
	 **/
	static FParamTypeHandle GetParameterHandleFromPin(const FEdGraphPinType& InPinType);
};

}