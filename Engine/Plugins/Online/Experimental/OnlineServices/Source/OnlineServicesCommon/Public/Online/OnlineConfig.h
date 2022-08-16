// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/ConfigCacheIni.h"
#include "Online/OnlineMeta.h"
#include "Algo/Transform.h"

namespace UE::Online {

/**
 * Interface for retrieving config values used by OnlineServices implementations
 * 
 * For all the GetValue functions, Section will correspond to the section in an ini file, but can be
 * interpreted in other ways by an IOnlineConfigProvider implementation to allow for OnlineServices
 * implementations to use an alternative configuration file.
 * 
 * By convention, all sections of the OnlineServices implementations will take the following format:
 *   OnlineServices.<ServicesProvider>.<OptionalInterface>.<OptionalOperation> <OptionalOverride>
 * For example, valid sections include the following:
 *   OnlineServices.Null
 *   OnlineServices.Null.Auth
 *   OnlineServices.Null.Auth.Login
 *   OnlineServices.Null.Auth.Login Prod
 * 
 * Implementations must at a minimum implement the FString and TArray<FString> GetValue. The others are
 * optional, and the default implementation will convert strings to the appropriate data type, but can 
 * be overridden in cases where the underlying configuration system stores integers, floats, etc directly
 * instead of as strings
 */
class ONLINESERVICESCOMMON_API IOnlineConfigProvider
{
public:
	virtual ~IOnlineConfigProvider() {}

	/**
	 * Get a FString value
	 * 
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value FString value read from the config. Will be unchanged if not present
	 * 
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, FString& Value) = 0;

	/**
	 * Get an value consisting of a TArray of FStrings
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of FStrings read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<FString>& Value) = 0;

	/**
	 * Get an FText value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value FText value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, FText& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			return FTextStringHelper::ReadFromBuffer(*StringValue, Value, Section) != nullptr;
		}
		return false;
	}

	/**
	 * Get an FName value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value FName value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, FName& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FName(*StringValue);
			return true;
		}
		return false;
	}

	/**
	 * Get a bool value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value bool value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, bool& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FCString::ToBool(*StringValue);
			return true;
		}
		return false;
	}

	/**
	 * Get an int32 value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value int32 value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, int32& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FCString::Strtoi(*StringValue, nullptr, 10);
			return true;
		}
		return false;
	}

	/**
	 * Get an int64 value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value int64 value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, int64& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FCString::Strtoi64(*StringValue, nullptr, 10);
			return true;
		}
		return false;
	}

	/**
	 * Get a uint64 value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value uint64 value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, uint64& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FCString::Strtoui64(*StringValue, nullptr, 10);
			return true;
		}
		return false;
	}

	/**
	 * Get a float value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value float value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, float& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FCString::Atof(*StringValue);
			return true;
		}
		return false;
	}

	/**
	 * Get a double value
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value double value read from the config. Will be unchanged if not present
	 *
	 * @return true if a value was read
	 */
	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, double& Value)
	{
		FString StringValue;
		if (GetValue(Section, Key, StringValue))
		{
			Value = FCString::Atod(*StringValue);
			return true;
		}
		return false;
	}

	/**
	 * Get a value consisting of a TArray of FText
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of FText read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<FText>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[Section](const FString& StringValue) -> FText
				{
					FText TextValue;
					FTextStringHelper::ReadFromBuffer(*StringValue, TextValue, Section);
					return TextValue;
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of FName
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of FName values read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<FName>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> const TCHAR*
				{
					return *StringValue;
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of bool
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of bool read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<bool>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> bool
				{
					return FCString::ToBool(*StringValue);
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of int32
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of int32 read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<int32>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> int32
				{
					return FCString::Strtoi(*StringValue, nullptr, 10);
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of int64
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of int64 read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<int64>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> int64
				{
					return FCString::Strtoi64(*StringValue, nullptr, 10);
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of uint64
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of uint64 read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<uint64>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> uint64
				{
					return FCString::Strtoui64(*StringValue, nullptr, 10);
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of float
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of float read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<float>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> float
				{
					return FCString::Atof(*StringValue);
				});
		}
		return Value.Num();
	}

	/**
	 * Get a value consisting of a TArray of double
	 *
	 * @param Section Section to read the value from
	 * @param Key Key in the section for the value
	 * @param Value Array of double read from the config. Will be empty if not present
	 *
	 * @return number of values in the array
	 */
	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<double>& Value)
	{
		Value.Empty();
		TArray<FString> StringArray;
		if (GetValue(Section, Key, StringArray) > 0)
		{
			Value.Reserve(StringArray.Num());
			Algo::Transform(StringArray, Value,
				[](const FString& StringValue) -> double
				{
					return FCString::Atod(*StringValue);
				});
		}
		return Value.Num();
	}

	template <typename T>
	std::enable_if_t<TModels<Meta::COnlineMetadataAvailable, T>::Value, bool> GetValue(const TCHAR* Section, const TCHAR* Key, T& Value);

	template <typename T>
	std::enable_if_t<TModels<Meta::COnlineMetadataAvailable, T>::Value, int32> GetValue(const TCHAR* Section, const TCHAR* Key, TArray<T>& Value);
};

/**
 * IOnlineConfigProvider implementation that uses GConfig to retrieve values
 */
class ONLINESERVICESCOMMON_API FOnlineConfigProviderGConfig : public IOnlineConfigProvider
{
public:
	FOnlineConfigProviderGConfig(const FString& InConfigFile)
		: ConfigFile(InConfigFile)
	{
	}

	virtual bool GetValue(const TCHAR* Section, const TCHAR* Key, FString& Value) override
	{
		return GConfig->GetValue(Section, Key, Value, ConfigFile);
	}

	virtual int32 GetValue(const TCHAR* Section, const TCHAR* Key, TArray<FString>& Value) override
	{
		return GConfig->GetValue(Section, Key, Value, ConfigFile);
	}

private:
	FString ConfigFile;
};

namespace Private {

// If needed, this can be specialized for additional types generically using LexFromString or explicit specialization
template <typename T>
auto LoadConfigValue(IOnlineConfigProvider& Provider, const TCHAR* Section, const TCHAR* Key, T& Value)
	-> std::enable_if_t<std::is_same_v<decltype(Provider.GetValue(Section, Key, Value)), bool>, bool>
{
	return Provider.GetValue(Section, Key, Value);
}

template <typename T>
auto LoadConfigValue(IOnlineConfigProvider& Provider, const TCHAR* Section, const TCHAR* Key, T& Value)
	-> std::enable_if_t<std::is_same_v<decltype(Provider.GetValue(Section, Key, Value)), int32>, bool>
{
	return Provider.GetValue(Section, Key, Value) > 0;
}

template <typename T>
auto LoadConfigValue(IOnlineConfigProvider& Provider, const TCHAR* Section, const TCHAR* Key, T& Value)
	-> std::enable_if_t<std::is_enum_v<T>, bool>
{
	FString StringValue;
	if (Provider.GetValue(Section, Key, StringValue))
	{
		using ::LexFromString;
		LexFromString(Value, *StringValue);
		return true;
	}
	return false;
}

template <typename T>
auto LoadConfigValue(IOnlineConfigProvider& Provider, const TCHAR* Section, const TCHAR* Key, TArray<T>& Value)
	-> std::enable_if_t<std::is_enum_v<T>, int32>
{
	Value.Empty();
	TArray<FString> StringArray;
	if (Provider.GetValue(Section, Key, StringArray))
	{
		Value.Reserve(StringArray.Num());
		Algo::Transform(StringArray, Value,
			[](const FString& StringValue) -> T
			{
				T EnumValue;
				using ::LexFromString;
				LexFromString(EnumValue, *StringValue);
				return EnumValue;
			});
	}
	return Value.Num();
}

/* Private */ }

/**
 * Populate a struct from a config provider. It will load from keys matching the struct member names in the section
 * Requires that the type metadata is specified for StructType
 * 
 * @param Provider The config provider
 * @param Section The section of the config to load the values from
 * @param The struct whose fields will be loaded from config
 * 
 * @return true if any values were loaded
 */
template <typename StructType>
bool LoadConfig(IOnlineConfigProvider& Provider, const FString& Section, StructType& Struct)
{
	bool bLoadedValue = false;
	Meta::VisitFields(Struct,
		[&Provider, &Section, &bLoadedValue](const TCHAR* FieldName, auto& Field)
		{
			bLoadedValue |= Private::LoadConfigValue(Provider, *Section, FieldName, Field);
		});
	return bLoadedValue;
}

template <typename StructType>
bool LoadConfig(IOnlineConfigProvider& Provider, const TArray<FString>& SectionHeiarchy, StructType& OutValue)
{
	bool bLoadedValue = false;
	for (const FString& Section : SectionHeiarchy)
	{
		StructType Value;
		if (LoadConfig(Provider, Section, Value))
		{
			bLoadedValue = true;
			OutValue = MoveTemp(Value);
		}
	}
	return bLoadedValue;
}

template <typename T>
std::enable_if_t<TModels<Meta::COnlineMetadataAvailable, T>::Value, bool> IOnlineConfigProvider::GetValue(const TCHAR* Section, const TCHAR* Key, T& OutValue)
{
	const FString AggregateSection = FString::Printf(TEXT("%s.%s"), Section, Key);
	bool bLoadedValue = false;
	T Value;
	Meta::VisitFields(Value,
		[this, &AggregateSection, &bLoadedValue](const TCHAR* FieldName, auto& Field)
		{
			bLoadedValue |= Private::LoadConfigValue(*this, *AggregateSection, FieldName, Field);
		});

	if (bLoadedValue)
	{
		OutValue = MoveTemp(Value);
	}

	return bLoadedValue;
}

template <typename T>
std::enable_if_t<TModels<Meta::COnlineMetadataAvailable, T>::Value, int32> IOnlineConfigProvider::GetValue(const TCHAR* Section, const TCHAR* Key, TArray<T>& Value)
{
	Value.Empty();
	TArray<FString> StringArray;
	if (GetValue(Section, Key, StringArray) > 0)
	{
		Value.Reserve(StringArray.Num());
		for (const FString& StringValue : StringArray)
		{
			FString PrecedingToken;
			const TCHAR* OldValue = *StringValue;

			if (FParse::Token(OldValue, PrecedingToken, true))
			{
				const FString AggregateSection = FString::Printf(TEXT("%s.%s.%s"), Section, Key, *StringValue);
				Meta::VisitFields(Value.Emplace_GetRef(),
					[this, &AggregateSection](const TCHAR* FieldName, auto& Field)
					{
						Private::LoadConfigValue(*this, *AggregateSection, FieldName, Field);
					});
			}
		}
	}
	return Value.Num();
}

/* UE::Online */ }
