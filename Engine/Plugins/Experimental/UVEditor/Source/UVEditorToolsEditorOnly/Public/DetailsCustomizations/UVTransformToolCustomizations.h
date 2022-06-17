// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Templates/SharedPointer.h"

class IPropertyHandle;
class FPropertyRestriction;
class UUVEditorTransformTool;

// Customization for UUVEditorUVTransformProperties
class FUVEditorUVTransformToolDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	TWeakObjectPtr<UUVEditorTransformTool> TargetTool;
	void BuildQuickTranslateMenu(IDetailLayoutBuilder& DetailBuilder);
	void BuildQuickRotateMenu(IDetailLayoutBuilder& DetailBuilder);

	FReply OnQuickTranslate(float TranslationValue, const FVector2D& Direction);
	FReply OnQuickRotate(float RotationValue);
};


