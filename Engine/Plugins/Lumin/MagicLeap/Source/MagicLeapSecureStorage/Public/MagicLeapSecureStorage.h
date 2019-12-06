// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapSecureStorage.generated.h"

DEFINE_LOG_CATEGORY_STATIC(LogSecureStorage, Display, All);

/**
  Function library for the Magic Leap Secure Storage API.
  Currently supports bool, uint8, int32, float, FString, FVector, FRotator and FTransform via Blueprints.
  Provides a template function for any non specialized types to be used via C++.
*/
UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPSECURESTORAGE_API UMagicLeapSecureStorage : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	  Stores the boolean under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureBool(const FString& Key, bool DataToStore);

	/**
	  Stores the byte (uint8) under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureByte(const FString& Key, uint8 DataToStore);

	/**
	  Stores the integer (int32) under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureInt(const FString& Key, int32 DataToStore);

	/**
	  Stores the 64 bit integer under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureInt64(const FString& Key, int64 DataToStore);

	/**
	  Stores the float under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureFloat(const FString& Key, float DataToStore);

	/**
	  Stores the string under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureString(const FString& Key, const FString& DataToStore);

	/**
	  Stores the vector under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by `PutSecureArray`"), Category = "SecureStorage|MagicLeap")
	static bool PutSecureVector(const FString& Key, const FVector& DataToStore);

	/**
	  Stores the rotator under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by `PutSecureArray`"), Category = "SecureStorage|MagicLeap")
	static bool PutSecureRotator(const FString& Key, const FRotator& DataToStore);

	/**
	  Stores the transform under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by `PutSecureArray`"), Category = "SecureStorage|MagicLeap")
	static bool PutSecureTransform(const FString& Key, const FTransform& DataToStore);

	/**
	  Retrieves the boolean associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureBool(const FString& Key, bool& DataToRetrieve);

	/**
	  Retrieves the byte (uint8) associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureByte(const FString& Key, uint8& DataToRetrieve);

	/**
	  Retrieves the integer (int32) associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureInt(const FString& Key, int32& DataToRetrieve);

	/**
	  Retrieves the 64 bit integer associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureInt64(const FString& Key, int64& DataToRetrieve);

	/**
	  Retrieves the float associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureFloat(const FString& Key, float& DataToRetrieve);

	/**
	  Retrieves the string associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureString(const FString& Key, FString& DataToRetrieve);

	/**
	  Retrieves the vector associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by `GetSecureArray`"), Category = "SecureStorage|MagicLeap")
	static bool GetSecureVector(const FString& Key, FVector& DataToRetrieve);

	/**
	  Retrieves the rotator associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction, DeprecationMessage = "This function has been replaced by `GetSecureArray`"), Category = "SecureStorage|MagicLeap")
	static bool GetSecureRotator(const FString& Key, FRotator& DataToRetrieve);

	/**
	  Retrieves the transform associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, meta=(DeprecatedFunction, DeprecationMessage="This function has been replaced by `GetSecureArray`"),Category = "SecureStorage|MagicLeap")
	static bool GetSecureTransform(const FString& Key, FTransform& DataToRetrieve);

	/**
	  Stores the USaveGame object under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   ObjectToStore The USaveGame object to serialize and store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool PutSecureSaveGame(const FString& Key, USaveGame* ObjectToStore);

	/**
	  Stores the data under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (ArrayParm = "DataToStore"), Category = "SecureStorage|MagicLeap")
	static bool PutSecureArray(const FString& Key, const TArray<int32>& DataToStore);

	DECLARE_FUNCTION(execPutSecureArray)
	{

		P_GET_PROPERTY(FStrProperty, Key);

		Stack.StepCompiledIn<FArrayProperty>(nullptr);
		void* ArrayAddress = Stack.MostRecentPropertyAddress;
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);

		if (!ArrayProperty)
		{
			Stack.bArrayContextFailed = true;
			return;
		}

		P_FINISH;

		P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = GenericPutSecureArray(Key, ArrayProperty, ArrayAddress);
		P_NATIVE_END;

	}

	static bool GenericPutSecureArray(const FString& Key, const FArrayProperty* ArrayProperty, void* TargetArray);

	/**
	  Retrieves a USaveGame object associated with the specified key.
	  @param   Key The string key to look for.
	  @param   ObjectToRetrieve Reference to a USaveGame object that will be populated with the serialized data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool GetSecureSaveGame(const FString& Key, USaveGame*& ObjectToRetrieve);

	/**
	  Retrieves an array associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to an array that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (ArrayParm = "DataToRetrieve"), Category = "SecureStorage|MagicLeap")
	static bool GetSecureArray(const FString& Key, TArray<int32>& DataToRetrieve);

	DECLARE_FUNCTION(execGetSecureArray)
	{

		P_GET_PROPERTY(FStrProperty, Key);

		Stack.StepCompiledIn<FArrayProperty>(nullptr);
		void* ArrayAddress = Stack.MostRecentPropertyAddress;
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Stack.MostRecentProperty);

		if (!ArrayProperty)
		{
			Stack.bArrayContextFailed = true;
			return;
		}

		P_FINISH;

		P_NATIVE_BEGIN;
		*static_cast<bool*>(RESULT_PARAM) = GenericGetSecureArray(Key, ArrayProperty, ArrayAddress);
		P_NATIVE_END;

	}

	static bool GenericGetSecureArray(const FString& Key, FArrayProperty* ArrayProperty, void* TargetArray);

	/**
	  Template function to store the data under the specified key. An existing key would be overwritten.
	  @param   Key String key associated with the data.
	  @param   DataToStore The data to store.
	  @return  True if the data was stored successfully, false otherwise.
	*/
	template<class T>
	static bool PutSecureBlob(const FString& Key, const T* DataToStore);

	/**
	  Template function to retrieve the data associated with the specified key.
	  @param   Key The string key to look for.
	  @param   DataToRetrieve Reference to the variable that will be populated with the stored data.
	  @return  True if the key was found and output parameter was successfully populated with the data, false otherwise.
	*/
	template<class T>
	static bool GetSecureBlob(const FString& Key, T& DataToRetrieve);

	/**
	  Deletes the data associated with the specified key.
	  @param   Key The string key of the data to delete.
	  @return  True if the data was deleted successfully or did not exist altogether, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "SecureStorage|MagicLeap")
	static bool DeleteSecureData(const FString& Key);

private:
	static bool PutSecureBlobImpl(const FString& Key, const uint8* DataToStore, size_t DataTypeSize);

	static bool GetSecureBlobImpl(const FString& Key, uint8*& DataToRetrieve, size_t& DataTypeSize);

	static void FreeBlobBufferImpl(uint8* Buffer);
};

template<class T>
bool UMagicLeapSecureStorage::PutSecureBlob(const FString& Key, const T* DataToStore)
{
	return UMagicLeapSecureStorage::PutSecureBlobImpl(Key, reinterpret_cast<const uint8*>(DataToStore), sizeof(T));
}

template<class T>
bool UMagicLeapSecureStorage::GetSecureBlob(const FString& Key, T& DataToRetrieve)
{

	uint8* Data;
	size_t Size;
	bool bSuccess = UMagicLeapSecureStorage::GetSecureBlobImpl(Key, Data, Size);

	if (!bSuccess) 
	{
		return false;
	}

	if (Size != sizeof(T))
	{
		UE_LOG(LogSecureStorage, Error, TEXT("Size of blob data %s does not match the size of requested data type. Requested size = %d vs Actual size = %d"), *Key, sizeof(T), Size);
		FreeBlobBufferImpl(Data);
		return false;
	}

	DataToRetrieve = *reinterpret_cast<T*>(Data);
	return bSuccess;
}
