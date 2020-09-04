// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UsdWrappers/UsdStage.h"

enum class EPrimPropertyWidget : uint8
{
	Text,
	Dropdown,
};

class FUsdPrimAttributesViewModel;

class USDSTAGEEDITORVIEWMODELS_API FUsdPrimAttributeViewModel : public TSharedFromThis< FUsdPrimAttributeViewModel >
{
public:
	explicit FUsdPrimAttributeViewModel( FUsdPrimAttributesViewModel* InOwner );
	TArray< TSharedPtr< FString > > GetDropdownOptions() const;

	void SetAttributeValue( const FString& InValue );

public:
	UE::FUsdStage UsdStage;

	FString Label;
	FString Value;
	EPrimPropertyWidget WidgetType = EPrimPropertyWidget::Text;

private:
	FUsdPrimAttributesViewModel* Owner;
};

class USDSTAGEEDITORVIEWMODELS_API FUsdPrimAttributesViewModel
{
public:
	void SetPrimAttribute( const FString& AttributeName, const FString& Value );

	void Refresh( const TCHAR* InPrimPath, float TimeCode );

public:
	UE::FUsdStage UsdStage;

	TArray< TSharedPtr< FUsdPrimAttributeViewModel > > PrimAttributes;

private:
	FString PrimPath;
};