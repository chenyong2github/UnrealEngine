// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConnectionRemapUtilsImpl.h"

#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "UI/VCamConnectionStructs.h"
#include "Widgets/Text/STextBlock.h"

namespace UE::VCamCoreEditor::Private
{
	FConnectionRemapUtilsImpl::FConnectionRemapUtilsImpl(TSharedRef<IDetailLayoutBuilder> Builder)
		: Builder(MoveTemp(Builder))
	{}

	void FConnectionRemapUtilsImpl::AddConnection(FAddConnectionArgs Args)
	{
		TSharedPtr<IDetailLayoutBuilder> BuilderPin =  Builder.Pin();
		if (!BuilderPin)
		{
			return;
		}
		
		TSharedPtr<FStructOnScope> StructData;
		if (const TSharedPtr<FStructOnScope>* ExistingStructData = AddedConnections.Find(Args.PropertyName))
		{
			StructData = *ExistingStructData;
		}
		else
		{
			StructData = MakeShared<FStructOnScope>(MoveTemp(Args.StructData));
			AddedConnections.Add(Args.PropertyName, StructData);
		}
		
		const TSharedPtr<IPropertyHandle> PropertyHandle = BuilderPin->AddStructurePropertyData(StructData, Args.PropertyName);
		if (!ensureMsgf(PropertyHandle, TEXT("No property of %s found on struct"), *Args.PropertyName.ToString()))
		{
			return;
		}
		
		PropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda([PropertyHandle, Callback = MoveTemp(Args.OnTargetSettingsChangedDelegate)]()
		{
			void* Data;
			if (ensure(PropertyHandle->GetValueData(Data) == FPropertyAccess::Success))
			{
				Callback.Execute(*(FVCamConnectionTargetSettings*)Data);
			}
		}));

		Args.DetailGroup.AddPropertyRow(PropertyHandle.ToSharedRef())
			.DisplayName(FText::FromName(Args.ConnectionName)) 
			.CustomWidget(true)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(Args.Font)
				.Text(FText::FromName(Args.ConnectionName))
			]
			.ValueContent()
			[
				PropertyHandle->CreatePropertyValueWidget()
			];
	}

	FSlateFontInfo FConnectionRemapUtilsImpl::GetRegularFont() const
	{
		if (TSharedPtr<IDetailLayoutBuilder> BuilderPin = Builder.Pin())
		{
			return BuilderPin->GetDetailFont();
		}
		return FSlateFontInfo{};
	}

	void FConnectionRemapUtilsImpl::ForceRefreshProperties() const
	{
		if (TSharedPtr<IDetailLayoutBuilder> BuilderPin = Builder.Pin())
		{
			BuilderPin->ForceRefreshDetails();
		}
	}
}

