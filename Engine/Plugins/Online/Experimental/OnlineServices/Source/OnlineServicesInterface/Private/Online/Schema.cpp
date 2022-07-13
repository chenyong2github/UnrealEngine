// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Schema.h"

namespace UE::Online {


const FString LexToString(const FSchemaVariant& Variant)
{
	return Variant.GetString();
}

void LexFromString(FSchemaVariant& Variant, const TCHAR* InStr)
{
	Variant.Set(TEXT("")); // todo
}

int64 FSchemaVariant::GetInt64() const
{
	int64 AsInt64 = 0;
	if (VariantData.IsType<int64>())
	{
		AsInt64 = VariantData.Get<int64>();
	}
	else if (VariantData.IsType<bool>())
	{
		AsInt64 = static_cast<int64>(VariantData.Get<bool>());
	}
	else if (VariantData.IsType<double>())
	{
		AsInt64 = static_cast<int64>(VariantData.Get<double>());
	}
	else if (VariantData.IsType<FString>())
	{
		::LexFromString(AsInt64, *VariantData.Get<FString>());
	}
	return AsInt64;
}

double FSchemaVariant::GetDouble() const
{
	double AsDouble = 0;
	if (VariantData.IsType<double>())
	{
		AsDouble = VariantData.Get<double>();
	}
	else if (VariantData.IsType<FString>())
	{
		::LexFromString(AsDouble, *VariantData.Get<FString>());
	}
	else if (VariantData.IsType<int64>())
	{
		AsDouble = static_cast<double>(VariantData.Get<int64>());
	}
	else if (VariantData.IsType<bool>())
	{
		AsDouble = static_cast<double>(VariantData.Get<bool>());
	}
	return AsDouble;
}

bool FSchemaVariant::GetBoolean() const
{
	bool bAsBool = false;
	if (VariantData.IsType<bool>())
	{
		bAsBool = VariantData.Get<bool>();
	}
	else if (VariantData.IsType<FString>())
	{
		::LexFromString(bAsBool, *VariantData.Get<FString>());
	}
	else
	{
		bAsBool = GetInt64() != 0;
	}
	return bAsBool;
}

FString FSchemaVariant::GetString() const
{
	if (VariantData.IsType<FString>())
	{
		return VariantData.Get<FString>();
	}
	else if (VariantData.IsType<int64>())
	{
		return FString::Printf(TEXT("%" INT64_FMT), VariantData.Get<int64>());
	}
	else if (VariantData.IsType<bool>())
	{
		return ::LexToString(VariantData.Get<bool>());
	}
	else if (VariantData.IsType<double>())
	{
		return FString::Printf(TEXT("%f"), VariantData.Get<double>());
	}
	else
	{
		checkNoEntry();
	}
	return TEXT("");
}

bool FSchemaVariant::operator==(const FSchemaVariant& Other) const
{
	if (VariantData.GetIndex() != Other.VariantData.GetIndex())
	{
		return false;
	}

	switch (VariantData.GetIndex())
	{
	case FVariantType::IndexOfType<FString>():	return VariantData.Get<FString>() == Other.VariantData.Get<FString>();
	case FVariantType::IndexOfType<int64>():	return VariantData.Get<int64>() == Other.VariantData.Get<int64>();
	case FVariantType::IndexOfType<double>():	return VariantData.Get<double>() == Other.VariantData.Get<double>();

	default:checkNoEntry(); // Intentional fallthrough
	case FVariantType::IndexOfType<bool>():	return VariantData.Get<bool>() == Other.VariantData.Get<bool>();
	}
}

/* UE::Online */ }
