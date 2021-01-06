// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphPin.h"

enum class EAnimGraphAttributeBlend;

class SGraphPinPose : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinPose)	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

	// Struct used by connection drawing to draw attributes
	struct FAttributeInfo
	{
		FAttributeInfo(FName InAttribute, const FLinearColor& InColor, EAnimGraphAttributeBlend InBlend, int32 InSortOrder)
			: Attribute(InAttribute)
			, Color(InColor)
			, Blend(InBlend)
			, SortOrder(InSortOrder)
		{}

		FName Attribute;
		FLinearColor Color;
		EAnimGraphAttributeBlend Blend;
		int32 SortOrder;
	};

	// Get the attribute info used to draw connections. This varies based on LOD level.
	TArrayView<const FAttributeInfo> GetAttributeInfo() const;

	// Exposes the parent panel's zoom scalar for use when drawing links
	float GetZoomAmount() const;

private:
	void ReconfigureWidgetForAttributes();

	// Get tooltip text with attributes included
	FText GetAttributeTooltipText() const;

protected:
	//~ Begin SGraphPin Interface
	virtual const FSlateBrush* GetPinIcon() const override;
	//~ End SGraphPin Interface

	mutable const FSlateBrush* CachedImg_Pin_ConnectedHovered;
	mutable const FSlateBrush* CachedImg_Pin_DisconnectedHovered;

	TArray<FAttributeInfo> AttributeInfos;
};
