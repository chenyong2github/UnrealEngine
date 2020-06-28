// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class BlendSettingsCustomization : public IDetailCustomization

{
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	static TSharedRef<IDetailCustomization> MakeInstance();

private:
	bool CanCreateMaterialBlend() const;
	FReply CreateMaterialBlend();

};