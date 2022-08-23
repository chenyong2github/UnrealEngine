// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolWidgetsModule.h"

#include "IRemoteControlModule.h"
#include "IRemoteControlProtocol.h"
#include "IRemoteControlProtocolModule.h"
#include "IRemoteControlModule.h"
#include "RemoteControlPreset.h"
#include "Styling/ProtocolPanelStyle.h"
#include "ViewModels/ProtocolEntityViewModel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SRCProtocolBindingList.h"

DEFINE_LOG_CATEGORY(LogRemoteControlProtocolWidgets);

void FRemoteControlProtocolWidgetsModule::StartupModule()
{
	FProtocolPanelStyle::Initialize();
}

void FRemoteControlProtocolWidgetsModule::ShutdownModule()
{
	FProtocolPanelStyle::Shutdown();
}

TSharedRef<SWidget> FRemoteControlProtocolWidgetsModule::GenerateDetailsForEntity(URemoteControlPreset* InPreset, const FGuid& InFieldId, const EExposedFieldType& InFieldType)
{
	check(InPreset);

	ResetProtocolBindingList();

	if (!InFieldId.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Currently only supports Properties
	if (const UScriptStruct* PropertyStruct = InPreset->GetExposedEntityType(InFieldId))
	{
		if (PropertyStruct->IsChildOf(FRemoteControlProperty::StaticStruct()))
		{
			TSharedRef<FProtocolEntityViewModel> EntityViewModel = FProtocolEntityViewModel::Create(InPreset, InFieldId);

			EntityViewModel->OnBindingAdded().AddRaw(this, &FRemoteControlProtocolWidgetsModule::OnBindingAdded);

			EntityViewModel->OnBindingRemoved().AddRaw(this, &FRemoteControlProtocolWidgetsModule::OnBindingRemoved);

			return SAssignNew(RCProtocolBindingList, SRCProtocolBindingList, EntityViewModel);
		}
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

FOnProtocolBindingAddedOrRemoved& FRemoteControlProtocolWidgetsModule::OnProtocolBindingAddedOrRemoved()
{
	return OnProtocolBindingAddedOrRemovedDelegate;
}

void FRemoteControlProtocolWidgetsModule::OnBindingAdded(TSharedRef<FProtocolBindingViewModel> InBindingViewModel)
{
	OnProtocolBindingAddedOrRemovedDelegate.Broadcast(ERCProtocolBinding::Added);
}

void FRemoteControlProtocolWidgetsModule::OnBindingRemoved(FGuid InBindingId)
{
	OnProtocolBindingAddedOrRemovedDelegate.Broadcast(ERCProtocolBinding::Removed);
}

IMPLEMENT_MODULE(FRemoteControlProtocolWidgetsModule, RemoteControlProtocolWidgets);
