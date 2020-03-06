// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Interface.h"
#include "Internationalization/Internationalization.h"
#include "IDataSourceFilterInterface.generated.h"

UINTERFACE(Blueprintable)
class SOURCEFILTERINGCORE_API UDataSourceFilterInterface : public UInterface
{
	GENERATED_BODY()
};

/** Interface used for implementing Engine and UnrealInsights versions respectively UDataSourceFilter and UTraceDataSourceFilter */
class SOURCEFILTERINGCORE_API IDataSourceFilterInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = TraceSourceFiltering)
	void GetDisplayText(FText& OutDisplayText) const;
	void GetDisplayText_Implementation(FText& OutDisplayText) const
	{
		return GetDisplayText_Internal(OutDisplayText);
	}

	UFUNCTION(BlueprintNativeEvent, Category = TraceSourceFiltering)
	void GetToolTipText(FText& OutDisplayText) const;
	void GetToolTipText_Implementation(FText& OutDisplayText) const
	{
		return GetToolTipText_Internal(OutDisplayText);
	}

	virtual void SetEnabled(bool bState) = 0;
	virtual bool IsEnabled() const = 0;
protected:
	virtual void GetDisplayText_Internal(FText& OutDisplayText) const { OutDisplayText = NSLOCTEXT("IDataSourceFilterInterface", "GetDisplayTextNotImplementedText", "IDataSourceFilterInterface::GetDisplayText_Internal not overidden (missing implementation)"); }
	virtual void GetToolTipText_Internal(FText& OutDisplayText) const { GetDisplayText_Internal(OutDisplayText); }
};