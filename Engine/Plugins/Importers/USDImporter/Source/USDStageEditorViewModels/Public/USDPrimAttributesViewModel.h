// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDValueConversion.h"

#include "UsdWrappers/UsdStage.h"

#include "Templates/SharedPointer.h"
#include "Widgets/Views/SHeaderRow.h"

class FUsdPrimAttributesViewModel;

enum class EAttributeModelType : int32
{
	Metadata = 0,
	Attribute = 1,
	Relationship = 2,
	MAX = 3
};

namespace PrimAttributeColumnIds
{
	inline const FName TypeColumn = TEXT("PropertyType");
	inline const FName NameColumn = TEXT("PropertyName");
	inline const FName ValueColumn = TEXT("PropertyValue");
};

class USDSTAGEEDITORVIEWMODELS_API FUsdPrimAttributeViewModel : public TSharedFromThis< FUsdPrimAttributeViewModel >
{
public:
	explicit FUsdPrimAttributeViewModel( FUsdPrimAttributesViewModel* InOwner );

	// This member function is necessary because the no-RTTI slate module can't query USD for the available token options
	TArray< TSharedPtr< FString > > GetDropdownOptions() const;
	void SetAttributeValue( const UsdUtils::FConvertedVtValue& InValue );

public:
	EAttributeModelType Type;
	FString Label;
	UsdUtils::FConvertedVtValue Value;
	FString ValueRole;
	bool bReadOnly = false;

private:
	FUsdPrimAttributesViewModel* Owner;
};


class USDSTAGEEDITORVIEWMODELS_API FUsdPrimAttributesViewModel
{
public:
	template<typename T>
	void CreatePrimAttribute(EAttributeModelType Type, const FString& AttributeName, const T& Value, UsdUtils::EUsdBasicDataTypes UsdType, const FString& ValueRole = FString(), bool bReadOnly = false);
	void CreatePrimAttribute(EAttributeModelType Type, const FString& AttributeName, const UsdUtils::FConvertedVtValue& Value, bool bReadOnly = false);

	void SetPrimAttribute( const FString& AttributeName, const UsdUtils::FConvertedVtValue& Value );

	void Refresh( const UE::FUsdStageWeak& UsdStage, const TCHAR* PrimPath, float TimeCode );
	void Sort();

	UE::FUsdStageWeak GetUsdStage() const;
	FString GetPrimPath() const;

public:
	EColumnSortMode::Type CurrentSortMode = EColumnSortMode::Ascending;
	FName CurrentSortColumn = PrimAttributeColumnIds::TypeColumn;
	TArray< TSharedPtr< FUsdPrimAttributeViewModel > > PrimAttributes;

private:
	UE::FUsdStageWeak UsdStage;
	FString PrimPath;
};
