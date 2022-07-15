// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"

namespace UE::Online {

using FSchemaId = FName;
using FSchemaAttributeId = FName;

class ONLINESERVICESINTERFACE_API FSchemaVariant final
{
public:
	using FVariantType = TVariant<FString, int64, double, bool>;
	FSchemaVariant() = default;
	FSchemaVariant(const FSchemaVariant& InOther) = default;
	FSchemaVariant(FSchemaVariant&& InOther) = default;
	FSchemaVariant& operator=(FSchemaVariant&&) = default;
	FSchemaVariant& operator=(const FSchemaVariant&) = default;

	template<typename ValueType>
	FSchemaVariant(const ValueType& InData) { Set(InData); }
	template<typename ValueType>
	FSchemaVariant(ValueType&& InData) { Set(MoveTemp(InData)); }
	void Set(const TCHAR* AsString) { VariantData.Emplace<FString>(AsString); }
	void Set(const FString& AsString) { VariantData.Emplace<FString>(AsString); }
	void Set(FString&& AsString) { VariantData.Emplace<FString>(MoveTemp(AsString)); }
	void Set(int64 AsInt) { VariantData.Emplace<int64>(AsInt); }
	void Set(double AsDouble) { VariantData.Emplace<double>(AsDouble); }
	void Set(bool bAsBool) { VariantData.Emplace<bool>(bAsBool); }
	int64 GetInt64() const;
	double GetDouble() const;
	bool GetBoolean() const;
	FString GetString() const;

	bool operator==(const FSchemaVariant& Other) const;
	bool operator!=(const FSchemaVariant& Other) const { return !(*this == Other); }
public:
	FVariantType VariantData;
};

// Don't allow implicit conversion to FSchemaVariant when calling LexToString.
template <typename T> class FLexToStringAdaptor;
template <>
class FLexToStringAdaptor<FSchemaVariant>
{
public:
	FLexToStringAdaptor(const FSchemaVariant& SchemaVariant)
		: SchemaVariant(SchemaVariant)
	{
	}

	const FSchemaVariant& SchemaVariant;
};

ONLINESERVICESINTERFACE_API const FString LexToString(const FLexToStringAdaptor<FSchemaVariant>& Adaptor);
ONLINESERVICESINTERFACE_API void LexFromString(FSchemaVariant& Variant, const TCHAR* InStr);

/* UE::Online */ }
