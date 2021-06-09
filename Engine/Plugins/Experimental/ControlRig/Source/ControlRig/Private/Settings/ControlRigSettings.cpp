// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/ControlRigSettings.h"

#if WITH_EDITOR
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMNode.h"
#endif

#include "Units/Hierarchy/RigUnit_GetTransform.h"
#include "Units/Hierarchy/RigUnit_SetTransform.h"

UControlRigSettings::UControlRigSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, bResetControlTransformsOnCompile(true)
#endif
{
#if WITH_EDITORONLY_DATA
	DefaultGizmoLibrary = LoadObject<UControlRigGizmoLibrary>(nullptr, TEXT("/ControlRig/Controls/DefaultGizmoLibrary.DefaultGizmoLibrary"));
	bResetControlsOnCompile = true;
	bResetControlsOnPinValueInteraction = false;

	SetupEventBorderColor = FLinearColor::Red;
	BackwardsSolveBorderColor = FLinearColor::Yellow;
	BackwardsAndForwardsBorderColor = FLinearColor::Blue;

	NodeSnippet_1 = GetSnippetContentForUnitNode(FRigUnit_GetTransform::StaticStruct());
	NodeSnippet_2 = GetSnippetContentForUnitNode(FRigUnit_SetTransform::StaticStruct());
	 
#endif
}

#if WITH_EDITOR

FString UControlRigSettings::GetSnippetContentForUnitNode(UScriptStruct* InUnitNodeStruct)
{
	URigVMGraph* Graph = NewObject<URigVMGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	URigVMController* Controller = NewObject<URigVMController>(GetTransientPackage(), NAME_None, RF_Transient);
	Controller->UnfoldStructDelegate.BindLambda([](const UStruct* InStruct) -> bool
	{
		if (InStruct == TBaseStructure<FQuat>::Get())
		{
			return false;
		}

		return true;
	});

	Controller->SetGraph(Graph);
	URigVMNode* Node = Controller->AddUnitNode(InUnitNodeStruct, FRigUnit::GetMethodName(), FVector2D::ZeroVector, FString(), false);
	TArray<FName> NodeNames;
	NodeNames.Add(Node->GetFName());
	return Controller->ExportNodesToText(NodeNames);
}

#endif