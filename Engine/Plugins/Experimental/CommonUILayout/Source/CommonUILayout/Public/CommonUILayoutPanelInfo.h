// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "UObject/NameTypes.h"
#include "UObject/SoftObjectPtr.h"

struct FCommonUILayoutPanelInfo
{
	static const int32 DEFAULT_ZORDER = 1000;

public:
	FCommonUILayoutPanelInfo() {}
	FCommonUILayoutPanelInfo(const TSoftClassPtr<UUserWidget>& InWidgetClass, const FName& InUniqueID, const int32 InZOrder = DEFAULT_ZORDER, const bool bInIsUsingSafeZone = true)
		: WidgetClass(InWidgetClass)
		, WidgetInstance(nullptr)
		, ZOrder(InZOrder)
		, UniqueID(InUniqueID)
		, bIsUsingSafeZone(bInIsUsingSafeZone)
	{}

	bool operator==(const FCommonUILayoutPanelInfo& Other) const
	{
		// Only Widget Class & Unique ID are included in the equally operator on purpose.
		return WidgetClass == Other.WidgetClass && UniqueID == Other.UniqueID;
	}

	bool IsValid() const
	{
		return !WidgetClass.IsNull() || WidgetInstance.IsValid(false);
	}

	FString ToString() const
	{
		TStringBuilder<2048> StringBuilder;
		StringBuilder << (WidgetInstance.IsValid() ? WidgetInstance->GetName() : WidgetClass.ToString());
		StringBuilder << TEXT(" [Z: ") << ZOrder << TEXT("]");
		return StringBuilder.ToString();
	}

	TSoftClassPtr<UUserWidget> WidgetClass;
	TWeakObjectPtr<UUserWidget> WidgetInstance; // This will only be set for widgets provided pre-constructed (ie: RootViewportLayout & StateContent)
	int32 ZOrder = DEFAULT_ZORDER;
	FName UniqueID;
	bool bIsUsingSafeZone = true;
};

inline uint32 GetTypeHash(const FCommonUILayoutPanelInfo& Info)
{
	return GetTypeHash(Info.WidgetClass) ^ GetTypeHash(Info.UniqueID);
}
