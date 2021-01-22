// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/TVariant.h"
#include "Templates/SharedPointer.h"

#include "UsdWrappers/UsdStage.h"

namespace USDViewModels
{
	enum class EUsdBasicDataTypes
	{
		None,
		Bool,
		Uchar,
		Int,
		Uint,
		Int64,
		Uint64,
		Half,
		Float,
		Double,
		Timecode,
		String,
		Token,
		Asset,
		Matrix2d,
		Matrix3d,
		Matrix4d,
		Quatd,
		Quatf,
		Quath,
		Double2,
		Float2,
		Half2,
		Int2,
		Double3,
		Float3,
		Half3,
		Int3,
		Double4,
		Float4,
		Half4,
		Int4,
	};

	using FPrimPropertyValueComponent = TVariant<bool, uint8, int32, uint32, int64, uint64, float, double, FString>;

	struct USDSTAGEEDITORVIEWMODELS_API FPrimPropertyValue
	{
		TArray<FPrimPropertyValueComponent> Components;
		EUsdBasicDataTypes SourceType = EUsdBasicDataTypes::None;
		FString SourceRole;
	};
};

class FUsdPrimAttributesViewModel;

class USDSTAGEEDITORVIEWMODELS_API FUsdPrimAttributeViewModel : public TSharedFromThis< FUsdPrimAttributeViewModel >
{
public:
	explicit FUsdPrimAttributeViewModel( FUsdPrimAttributesViewModel* InOwner );

	// This member function is necessary because the no-RTTI slate module can't query USD for the available token options
	TArray< TSharedPtr< FString > > GetDropdownOptions() const;
	void SetAttributeValue( const USDViewModels::FPrimPropertyValue& InValue );

public:
	UE::FUsdStage UsdStage;

	FString Label;
	USDViewModels::FPrimPropertyValue Value;
	bool bReadOnly = false;

private:
	FUsdPrimAttributesViewModel* Owner;
};


class USDSTAGEEDITORVIEWMODELS_API FUsdPrimAttributesViewModel
{
public:
	template<typename T>
	void CreatePrimAttribute( const FString& AttributeName, const T& Value, USDViewModels::EUsdBasicDataTypes UsdType, const FString& SourceRole = FString(), bool bReadOnly = false );
	void CreatePrimAttribute( const FString& AttributeName, const USDViewModels::FPrimPropertyValue& Value, bool bReadOnly = false );

	void SetPrimAttribute( const FString& AttributeName, const USDViewModels::FPrimPropertyValue& Value );

	void Refresh( const TCHAR* InPrimPath, float TimeCode );

public:
	UE::FUsdStage UsdStage;
	TArray< TSharedPtr< FUsdPrimAttributeViewModel > > PrimAttributes;

private:
	FString PrimPath;
};
