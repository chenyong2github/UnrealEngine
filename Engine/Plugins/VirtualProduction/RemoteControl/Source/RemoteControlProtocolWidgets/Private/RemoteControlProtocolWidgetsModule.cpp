// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolWidgetsModule.h"

#include "RemoteControlPreset.h"
#include "ViewModels/ProtocolEntityViewModel.h"
#include "Widgets/SRCProtocolBindingList.h"

DEFINE_LOG_CATEGORY(LogRemoteControlProtocolWidgets);

TSharedRef<SWidget> FRemoteControlProtocolWidgetsModule::GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType)
{
	check(InPreset);

	// Currently only supports Properties
	if(InFieldId.IsValid() && InFieldType == EExposedFieldType::Property)
	{
		return SNew(SRCProtocolBindingList, FProtocolEntityViewModel::Create(InPreset, InFieldId));
	}

	return SNullWidget::NullWidget;
}

IMPLEMENT_MODULE(FRemoteControlProtocolWidgetsModule, RemoteControlProtocolWidgets);
