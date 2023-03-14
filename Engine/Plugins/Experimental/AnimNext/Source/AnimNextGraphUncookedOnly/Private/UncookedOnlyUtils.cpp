// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncookedOnlyUtils.h"
#include "AnimNextGraph.h"
#include "AnimNextGraph_EditorData.h"
#include "AnimNextGraph_EdGraph.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVM.h"
#include "ExecuteContext.h"

namespace UE::AnimNext::GraphUncookedOnly
{

void FUtils::Compile(UAnimNextGraph* InGraph)
{
	check(InGraph);
	
	UAnimNextGraph_EditorData* EditorData = GetEditorData(InGraph);
	
	if(EditorData->bIsCompiling)
	{
		return;
	}
	
	TGuardValue<bool> CompilingGuard(EditorData->bIsCompiling, true);
	
	EditorData->bErrorsDuringCompilation = false;

	EditorData->RigGraphDisplaySettings.MinMicroSeconds = EditorData->RigGraphDisplaySettings.LastMinMicroSeconds = DBL_MAX;
	EditorData->RigGraphDisplaySettings.MaxMicroSeconds = EditorData->RigGraphDisplaySettings.LastMaxMicroSeconds = (double)INDEX_NONE;
	
	TGuardValue<bool> ReentrantGuardSelf(EditorData->bSuspendModelNotificationsForSelf, true);
	TGuardValue<bool> ReentrantGuardOthers(EditorData->bSuspendModelNotificationsForOthers, true);

	RecreateVM(InGraph);

	InGraph->VMRuntimeSettings = EditorData->VMRuntimeSettings;

	EditorData->CompileLog.Messages.Reset();
	EditorData->CompileLog.NumErrors = EditorData->CompileLog.NumWarnings = 0;

	URigVMCompiler* Compiler = URigVMCompiler::StaticClass()->GetDefaultObject<URigVMCompiler>();
	EditorData->VMCompileSettings.SetExecuteContextStruct(EditorData->RigVMClient.GetExecuteContextStruct());
	Compiler->Settings = (EditorData->bCompileInDebugMode) ? FRigVMCompileSettings::Fast(EditorData->VMCompileSettings.GetExecuteContextStruct()) : EditorData->VMCompileSettings;
	URigVMController* RootController = EditorData->GetRigVMClient()->GetOrCreateController(EditorData->GetRigVMClient()->GetDefaultModel());
	Compiler->Compile(EditorData->GetRigVMClient()->GetAllModels(false, false), RootController, InGraph->RigVM, InGraph->GetRigVMExternalVariables(), &EditorData->PinToOperandMap);

	if (EditorData->bErrorsDuringCompilation)
	{
		if(Compiler->Settings.SurpressErrors)
		{
			Compiler->Settings.Reportf(EMessageSeverity::Info, InGraph,TEXT("Compilation Errors may be suppressed for AnimNext Interface Graph: %s. See VM Compile Settings for more Details"), *InGraph->GetName());
		}
	}

	EditorData->bVMRecompilationRequired = false;
	if(InGraph->RigVM)
	{
		EditorData->VMCompiledEvent.Broadcast(InGraph, InGraph->RigVM);
	}

#if WITH_EDITOR
//	RefreshBreakpoints(EditorData);
#endif
}

void FUtils::RecreateVM(UAnimNextGraph* InGraph)
{
	InGraph->RigVM = NewObject<URigVM>(InGraph, TEXT("VM"), RF_NoFlags);
	InGraph->RigVM->SetContextPublicDataStruct(FAnimNextExecuteContext::StaticStruct());
	
	// Cooked platforms will load these pointers from disk
	if (!FPlatformProperties::RequiresCookedData())
	{
		// We dont support ERigVMMemoryType::Work memory as we dont operate on an instance
	//	InGraph->RigVM->GetMemoryByType(ERigVMMemoryType::Work, true);
		InGraph->RigVM->GetMemoryByType(ERigVMMemoryType::Literal, true);
		InGraph->RigVM->GetMemoryByType(ERigVMMemoryType::Debug, true);
	}

	InGraph->RigVM->Reset();
}

UAnimNextGraph_EditorData* FUtils::GetEditorData(const UAnimNextGraph* InAnimNextGraph)
{
	check(InAnimNextGraph);
	
	return CastChecked<UAnimNextGraph_EditorData>(InAnimNextGraph->EditorData);
}

FParamTypeHandle FUtils::GetParameterHandleFromPin(const FEdGraphPinType& InPinType)
{
	FAnimNextParamType::EValueType ValueType = FAnimNextParamType::EValueType::None;
	FAnimNextParamType::EContainerType ContainerType = FAnimNextParamType::EContainerType::None;
	UObject* ValueTypeObject = nullptr;

	if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		ValueType = FAnimNextParamType::EValueType::Bool;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		ValueType = FAnimNextParamType::EValueType::Byte;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		ValueType = FAnimNextParamType::EValueType::Int32;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
	{
		ValueType = FAnimNextParamType::EValueType::Int64;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		if (InPinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
		{
			ValueType = FAnimNextParamType::EValueType::Float;
		}
		else if (InPinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
		{
			ValueType = FAnimNextParamType::EValueType::Double;
		}
		else
		{
			ensure(false);	// Reals should be either floats or doubles
		}
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Float)
	{
		ValueType = FAnimNextParamType::EValueType::Float;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Double)
	{
		ValueType = FAnimNextParamType::EValueType::Double;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		ValueType = FAnimNextParamType::EValueType::Name;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		ValueType = FAnimNextParamType::EValueType::String;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		ValueType = FAnimNextParamType::EValueType::Text;
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
	{
		ValueType = FAnimNextParamType::EValueType::Enum;
		ValueTypeObject = InPinType.PinSubCategoryObject.Get();
		ensure(ValueTypeObject);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		ValueType = FAnimNextParamType::EValueType::Struct;
		ValueTypeObject = Cast<UScriptStruct>(InPinType.PinSubCategoryObject.Get());
		ensure(ValueTypeObject);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		ValueType = FAnimNextParamType::EValueType::Object;
		ValueTypeObject = Cast<UClass>(InPinType.PinSubCategoryObject.Get());
		ensure(ValueTypeObject);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
	{
		ValueType = FAnimNextParamType::EValueType::SoftObject;
		ValueTypeObject = Cast<UClass>(InPinType.PinSubCategoryObject.Get());
		ensure(ValueTypeObject);
	}
	else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		ValueType = FAnimNextParamType::EValueType::SoftClass;
		ValueTypeObject = Cast<UClass>(InPinType.PinSubCategoryObject.Get());
		ensure(ValueTypeObject);
	}

	if(InPinType.ContainerType == EPinContainerType::Array)
	{
		ContainerType = FAnimNextParamType::EContainerType::Array;
	}
	else if(InPinType.ContainerType == EPinContainerType::Set)
	{
		ensureMsgf(false, TEXT("Set pins are not yet supported"));
	}
	if(InPinType.ContainerType == EPinContainerType::Map)
	{
		ensureMsgf(false, TEXT("Map pins are not yet supported"));
	}
	
	return FAnimNextParamType(ValueType, ContainerType, ValueTypeObject).GetHandle();
}

}
