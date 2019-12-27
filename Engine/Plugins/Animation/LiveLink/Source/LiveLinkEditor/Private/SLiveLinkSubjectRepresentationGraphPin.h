// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphPin.h"

#include "LiveLinkRole.h"

struct FLiveLinkSubjectRepresentation;

class SComboButton;

class SLiveLinkSubjectRepresentationGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SLiveLinkSubjectRepresentationGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

private:
	FLiveLinkSubjectRepresentation GetValue() const;
	void SetValue(FLiveLinkSubjectRepresentation NewValue);

	FLiveLinkSubjectRepresentation SubjectRepresentation;
};
