// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigGraphPanelPinFactory.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/SControlRigGraphPinNameList.h"
#include "Graph/SControlRigGraphPinCurveFloat.h"
#include "Graph/SControlRigGraphPinVariableName.h"
#include "Graph/SControlRigGraphPinParameterName.h"
#include "KismetPins/SGraphPinExec.h"
#include "ControlRig.h"
#include "NodeFactory.h"
#include "EdGraphSchema_K2.h"
#include "Curves/CurveFloat.h"
#include "RigVMModel/Nodes/RigVMStructNode.h"
#include "RigVMCore/RigVMExecuteContext.h"

TSharedPtr<SGraphPin> FControlRigGraphPanelPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (InPin)
	{
		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InPin->GetOwningNode()))
		{
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(RigNode->GetGraph());

			if (URigVMPin* ModelPin = RigNode->GetModelPinFromPinPath(InPin->GetName()))
			{
				FName CustomWidgetName = ModelPin->GetCustomWidgetName();
				if (CustomWidgetName == TEXT("BoneName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetBoneNameList);
				}
				else if (CustomWidgetName == TEXT("ControlName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetControlNameList);
				}
				else if (CustomWidgetName == TEXT("SpaceName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetSpaceNameList);
				}
				else if (CustomWidgetName == TEXT("CurveName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetCurveNameList);
				}
				else if (CustomWidgetName == TEXT("ElementName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetElementNameList);
				}
				else if (CustomWidgetName == TEXT("DrawingName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetDrawingNameList);
				}
				else if (CustomWidgetName == TEXT("VariableName"))
				{
					return SNew(SControlRigGraphPinVariableName, InPin);
				}
				else if (CustomWidgetName == TEXT("ParameterName"))
				{
					return SNew(SControlRigGraphPinParameterName, InPin);
				}
			}

			if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				if (Cast<UStruct>(InPin->PinType.PinSubCategoryObject)->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					return SNew(SGraphPinExec, InPin);
				}
				else if (InPin->PinType.PinSubCategoryObject == FRuntimeFloatCurve::StaticStruct())
				{
					return SNew(SControlRigGraphPinCurveFloat, InPin);
				}
			}
		}
	}

	TSharedPtr<SGraphPin> K2PinWidget = FNodeFactory::CreateK2PinWidget(InPin);
	if(K2PinWidget.IsValid())
	{
		return K2PinWidget;
	}

	return nullptr;
}