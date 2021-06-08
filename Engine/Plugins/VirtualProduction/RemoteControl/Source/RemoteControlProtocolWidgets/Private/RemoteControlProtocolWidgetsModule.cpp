// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolWidgetsModule.h"

#include "IRemoteControlModule.h"
#include "IRemoteControlProtocol.h"
#include "IRemoteControlProtocolModule.h"
#include "IRemoteControlModule.h"
#include "RemoteControlPreset.h"
#include "ViewModels/ProtocolEntityViewModel.h"
#include "Widgets/SRCProtocolBindingList.h"

DEFINE_LOG_CATEGORY(LogRemoteControlProtocolWidgets);

TSharedRef<SWidget> FRemoteControlProtocolWidgetsModule::GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType)
{
	check(InPreset);

	ResetProtocolBindingList();

	// Currently only supports Properties
	if (InFieldId.IsValid() && InFieldType == EExposedFieldType::Property)
	{
		return SAssignNew(RCProtocolBindingList, SRCProtocolBindingList, FProtocolEntityViewModel::Create(InPreset, InFieldId));
	}

	return SNullWidget::NullWidget;
}

void FRemoteControlProtocolWidgetsModule::ResetProtocolBindingList()
{
	RCProtocolBindingList = nullptr;
}

TSharedPtr<IRCProtocolBindingList> FRemoteControlProtocolWidgetsModule::GetProtocolBindingList() const
{
	return RCProtocolBindingList;
}

IMPLEMENT_MODULE(FRemoteControlProtocolWidgetsModule, RemoteControlProtocolWidgets);
