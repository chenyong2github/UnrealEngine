// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "SoundGeneratorOutput.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MetasoundOutput.generated.h"

/**
 * Exposes the value of an output on a FMetasoundGenerator.
 */
USTRUCT(BlueprintType)
struct METASOUNDENGINE_API FMetaSoundOutput : public FSoundGeneratorOutput
{
	GENERATED_BODY()

	FMetaSoundOutput() = default;

	FMetaSoundOutput(const FMetaSoundOutput& Other);

	FMetaSoundOutput& operator=(const FMetaSoundOutput& Other);

	/**
	 * Has this output been initialized?
	 *
	 * @returns true if the output has been initialized, false otherwise
	 */
	bool IsValid() const;

	/**
	 * Get the type name of the output
	 *
	 * @returns The type name, or none if invalid
	 */
	FName GetTypeName() const;
	
	/**
	 * Initialize the output with some data.
	 *
	 * @tparam DataType - The type to use for initialization
	 * @param InitialValue - The initial value to use
	 */
	template<typename DataType>
	void Init(const DataType& InitialValue)
	{
		DataReference = MakeUnique<Metasound::FAnyDataReference>(Metasound::TDataWriteReference<DataType>::CreateNew());
		*DataReference->GetDataWriteReference<DataType>() = InitialValue;
	}

	/**
	 * Check if this output is of the given type
	 *
	 * @tparam DataType - The data type to check
	 * @returns true if it's the given type, false otherwise
	 */
	template<typename DataType>
	bool IsType() const
	{
		return IsValid() && Metasound::IsDataReferenceOfType<DataType>(*DataReference);
	}
	
	/**
	 * Get the value, for copyable registered Metasound data types
	 *
	 * @tparam DataType - The expected data type of the output
	 * @param Value - The value to use
	 * @returns true if the value was retrieved, false otherwise
	 */
	template<typename DataType>
	bool Get(DataType& Value) const
	{
		if (!IsType<DataType>())
		{
			return false;
		}

		Value = *DataReference->GetDataReadReference<DataType>();
		return true;
	}
	
	/**
	 * Set the value, for copyable registered Metasound data types
	 *
	 * @tparam DataType - The expected data type of the output
	 * @param Value - The value to use
	 * @returns true if the value was set, false otherwise
	 */
	template<typename DataType>
	bool Set(const DataType& Value)
	{
		if (!IsType<DataType>())
		{
			return false;
		}

		*DataReference->GetDataWriteReference<DataType>() = Value;
		return true;
	}

private:
	TUniquePtr<Metasound::FAnyDataReference> DataReference;
};

/**
 * Blueprint support for core types. If you want to support more core types, add them here.
 * If you want to support types introduced in other plugins, create a blueprint library in that plugin.
 */
UCLASS()
class UMetasoundOutputBlueprintAccess final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static bool IsFloat(const FMetaSoundOutput& Output);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static float GetFloat(const FMetaSoundOutput& Output, bool& Success);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static bool IsInt32(const FMetaSoundOutput& Output);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static int32 GetInt32(const FMetaSoundOutput& Output, bool& Success);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static bool IsBool(const FMetaSoundOutput& Output);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static bool GetBool(const FMetaSoundOutput& Output, bool& Success);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static bool IsString(const FMetaSoundOutput& Output);
	UFUNCTION(BlueprintCallable, Category="MetaSoundOutput")
	static FString GetString(const FMetaSoundOutput& Output, bool& Success);
};
