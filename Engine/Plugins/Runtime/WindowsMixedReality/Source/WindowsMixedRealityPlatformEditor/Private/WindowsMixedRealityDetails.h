// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "IDetailCustomNodeBuilder.h"
#include "PropertyHandle.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Widgets/Text/STextBlock.h"

class FWindowsMixedRealityDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnConnectButtonClicked();
	FReply OnDisconnectButtonClicked();
	bool AreButtonsEnabled() const;
	
	void SetStatusText(FString message, FLinearColor statusColor);

	FString StatusText;
	TSharedPtr<STextBlock> statusTextWidget;
};