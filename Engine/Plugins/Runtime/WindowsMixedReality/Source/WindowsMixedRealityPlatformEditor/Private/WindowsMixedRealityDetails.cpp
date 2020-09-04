// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealityDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#include "WindowsMixedRealityRuntimeSettings.h"
#include "WindowsMixedRealityStatics.h"

#define LOCTEXT_NAMESPACE "FWindowsMixedRealityDetails"

TSharedRef<IDetailCustomization> FWindowsMixedRealityDetails::MakeInstance()
{
	return MakeShareable(new FWindowsMixedRealityDetails);
}

void FWindowsMixedRealityDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	statusTextWidget = SNew(STextBlock);

	UWindowsMixedRealityRuntimeSettings::Get()->OnRemotingStatusChanged.BindSP(this, &FWindowsMixedRealityDetails::SetStatusText);
	
	IDetailCategoryBuilder& remotingCategory = DetailBuilder.EditCategory(TEXT("Holographic Remoting"));
	remotingCategory.AddCustomRow(LOCTEXT("Connect Button", "Connect Button"))
		[
			SNew(SButton)
			.Text(LOCTEXT("Connect", "Connect"))
			.OnClicked_Raw(this, &FWindowsMixedRealityDetails::OnConnectButtonClicked)
			.IsEnabled_Raw(this, &FWindowsMixedRealityDetails::AreButtonsEnabled)
		];
	
	remotingCategory.AddCustomRow(LOCTEXT("Disconnect Button", "Disconnect Button"))
		[
			SNew(SButton)
			.Text(LOCTEXT("Disconnect", "Disconnect"))
			.OnClicked_Raw(this, &FWindowsMixedRealityDetails::OnDisconnectButtonClicked)
			.IsEnabled_Raw(this, &FWindowsMixedRealityDetails::AreButtonsEnabled)
		];
		
	remotingCategory.AddCustomRow(LOCTEXT("Status Text", "Status Text"))
		[
			statusTextWidget.ToSharedRef()
		];
}

FReply FWindowsMixedRealityDetails::OnConnectButtonClicked()
{
	UWindowsMixedRealityRuntimeSettings* settings = UWindowsMixedRealityRuntimeSettings::Get();

	FString ip = settings->RemoteHoloLensIP;
	int HoloLensType = settings->IsHoloLens1Remoting ? 1 : 2;
	UE_LOG(LogTemp, Log, TEXT("Editor connecting to remote HoloLens%i: %s"), HoloLensType, *ip);

	unsigned int bitrate = settings->MaxBitrate;

	FString Details;
	WindowsMixedReality::FWindowsMixedRealityStatics::ConnectToRemoteHoloLens(ip, bitrate, settings->IsHoloLens1Remoting);

	return FReply::Handled();
}

FReply FWindowsMixedRealityDetails::OnDisconnectButtonClicked()
{
	UE_LOG(LogTemp, Log, TEXT("Editor disconnecting from remote HoloLens"));
	WindowsMixedReality::FWindowsMixedRealityStatics::DisconnectFromRemoteHoloLens();

	return FReply::Handled();
}

bool FWindowsMixedRealityDetails::AreButtonsEnabled() const
{
	UWindowsMixedRealityRuntimeSettings* settings = UWindowsMixedRealityRuntimeSettings::Get();
	return settings->bEnableRemotingForEditor;
}

void FWindowsMixedRealityDetails::SetStatusText(FString message, FLinearColor statusColor)
{
	if (statusTextWidget == nullptr)
	{
		return;
	}

	statusTextWidget->SetText(FText::FromString(message));
	statusTextWidget->SetColorAndOpacity(FSlateColor(statusColor));
}

#undef LOCTEXT_NAMESPACE
