// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Graph/SGraphPinCurveFloat.h"
#include "Graph/ControlRigGraph.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "DetailLayoutBuilder.h"
#include "PropertyPathHelpers.h"

#include "IControlRigEditorModule.h"

void SGraphPinCurveFloat::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SGraphPinCurveFloat::GetDefaultValueWidget()
{
	UControlRigGraph* RigGraph = Cast<UControlRigGraph>(GraphPinObj->GetOwningNode()->GetGraph());

	TSharedRef<SWidget> Widget = SNew(SBox)
		.MinDesiredWidth(200)
		.MaxDesiredWidth(400)
		.MinDesiredHeight(175)
		.MaxDesiredHeight(300)
		[
			SAssignNew(CurveEditor, SCurveEditor)
			.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
			.ViewMinInput(0.f)
			.ViewMaxInput(1.f)
			.ViewMinOutput(0.f)
			.ViewMaxOutput(1.f)
			.TimelineLength(1.f)
			.DesiredSize(FVector2D(300, 200))
			.HideUI(true)
		];

	CurveEditor->SetCurveOwner(this);

	return Widget;
}

TArray<FRichCurveEditInfoConst> SGraphPinCurveFloat::GetCurves() const
{
	TArray<FRichCurveEditInfoConst> Curves;
	Curves.Add(Curve.GetRichCurveConst());
	return Curves;
}

TArray<FRichCurveEditInfo> SGraphPinCurveFloat::GetCurves()
{
	TArray<FRichCurveEditInfo> Curves;
	Curves.Add(UpdateAndGetCurve().GetRichCurve());
	return Curves;
}

FRuntimeFloatCurve& SGraphPinCurveFloat::UpdateAndGetCurve()
{
	if (UEdGraphPin* Pin = GetPinObj())
	{
		FRuntimeFloatCurve::StaticStruct()->ImportText(*Pin->DefaultValue, &Curve, nullptr, EPropertyPortFlags::PPF_None, nullptr, FRuntimeFloatCurve::StaticStruct()->GetName(), true);
	}
	return Curve;
}


void SGraphPinCurveFloat::ModifyOwner()
{
	if (UEdGraphPin* Pin = GetPinObj())
	{
		Pin->Modify();
	}
}

TArray<const UObject*> SGraphPinCurveFloat::GetOwners() const
{
	TArray<const UObject*> Owners;
	if (UEdGraphPin* Pin = GetPinObj())
	{
		if (UEdGraphNode* Node = Pin->GetOwningNode())
		{
			Owners.Add(Node);
		}
	}
	return Owners;
}

void SGraphPinCurveFloat::MakeTransactional()
{
}

bool SGraphPinCurveFloat::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	if (UEdGraphPin* Pin = GetPinObj())
	{
		if (UControlRigGraphNode* Node = Cast<UControlRigGraphNode>(Pin->GetOwningNode()))
		{
			if (const UStructProperty* StructProperty = Node->GetUnitProperty())
			{
				FString NodeName, PropertyName;
				Pin->PinName.ToString().Split(TEXT("."), &NodeName, &PropertyName);

				if (UProperty* CurveProperty = StructProperty->Struct->FindPropertyByName(*PropertyName))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void SGraphPinCurveFloat::OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos)
{
	if (UEdGraphPin* Pin = GetPinObj())
	{
		if (UControlRigGraphNode* Node = Cast<UControlRigGraphNode>(Pin->GetOwningNode()))
		{
			Pin->DefaultValue.Empty();
			FRuntimeFloatCurve::StaticStruct()->ExportText(Pin->DefaultValue, &Curve, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr, true);
			Node->CopyPinDefaultsToModel(Pin);
		}
	}
	ModifyOwner();
}
