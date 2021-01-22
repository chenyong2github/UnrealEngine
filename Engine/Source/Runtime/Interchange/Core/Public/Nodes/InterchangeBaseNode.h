// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeBaseNode.generated.h"

/**
 * Internal Helper to get set custom property for class that derive from UInterchangeBaseNode. This is use by the macro IMPLEMENT_UOD_ATTRIBUTE.
 */
namespace InterchangePrivateNodeBase
{
	/**
	 * Retrieve a custom attribute if the attribute exist
	 *
	 * @param Attributes - The attribute storage you want to query the custom attribute
	 * @param AttributeKey - The storage key for the attribute
	 * @param OperationName - The name of the operation in case there is an error
	 * @param OutAttributeValue - This is where we store the value we retrieve from the storage
	 *
	 * @return - return true if the attribute exist in the storage and was query without error.
	 *           return false if the attribute do not exist or there is an error retriving it from the Storage
	 */
	template<typename ValueType>
	bool GetCustomAttribute(const UE::Interchange::FAttributeStorage& Attributes, const UE::Interchange::FAttributeKey& AttributeKey, const FString& OperationName, ValueType& OutAttributeValue)
	{
		if (!Attributes.ContainAttribute(AttributeKey))
		{
			return false;
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<ValueType> AttributeHandle = Attributes.GetAttributeHandle<ValueType>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			return false;
		}
		UE::Interchange::EAttributeStorageResult Result = AttributeHandle.Get(OutAttributeValue);
		if (!UE::Interchange::IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::LogAttributeStorageErrors(Result, OperationName, AttributeKey);
			return false;
		}
		return true;
	}

	/**
	 * Add or update a custom attribute value in the specified storage
	 *
	 * @param Attributes - The attribute storage you want to add or update the custom attribute
	 * @param AttributeKey - The storage key for the attribute
	 * @param OperationName - The name of the operation in case there is an error
	 * @param AttributeValue - The value we want to add or update in the storage
	 */
	template<typename ValueType>
	bool SetCustomAttribute(UE::Interchange::FAttributeStorage& Attributes, const UE::Interchange::FAttributeKey& AttributeKey, const FString& OperationName, const ValueType& AttributeValue)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(AttributeKey, AttributeValue);
		if (!UE::Interchange::IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::LogAttributeStorageErrors(Result, OperationName, AttributeKey);
			return false;
		}
		return true;
	}
}

/**
 * Use those macro to create Get/Set/ApplyToAsset for an attribute you want to support in your custom UOD node that derive from UInterchangeBaseNode.
 * The attribute key will be declare under private access modifier and the functions are declare under public access modifier.
 * So the class access modifier is public after calling this macro.
 *
 * @note - The Get will return false if the attribute was never set.
 * @note - The Set will add the attribute to the storage or update the value if the storage already has this attribute.
 * @note - The Apply will set a member variable of the AssetType instance. It will apply the value only if the storage contain the attribute key
 *
 * @param AttributeName - The name of the Get/Set functions. Example if you pass Foo you will end up with GetFoo and SetFoo function
 * @param AttributeType - This is to specify the type of the attribute. bool, float, FString... anything supported by the FAttributeStorage
 * @param AssetType - This is the asset you want to apply the storage value"
 * @param EnumType - Optional, specify it only if the AssetType member is an enum so we can type cast it in the apply function (we use uint8 to store the enum value)"
 */
#define IMPLEMENT_NODE_ATTRIBUTE_KEY(AttributeName)																		\
const UE::Interchange::FAttributeKey Macro_Custom##AttributeName##Key = UE::Interchange::FAttributeKey(TEXT(#AttributeName));	\

#if WITH_ENGINE
#define IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AttributeName, AttributeType, AssetType, EnumType)	\
bool ApplyCustom##AttributeName##ToAsset(UObject* Asset) const										\
{																									\
	if (!Asset)																						\
	{																								\
		return false;																				\
	}																								\
	AssetType* TypedObject = Cast<AssetType>(Asset);												\
	if (!TypedObject)																				\
	{																								\
		return false;																				\
	}																								\
	AttributeType ValueData;																		\
	if (GetCustom##AttributeName(ValueData))														\
	{																								\
		TypedObject->AttributeName = EnumType(ValueData);											\
		return true;																				\
	}																								\
	return false;																					\
}																									\
																									\
bool FillCustom##AttributeName##FromAsset(UObject* Asset)											\
{																									\
	if (!Asset)																						\
	{																								\
		return false;																				\
	}																								\
	AssetType* TypedObject = Cast<AssetType>(Asset);												\
	if (!TypedObject)																				\
	{																								\
		return false;																				\
	}																								\
	if (SetCustom##AttributeName((AttributeType)TypedObject->AttributeName, false))					\
	{																								\
		return true;																				\
	}																								\
	return false;																					\
}

#else //#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(AttributeName, AttributeType, AssetType, EnumType)

#endif //#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_GETTER(AttributeName, AttributeType)																					\
	FString OperationName = GetTypeName() + TEXT(".Get" #AttributeName);																				\
	return InterchangePrivateNodeBase::GetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AttributeName, AttributeType)															\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																				\
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER(NodeClassName, AttributeName, AttributeType, AssetType)														\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																			\
	if(InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue))	\
	{																																				\
		if(bAddApplyDelegate)																														\
		{																																			\
			TArray<UE::Interchange::FApplyAttributeToAsset>& Delegates = ApplyCustomAttributeDelegates.FindOrAdd(AssetType::StaticClass());			\
			Delegates.Add(UE::Interchange::FApplyAttributeToAsset::CreateUObject(this, &NodeClassName::ApplyCustom##AttributeName##ToAsset));		\
			TArray<UE::Interchange::FFillAttributeToAsset>& FillDelegates = FillCustomAttributeDelegates.FindOrAdd(AssetType::StaticClass());		\
			FillDelegates.Add(UE::Interchange::FFillAttributeToAsset::CreateUObject(this, &NodeClassName::FillCustom##AttributeName##FromAsset));	\
		}																																			\
		return true;																																\
	}																																				\
	return false;

#else //#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER(NodeClassName, AttributeName, AttributeType, AssetType)															\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																				\
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#endif //#if WITH_ENGINE


//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		DECLARE_DELEGATE_RetVal_OneParam(bool, FApplyAttributeToAsset, UObject*);
		DECLARE_DELEGATE_RetVal_OneParam(bool, FFillAttributeToAsset, UObject*);

		class FNameAttributeArrayHelper
		{
		public:
			~FNameAttributeArrayHelper()
			{
				Attributes = nullptr;
				KeyCount = NAME_None;
			}

			void Initialize(const TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe>& InAttributes, const FString& BaseKeyName)
			{
				Attributes = InAttributes;
				check(Attributes.IsValid());
				FString BaseTryName = TEXT("__") + BaseKeyName;
				KeyCount = BaseTryName;
			}

			int32 GetCount() const
			{
				//The Class must be initialise properly before we can use it
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributePtr = Attributes.Pin();
				if (!ensure(AttributePtr.IsValid()))
				{
					return 0;
				}
				int32 NameCount = 0;
				if (AttributePtr->ContainAttribute(GetKeyCount()))
				{
					FAttributeStorage::TAttributeHandle<int32> Handle = AttributePtr->GetAttributeHandle<int32>(GetKeyCount());
					if (Handle.IsValid())
					{
						Handle.Get(NameCount);
					}
				}
				return NameCount;
			}

			void GetNames(TArray<FString>& OutNames) const
			{
				//The Class must be initialise properly before we can use it
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributePtr = Attributes.Pin();
				if (!ensure(AttributePtr.IsValid()))
				{
					OutNames.Empty();
					return;
				}
				int32 NameCount = 0;
				if (!AttributePtr->ContainAttribute(GetKeyCount()))
				{
					OutNames.Empty();
					return;
				}

				//Reuse as much memory we can to avoid allocation
				OutNames.Reset(NameCount);

				FAttributeStorage::TAttributeHandle<int32> Handle = AttributePtr->GetAttributeHandle<int32>(GetKeyCount());
				if (!Handle.IsValid())
				{
					return;
				}
				Handle.Get(NameCount);
				for (int32 NameIndex = 0; NameIndex < NameCount; ++NameIndex)
				{
					FAttributeKey DepIndexKey = GetIndexKey(NameIndex);
					FAttributeStorage::TAttributeHandle<FString> HandleName = AttributePtr->GetAttributeHandle<FString>(DepIndexKey);
					if (!HandleName.IsValid())
					{
						continue;
					}
					FString& OutName = OutNames.AddDefaulted_GetRef();
					HandleName.Get(OutName);
				}
			}

			bool AddName(const FString& Name)
			{
				//The Class must be initialise properly before we can use it
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributePtr = Attributes.Pin();
				if (!ensure(AttributePtr.IsValid()))
				{
					return false;
				}

				if (!AttributePtr->ContainAttribute(GetKeyCount()))
				{
					const int32 DependencyCount = 0;
					EAttributeStorageResult Result = AttributePtr->RegisterAttribute<int32>(GetKeyCount(), DependencyCount);
					if (!IsAttributeStorageResultSuccess(Result))
					{
						LogAttributeStorageErrors(Result, TEXT("FNameAttributeArrayHelper.AddName"), GetKeyCount());
						return false;
					}
				}
				FAttributeStorage::TAttributeHandle<int32> Handle = AttributePtr->GetAttributeHandle<int32>(GetKeyCount());
				if (!ensure(Handle.IsValid()))
				{
					return false;
				}
				int32 NameIndex = 0;
				Handle.Get(NameIndex);
				FAttributeKey NameIndexKey = GetIndexKey(NameIndex);
				//Increment the name counter
				NameIndex++;
				Handle.Set(NameIndex);

				EAttributeStorageResult AddNameResult = AttributePtr->RegisterAttribute<FString>(NameIndexKey, Name);
				if (!IsAttributeStorageResultSuccess(AddNameResult))
				{
					LogAttributeStorageErrors(AddNameResult, TEXT("FNameAttributeArrayHelper.AddName"), NameIndexKey);
					return false;
				}
				return true;
			}

			bool RemoveName(const FString& NameToDelete)
			{
				//The Class must be initialise properly before we can use it
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributePtr = Attributes.Pin();
				if (!ensure(AttributePtr.IsValid()))
				{
					return false;
				}

				int32 NameCount = 0;
				if (!AttributePtr->ContainAttribute(GetKeyCount()))
				{
					return false;
				}
				FAttributeStorage::TAttributeHandle<int32> Handle = AttributePtr->GetAttributeHandle<int32>(GetKeyCount());
				if (!Handle.IsValid())
				{
					return false;
				}
				Handle.Get(NameCount);
				bool DecrementKey = false;
				for (int32 NameIndex = 0; NameIndex < NameCount; ++NameIndex)
				{
					FAttributeKey DepIndexKey = GetIndexKey(NameIndex);
					FAttributeStorage::TAttributeHandle<FString> HandleName = AttributePtr->GetAttributeHandle<FString>(DepIndexKey);
					if (!HandleName.IsValid())
					{
						continue;
					}
					FString Name;
					HandleName.Get(Name);
					if (Name == NameToDelete)
					{
						//Remove this entry
						AttributePtr->UnregisterAttribute(DepIndexKey);
						Handle.Set(NameCount - 1);
						//We have to rename the key for all the next item
						DecrementKey = true;
					}
					else if (DecrementKey)
					{
						FAttributeKey NewDepIndexKey = GetIndexKey(NameIndex - 1);
						EAttributeStorageResult UnregisterResult = AttributePtr->UnregisterAttribute(DepIndexKey);
						if (IsAttributeStorageResultSuccess(UnregisterResult))
						{
							EAttributeStorageResult RegisterResult = AttributePtr->RegisterAttribute<FString>(NewDepIndexKey, Name);
							if (!IsAttributeStorageResultSuccess(RegisterResult))
							{
								LogAttributeStorageErrors(RegisterResult, TEXT("FNameAttributeArrayHelper.RemoveName"), NewDepIndexKey);
							}
						}
						else
						{
							LogAttributeStorageErrors(UnregisterResult, TEXT("FNameAttributeArrayHelper.RemoveName"), DepIndexKey);
						}

						//Avoid doing more code in the for since the HandleName is now invalid
						continue;
					}
				}
				return true;
			}

			bool RemoveAllNames()
			{
				//The Class must be initialise properly before we can use it
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributePtr = Attributes.Pin();
				if (!ensure(AttributePtr.IsValid()))
				{
					return false;
				}

				int32 NameCount = 0;
				if (!AttributePtr->ContainAttribute(GetKeyCount()))
				{
					return false;
				}
				FAttributeStorage::TAttributeHandle<int32> HandleCount = AttributePtr->GetAttributeHandle<int32>(GetKeyCount());
				if (!HandleCount.IsValid())
				{
					return false;
				}
				HandleCount.Get(NameCount);
				//Remove all attribute one by one
				for (int32 NameIndex = 0; NameIndex < NameCount; ++NameIndex)
				{
					FAttributeKey DepIndexKey = GetIndexKey(NameIndex);
					AttributePtr->UnregisterAttribute(DepIndexKey);
				}
				//Make sure Count is zero
				NameCount = 0;
				HandleCount.Set(NameCount);
				return true;
			}

		private:
			TWeakPtr<FAttributeStorage, ESPMode::ThreadSafe> Attributes = nullptr;

			FAttributeKey KeyCount; //Assign in Initialize function, it will ensure if its the default value (NAME_None) when using the class
			FAttributeKey GetKeyCount() const { ensure(!KeyCount.Key.IsEmpty()); return KeyCount; }

			FAttributeKey GetIndexKey(int32 Index) const
			{
				FString DepIndexKeyString = GetKeyCount().ToString() + TEXT("_NameIndex_") + FString::FromInt(Index);
				return FAttributeKey(*DepIndexKeyString);
			}
		};

		template<typename KeyType, typename ValueType>
		class TMapAttributeHelper
		{
			static_assert(TAttributeTypeTraits<KeyType>::GetType() != EAttributeTypes::None, "The key type must be supported by the attribute storage");
			static_assert(TAttributeTypeTraits<ValueType>::GetType() != EAttributeTypes::None, "The value type must be supported by the attribute storage");

			template<typename T>
			using TAttributeHandle = FAttributeStorage::TAttributeHandle<T>;

		public:
			void Initialize(const TSharedRef<FAttributeStorage, ESPMode::ThreadSafe>& InAttributes, const FString& BaseKeyName)
			{
				Attributes = InAttributes;
				FString BaseTryName = TEXT("__") + BaseKeyName;
				FAttributeKey KeyCountKey(MoveTemp(BaseTryName));
				if (!InAttributes->ContainAttribute(KeyCountKey))
				{ 
					EAttributeStorageResult Result = InAttributes->RegisterAttribute<int32>(KeyCountKey, 0);
					check (Result == EAttributeStorageResult::Operation_Success);
					KeyCountHandle = InAttributes->GetAttributeHandle<int32>(KeyCountKey);
				}
				else
				{
					KeyCountHandle = InAttributes->GetAttributeHandle<int32>(KeyCountKey);

					//Init map
					int32 KeyCount;
					KeyCountHandle.Get(KeyCount);

					CachedKeysAndValues.Reserve(KeyCount);

					for (int32 Index = 0; Index < KeyCount; Index++)
					{
						TAttributeHandle<KeyType> KeyAttribute = InAttributes->GetAttributeHandle<KeyType>(GetKeyAttribute(Index));
						KeyType Key;
						KeyAttribute.Get(Key);
						TAttributeHandle<ValueType> ValueAttribute = InAttributes->GetAttributeHandle<ValueType>(GetValueAttribute(Key));
						CachedKeysAndValues.Add(Key, TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>(KeyAttribute, ValueAttribute));
					}
				}
			}

			TMapAttributeHelper() = default;
			TMapAttributeHelper(const TMapAttributeHelper&) = default;
			TMapAttributeHelper(TMapAttributeHelper&&) = default;
			TMapAttributeHelper& operator=(const TMapAttributeHelper&) = default;
			TMapAttributeHelper& operator=(TMapAttributeHelper&&) = default;

			~TMapAttributeHelper()
			{
				Attributes.Reset();
			}

			void SetKeyValue(const KeyType& InKey, const ValueType& InValue)
			{
				const uint32 Hash = GetTypeHash(InKey);
				SetKeyValueByHash(Hash, InKey, InValue);
			}

			bool GetValue(const KeyType& InKey, ValueType& OutValue) const
			{
				const uint32 Hash = GetTypeHash(InKey);
				return GetValueByHash(Hash, InKey, OutValue);
			}

			bool RemoveKey(const KeyType& InKey)
			{
				const uint32 Hash = GetTypeHash(InKey);
				return RemoveKeyByHash(Hash, InKey);
			}

			bool RemoveKeyAndGetValue(const KeyType& InKey, ValueType& OutValue)
			{
				const uint32 Hash = GetTypeHash(InKey);
				return RemoveKeyAndGetValueByHash(Hash, InKey, OutValue);
			}

			void SetKeyValueByHash(uint32 Hash, const KeyType& InKey, const ValueType& InValue)
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return;
				}

				if (TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>* Pair = CachedKeysAndValues.FindByHash(Hash, InKey))
				{
					Pair->Value.Set(InValue);
				}
				else
				{
					FAttributeKey IndexKey = GetKeyAttribute(CachedKeysAndValues.Num());
					FAttributeKey AttributeKey = GetValueAttribute(InKey);
					check(AttributesPtr->RegisterAttribute<KeyType>(IndexKey, InKey) == EAttributeStorageResult::Operation_Success);
					check(AttributesPtr->RegisterAttribute<ValueType>(AttributeKey, InValue) == EAttributeStorageResult::Operation_Success);
					CachedKeysAndValues.AddByHash(
						Hash
						, InKey
						, TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>(
							AttributesPtr->GetAttributeHandle<KeyType>(IndexKey)
							, AttributesPtr->GetAttributeHandle<ValueType>(AttributeKey)
						));
					KeyCountHandle.Set(CachedKeysAndValues.Num());
				}
			}

		
			bool GetValueByHash(uint32 Hash, const KeyType& InKey, ValueType& OutValue) const
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return false;
				}

				if (const TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>* Pair = CachedKeysAndValues.FindByHash(Hash, InKey))
				{
					return Pair->Value.Get(OutValue) == EAttributeStorageResult::Operation_Success;
				}
			}

	
			bool RemoveKeyByHash(uint32 Hash, const KeyType& InKey)
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return false;
				}

				if (TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>* Pair = CachedKeysAndValues.FindByHash(Hash, InKey))
				{
					return RemoveBySwap(Hash, InKey, *Pair);
				}

				return false;
			}

			bool RemoveKeyAndGetValueByHash(uint32 Hash, const KeyType& InKey, ValueType& OutValue)
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return false;
				}

				if (TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>* Pair = CachedKeysAndValues.FindByHash(Hash, InKey))
				{
					if (Pair->Value.Get(OutValue) == EAttributeStorageResult::Operation_Success)
					{
						return RemoveBySwap(Hash, InKey, *Pair);
					}
				}

				return false;
			}

			void Reserve(int32 Number)
			{
				CachedKeysAndValues.Reserve(Number);
			}

			void Empty(int32 NumOfExpectedElements = 0)
			{
				EmptyInternal(NumOfExpectedElements);
			}

			TMapAttributeHelper& operator=(const TMap<KeyType, ValueType>& InMap)
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return *this;
				}

				// Empty
				EmptyInternal(InMap.Num());

				KeyCountHandle.Set(InMap.Num());

				for (const TPair<KeyType, ValueType>& Pair : InMap)
				{
					SetKeyValue(Pair.Key, Pair.Value);
				}

				return *this;
			}

			TMap<KeyType, ValueType> ToMap() const
			{
				TMap<KeyType, ValueType> Map;
				Map.Reserve(CachedKeysAndValues.Num());
				
				for (const TPair<KeyType, TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>>& Pair: CachedKeysAndValues)
				{
					KeyType Key;
					Pair.Value.Key.Get(Key);
					ValueType Value;
					Pair.Value.Value.Get(Value);

					Map.Add(MoveTemp(Key), MoveTemp(Value));
				}

				return Map;
			}

		private:
			FAttributeKey GetKeyAttribute(int32 Index) const
			{
				static const FString KeyIndex = TEXT("_KeyIndex_");
				FString IndexedKey =  KeyCountHandle.GetKey().ToString();
				IndexedKey.Reserve(KeyIndex.Len() + 16 /*Max size for a int32*/);
				IndexedKey.Append(KeyIndex);
				IndexedKey.AppendInt(Index);
				return FAttributeKey(MoveTemp(IndexedKey));
			}

			FAttributeKey GetValueAttribute(const KeyType& InKey) const
			{
				static const FString KeyChars = TEXT("_Key_");
				FString ValueAttribute =  KeyCountHandle.GetKey().ToString();
				FString Key = TTypeToString<KeyType>::ToString(InKey);
				ValueAttribute.Reserve(KeyChars.Len() + Key.Len());
				ValueAttribute.Append(KeyChars);
				ValueAttribute.Append(Key);
				return FAttributeKey(MoveTemp(ValueAttribute));
			}

			TAttributeHandle<KeyType> GetLastKeyAttributeHandle() const
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return {};
				}
				return AttributesPtr->GetAttributeHandle<KeyType>(GetKeyAttribute(CachedKeysAndValues.Num() - 1));
			}

			bool RemoveBySwap(uint32 Hash, const KeyType& InKey, TPair<TAttributeHandle<FString>, TAttributeHandle<ValueType>>& CachedPair)
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return false;
				}

				TAttributeHandle<KeyType> LastKeyIndex = GetLastKeyAttributeHandle();
				KeyType LastKey;
				LastKeyIndex.Get(LastKey);
				CachedKeysAndValues[LastKey].Key = CachedPair.Key;
				CachedPair->Key.Set(MoveTemp(LastKey));

				AttributesPtr->UnregisterAttribute(LastKeyIndex.GetKey());
				AttributesPtr->UnregisterAttribute(CachedPair.Value.GetKey());
				CachedKeysAndValues.RemoveByHash(Hash, InKey);
				KeyCountHandle.Set(CachedKeysAndValues.Num());
			}

			void EmptyInternal(int32 NumOfExpectedElements)
			{
				TSharedPtr<FAttributeStorage, ESPMode::ThreadSafe> AttributesPtr = Attributes.Pin();
				if (!ensure(AttributesPtr.IsValid()))
				{
					return;
				}

				for (const TPair<KeyType, TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>>& Pair : CachedKeysAndValues)
				{
					const FAttributeKey& KeyAttribute = Pair.Value.Key.GetKey();
					AttributesPtr->UnregisterAttribute(KeyAttribute);

					const FAttributeKey& ValueAttribute = Pair.Value.Value.GetKey();
					AttributesPtr->UnregisterAttribute(ValueAttribute);
				}

				CachedKeysAndValues.Empty(NumOfExpectedElements);
			}

			TMap<KeyType, TPair<TAttributeHandle<KeyType>, TAttributeHandle<ValueType>>> CachedKeysAndValues;
			TAttributeHandle<int32> KeyCountHandle; //Assign in Initialize function, it will ensure if its the default value (NAME_None) when using the class
			TWeakPtr<FAttributeStorage, ESPMode::ThreadSafe> Attributes = nullptr;
		};

		/**
		 * Helper struct use to declare static const data we use in the UInterchangeBaseNode
		 * Node that derive from UInterchangeBaseNode can also add a struct that derive from this one to add there static data
		 * @note: The static data are mainly for holding Attribute keys. All attributes that are always available for a node should be in this class or a derived class.
		 */
		struct FBaseNodeStaticData
		{
			static const FAttributeKey& UniqueIDKey()
			{
				static FAttributeKey AttributeKey(TEXT("__UNQ_ID_"));
				return AttributeKey;
			}

			static const FAttributeKey& DisplayLabelKey()
			{
				static FAttributeKey AttributeKey(TEXT("__DSPL_LBL_"));
				return AttributeKey;
			}

			static const FAttributeKey& ParentIDKey()
			{
				static FAttributeKey AttributeKey(TEXT("__PARENT_UID_"));
				return AttributeKey;
			}

			static const FAttributeKey& IsEnabledKey()
			{
				static FAttributeKey AttributeKey(TEXT("__IS_NBLD_"));
				return AttributeKey;
			}

			static const FString& GetDependenciesBaseKey()
			{
				static FString BaseNodeDependencies_BaseKey = TEXT("BaseNodeDependencies__");
				return BaseNodeDependencies_BaseKey;
			}
		};

	} //ns Interchange
} //ns UE

/**
 * This struct is used to store and retrieve key value attributes. The attributes are store in a generic FAttributeStorage which serialize the value in a TArray64<uint8>
 * See UE::Interchange::EAttributeTypes to know the supported template types
 * This is an abstract class. This is the base class of the interchange node graph format, all class in this format should derive from this class
 */
UCLASS(BlueprintType)
class INTERCHANGECORE_API UInterchangeBaseNode : public UObject
{
	GENERATED_BODY()

public:
	UInterchangeBaseNode()
	{
		Attributes = MakeShared<UE::Interchange::FAttributeStorage, ESPMode::ThreadSafe>();
		Dependencies.Initialize(Attributes, UE::Interchange::FBaseNodeStaticData::GetDependenciesBaseKey());
	}

	virtual ~UInterchangeBaseNode() = default;

	/**
	 * Initialize the base data of the node
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void InitializeNode(const FString& UniqueID, const FString& DisplayLabel);

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const;

	/**
	 * Add an attribute to the node
	 * @param NodeAttributeKey - The key of the attribute
	 * @param Value - The attribute value.
	 *
	 */
	template<typename T>
	UE::Interchange::FAttributeStorage::TAttributeHandle<T> RegisterAttribute(const UE::Interchange::FAttributeKey& NodeAttributeKey, const T& Value)
	{
		const UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(NodeAttributeKey, Value);
		
		if (IsAttributeStorageResultSuccess(Result))
		{
			return Attributes->GetAttributeHandle<T>(NodeAttributeKey);
		}
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), NodeAttributeKey);
		return UE::Interchange::FAttributeStorage::TAttributeHandle<T>();
	}

	/**
	 * Return true if the node contain an attribute with the specified Key
	 * @param nodeAttributeKey - The key of the searched attribute
	 *
	 */
	virtual bool HasAttribute(const UE::Interchange::FAttributeKey& NodeAttributeKey) const;

	/**
	 * This function return an attribute type for the specified Key. Return type None if the key is invalid
	 *
	 * @param NodeAttributeKey - The key of the attribute
	 */
	virtual UE::Interchange::EAttributeTypes GetAttributeType(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
	{
		return Attributes->GetAttributeType(NodeAttributeKey);
	}

	/**
	 * This function return an  attribute handle for the specified Key.
	 * If there is an issue with the KEY or storage the method will trip a check, always make sure you have a valid key before calling this
	 * 
	 * @param NodeAttributeKey - The key of the attribute
	 */
	template<typename T>
	UE::Interchange::FAttributeStorage::TAttributeHandle<T> GetAttributeHandle(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
	{
		return Attributes->GetAttributeHandle<T>(NodeAttributeKey);
	}

	/**
	 * Return the unique id pass in the constructor.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FString GetUniqueID() const;

	/**
	 * Return the display label.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FString GetDisplayLabel() const;

	/**
	 * Change the display label.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetDisplayLabel(const FString& DisplayName);

	/**
	 * Return the parent unique id. In case the attribute does not exist it will return InvalidNodeUID()
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FString GetParentUID() const;

	/**
	 * Set the parent unique id.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetParentUID(const FString& ParentUID);

	/**
	 * This function allow to retrieve the number of dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	int32 GetDependeciesCount() const;

	/**
	 * This function allow to retrieve the dependency for this object.
	 * 
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetDependecies(TArray<FString>& OutDependencies ) const;

	/**
	 * Add one dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetDependencyUID(const FString& DependencyUID);

	/**
	 * Remove one dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool RemoveDependencyUID(const FString& DependencyUID);

	/**
	 * IsEnable true mean that the node will be import/export, if false it will be discarded.
	 * Return false if this node was disabled. Return true if the attribute is not there or if it was enabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool IsEnabled() const;

	/**
	 * Set the IsEnable attribute to determine if this node should be part of the import/export process
	 * @param bIsEnabled - The enabled state we want to set this node. True will import/export the node, fals will not.
	 * @return true if it was able to set the attribute, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetEnabled(const bool bIsEnabled);

	/**
	 * Return a FGuid build from the FSHA1 of all the attribute data contain in the node.
	 *
	 * @note the attribute are sorted by key when building the FSHA1 data. The hash will be deterministic for the same data whatever
	 * the order we add the attributes.
	 * This function interface is pure virtual
	 */
	virtual FGuid GetHash() const;

	/**
	 * Optional, Any node that can import/export an asset should return the UClass of the asset so we can find factory/writer
	 */
	virtual class UClass* GetAssetClass() const;

	/** Return the invalid unique ID */
	static FString InvalidNodeUID();

	/**
	 * Each Attribute that was set and have a delegate set for the specified UObject->UClass will
	 * get the delegate execute so it apply the attribute to the UObject property.
	 * See the macros IMPLEMENT_NODE_ATTRIBUTE_SETTER at the top of the file to know how delegates are setup for property.
	 *
	 */
	void ApplyAllCustomAttributeToAsset(UObject* Object) const;

	void FillAllCustomAttributeFromAsset(UObject* Object) const;

	virtual void Serialize(FArchive& Ar) override;

	static void CompareNodeStorage(UInterchangeBaseNode* NodeA, const UInterchangeBaseNode* NodeB, TArray<UE::Interchange::FAttributeKey>& RemovedAttributes, TArray<UE::Interchange::FAttributeKey>& AddedAttributes, TArray<UE::Interchange::FAttributeKey>& ModifiedAttributes)
	{
		UE::Interchange::FAttributeStorage::CompareStorage(*(NodeA->Attributes), *(NodeB->Attributes), RemovedAttributes, AddedAttributes, ModifiedAttributes);
	}
	
	static void CopyStorageAttributes(const UInterchangeBaseNode* SourceNode, UInterchangeBaseNode* DestinationNode, TArray<UE::Interchange::FAttributeKey>& AttributeKeys)
	{
		UE::Interchange::FAttributeStorage::CopyStorageAttributes(*(SourceNode->Attributes), *(DestinationNode->Attributes), AttributeKeys);
	}

	static void CopyStorage(const UInterchangeBaseNode* SourceNode, UInterchangeBaseNode* DestinationNode)
	{
		*(DestinationNode->Attributes) = *(SourceNode->Attributes);
	}
	
	UPROPERTY()
	mutable FSoftObjectPath ReferenceObject;
protected:
	/** The storage use to store the Key value attribute for this node. */
	TSharedPtr<UE::Interchange::FAttributeStorage, ESPMode::ThreadSafe> Attributes;

	/* This array hold the delegate to apply the attribute that has to be set on an UObject */
	TMap<UClass*, TArray<UE::Interchange::FApplyAttributeToAsset>> ApplyCustomAttributeDelegates;

	TMap<UClass*, TArray<UE::Interchange::FFillAttributeToAsset>> FillCustomAttributeDelegates;

	bool bIsInitialized = false;

	UE::Interchange::FNameAttributeArrayHelper Dependencies;
};
