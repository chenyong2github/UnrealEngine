// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/SortedMap.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "Math/DualQuat.h"
#include "Math/RandomStream.h"
#include "Misc/FrameRate.h"
#include "Misc/NetworkGuid.h"
#include "Misc/ScopeLock.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{
		struct FAttributeKey
		{
			FString Key;

			FAttributeKey() = default;
		
			FAttributeKey(const FAttributeKey& Other) = default;
			FAttributeKey(FAttributeKey&& Other) = default;
			FAttributeKey& operator=(const FAttributeKey&) = default;
			FAttributeKey& operator=(FAttributeKey&&) = default;

			explicit FAttributeKey(const FName& Other)
			{
				Key = Other.ToString();
			}

			explicit FAttributeKey(const FString& Other)
			{
				Key = Other;
			}

			explicit FAttributeKey(FString&& Other)
			{
				Key = MoveTemp(Other);
			}

			explicit FAttributeKey(const FText& Other)
			{
				Key = Other.ToString();
			}

			explicit FAttributeKey(const TCHAR* Other)
			{
				const FString Helper(Other);
				Key = Helper;
			}

			FORCEINLINE const TCHAR* operator*() const
			{
				return *(Key);
			}

			FORCEINLINE FAttributeKey& operator=(const FName& Other)
			{
				Key = Other.ToString();
				return *this;
			}

			FORCEINLINE FAttributeKey& operator=(const FString& Other)
			{
				Key = Other;
				return *this;
			}

			FORCEINLINE FAttributeKey& operator=(FString&& Other)
			{
				Key = MoveTemp(Other);
				return *this;
			}

			FORCEINLINE FAttributeKey& operator=(const FText& Other)
			{
				Key = Other.ToString();
				return *this;
			}

			FORCEINLINE FAttributeKey& operator=(const TCHAR* Other)
			{
				const FString Helper(Other);
				Key = Helper;
				return *this;
			}
		
			FORCEINLINE bool operator==(const FAttributeKey& Other) const
			{
				return Key == Other.Key;
			}

			FORCEINLINE bool operator!=(const FAttributeKey& Other) const
			{
				return Key != Other.Key;
			}

			FORCEINLINE bool operator<(const FAttributeKey& Other) const
			{
				return Key < Other.Key;
			}

			FORCEINLINE bool operator<=(const FAttributeKey& Other) const
			{
				return Key.Compare(Other.Key) <= 0;
			}

			FORCEINLINE bool operator>(const FAttributeKey& Other) const
			{
				return Key > Other.Key;
			}

			FORCEINLINE bool operator>=(const FAttributeKey& Other) const
			{
				return Key.Compare(Other.Key) >= 0;
			}

			friend FArchive& operator<<(FArchive& Ar, FAttributeKey& AttributeKey)
			{
				Ar << AttributeKey.Key;
				return Ar;
			}

			FORCEINLINE FString ToString() const
			{
				return Key;
			}
		};

		using ::GetTypeHash;
		FORCEINLINE uint32 GetTypeHash(const FAttributeKey& AttributeKey)
		{
			return GetTypeHash(AttributeKey.Key);
		}


		/**
		 * Enumerates the built-in types that can be stored in instances of FAttributeStorage.
		 * We cannot change the value of a type to make sure the serialization of old asset is always working
		 */
		enum class EAttributeTypes : int32
		{
			None				= 0,
			Bool				= 1,
			Box					= 2,
			BoxSphereBounds		= 3,
			ByteArray			= 4,
			ByteArray64			= 5,
			Color				= 6,
			DateTime			= 7,
			Double				= 8,
			Enum				= 9,
			Float				= 10,
			Guid				= 11,
			Int8				= 12,
			Int16				= 13,
			Int32				= 14,
			Int64				= 15,
			IntRect				= 16,
			LinearColor			= 17,
			Matrix				= 18,
			Name				= 19,
			Plane				= 20,
			Quat				= 21,
			RandomStream		= 22,
			Rotator				= 23,
			String				= 24,
			Timespan			= 25,
			Transform			= 26,
			TwoVectors			= 27,
			UInt8				= 28,
			UInt16				= 29,
			UInt32				= 30,
			UInt64				= 31,
			Vector				= 32,
			Vector2d			= 33,
			Vector4				= 34,
			IntPoint			= 35,
			IntVector			= 36,
			NetworkGUID			= 37,
			Vector2DHalf		= 38,
			Float16				= 39,
			CapsuleShape		= 40,
			DualQuat			= 41,
			OrientedBox			= 42,
			Ray					= 43,
			Sphere				= 44,
			FrameNumber			= 45,
			FrameRate			= 46,
			FrameTime			= 47,
		};


		/**
		 * Stub for attribute type traits.
		 *
		 * Actual type traits need to be declared through template specialization for custom
		 * data types that are to be used internally by FAttributeStorage. Traits for the most commonly used built-in
		 * types are declared below.
		 *
		 * Complex types, such as structures and classes can be serialized into a byte array
		 * and then assigned to an attribute. Note that you will be responsible for ensuring
		 * correct byte ordering when serializing those types.
		 *
		 * @param T The type to be used in FAttributeStorage.
		 */
		template<typename T> struct TAttributeTypeTraits
		{
			static CONSTEXPR EAttributeTypes GetType()
			{
				static_assert(!sizeof(T), "Attribute type trait must be specialized for this type.");
				return EAttributeTypes::None;
			}
		};

		/**
		 * Enum to return complete status of a storage operation
		 * It support success with additional information
		 * It support multiple errors
		 */
		enum class EAttributeStorageResult : uint64
		{
			None											= 0x0,
			//success result
			Operation_Success								= 0x1,
		

			//Operation error result from here

			//The type of the value was not matching the existing type. We cannot change the type of an existing attribute
			Operation_Error_WrongType						= (0x1 << 20),
			//The size of the value is different from the existing size. We cannot have a different size
			Operation_Error_WrongSize						= (0x1 << 21),
			//The AttributeAllocationTable has an attribute that the offset is no valid in the storage
			Operation_Error_AttributeAllocationCorrupted	= (0x1 << 22),
			//We cannot find the specified key
			Operation_Error_CannotFoundKey					= (0x1 << 23),
			//There was an error when removing an attribute from the AttributeAllocationTable. The TArray remove has failed
			Operation_Error_CannotRemoveAttribute			= (0x1 << 24),
			//We try to override an attribute but the specified options do not allow override.
			Operation_Error_CannotOverrideAttribute			= (0x1 << 25),
			//The storage is invalid (nullptr).
			Operation_Error_InvalidStorage					= (0x1 << 26),
			//Cannot get a valid value data pointer.
			Operation_Error_InvalidMultiSizeValueData		= (0x1 << 27),
		};

		ENUM_CLASS_FLAGS(EAttributeStorageResult)

		/**
		 * Helper function to interpret storage result
		 * @return true if the Result contain at least one of the RefResult flags
		 */
		FORCEINLINE bool HasAttributeStorageResult(const EAttributeStorageResult Result, const EAttributeStorageResult RefResult)
		{
			return ((Result & RefResult) != EAttributeStorageResult::None);
		}
	
		/**
		 * Helper function to determine if the storage result is success
		 * @return true if result contain Operation_Success
		 */
		FORCEINLINE bool IsAttributeStorageResultSuccess(const EAttributeStorageResult Result)
		{
			return HasAttributeStorageResult(Result, EAttributeStorageResult::Operation_Success);
		}

		/**
		 * Helper function to transform an operation result into a LOG.
		 * @param Result - The result we want to output a log for
		 * @param OperationName - The operation name that end up with the speicified Result.
		 * @param AttributeKey - The attribute we did apply the operation on.
		 */
		INTERCHANGECORE_API void LogAttributeStorageErrors(const EAttributeStorageResult Result, const FString OperationName, const FAttributeKey AttributeKey );


		/**
		 * Enum to pass options when we add an attribute
		 */
		enum class EAttributeStorageAddOptions : uint32
		{
			None					= 0x0,
			Option_Override			= 0x1,
			//allow the AddAttribute to override the value if it exist.
		};

		ENUM_CLASS_FLAGS(EAttributeStorageAddOptions)

		/**
		 * Helper function to interpret storage add options
		 */
		FORCEINLINE bool HasAttributeStorageAddOption(const EAttributeStorageAddOptions Options, const EAttributeStorageAddOptions RefOptions)
		{
			return ((Options & RefOptions) != EAttributeStorageAddOptions::None);
		}

		/**
		 * Enumerates the attribute properties. Those properties affect how the attribute are stored or what they are use for
		 */
		enum class EAttributeProperty : uint32
		{
			None						= 0x0,
			NoHash						= 0x1, /*No hash attribute will not be part of the hash result when calling GetStorageHash*/
		};

		ENUM_CLASS_FLAGS(EAttributeProperty)

		/**
		 * Helper function to interpret attribute property
		 */
		FORCEINLINE bool HasAttributeProperty(const EAttributeProperty PropertyA, const EAttributeProperty PropertyB)
		{
			return ((PropertyA & PropertyB) != EAttributeProperty::None);
		}

		/**
		 * This is a helper class to specialize template function inside a class, it allow specialize template to not be static
		 */
		template<typename T>
		struct TSpecializeType
		{
			typedef T Type;
		};

	
		/**
		 * This class is a Key/Value storage inside a TArray64<uint8>
		 * The keys are of type FAttributeKey which is a FString, each key is unique and have only one value.
		 * The value can be of any type contain in EAttributeTypes.
		 * 
		 * @notes
		 * The storage is multi thread safe, it use a mutex to lock the storage for every read/write operation.
		 * The hash of the storage is deterministic because it sort the attributes before calculating the hash.
		 */
		class INTERCHANGECORE_API FAttributeStorage
		{
		public:
			/**
			 * Class to Get/Set an attribute of the storage 
			 */
			template<typename T>
			class TAttributeHandle
			{
				static_assert(TAttributeTypeTraits<T>::GetType() != EAttributeTypes::None, "Unsupported attribute type");

			public:
				TAttributeHandle()
				: AttributeStorage(nullptr)
				, Key()
				{}

				/**
				* Return true if the storage contain a valid attribute key, false otherwise. 
				*/
				FORCEINLINE bool IsValid() const
				{
					const EAttributeTypes ValueType = TAttributeTypeTraits<T>::GetType();
					return (AttributeStorage && AttributeStorage->ContainAttribute(Key) && AttributeStorage->GetAttributeType(Key) == ValueType);
				}
			
				EAttributeStorageResult Get(T& Value) const
				{
					if (AttributeStorage)
					{
						return AttributeStorage->GetAttribute<T>(Key, Value);
					}
					return EAttributeStorageResult::Operation_Error_InvalidStorage;
				}

				EAttributeStorageResult Set(const T& Value)
				{
					if (AttributeStorage)
					{
						return AttributeStorage->SetAttribute<T>(Key, Value);
					}
					return EAttributeStorageResult::Operation_Error_InvalidStorage;
				}

				const FAttributeKey& GetKey() const
				{
					return Key;
				}

			protected:
				class FAttributeStorage* AttributeStorage;
				FAttributeKey Key;
			
				TAttributeHandle(const FAttributeKey& InKey, const FAttributeStorage* InAttributeStorage)
				{
					AttributeStorage = const_cast<FAttributeStorage*>(InAttributeStorage);
					Key = InKey;
				
					//Look for storage validity
					if (AttributeStorage == nullptr)
					{
						//Storage is null
						LogAttributeStorageErrors(EAttributeStorageResult::Operation_Error_InvalidStorage, TEXT("GetAttributeHandle"), Key);
					}
					else
					{

						if (!AttributeStorage->ContainAttribute(Key))
						{
							//Storage do not contain the key
							LogAttributeStorageErrors(EAttributeStorageResult::Operation_Error_CannotFoundKey, TEXT("GetAttributeHandle"), Key);
						}
						if (AttributeStorage->GetAttributeType(Key) != TAttributeTypeTraits<T>::GetType())
						{
							//Value Type is different from the existing key
							LogAttributeStorageErrors(EAttributeStorageResult::Operation_Error_WrongType, TEXT("GetAttributeHandle"), Key);
						}
					}
				}

				friend FAttributeStorage;
			};

			FAttributeStorage() = default;

			FAttributeStorage(const FAttributeStorage& Other);

			FAttributeStorage& operator=(const FAttributeStorage& Other);

			/**
			 * Register an attribute in the storage. Return success if the attribute was properly added, or there is an existing
			 * attribute of the same type, return error otherwise.
			 *
			 * @Param ElementAttributeKey - is the storage key (the path) of the attribute
			 * @Param DefaultValue - is the default value for the registered attribute
			 *
			 * @note Possible errors
			 * - Key exist with a different type
			 * - Storage is corrupted
			 */
			template<typename T>
			EAttributeStorageResult RegisterAttribute(const FAttributeKey& ElementAttributeKey, const T& DefaultValue, EAttributeProperty AttributeProperty = EAttributeProperty::None)
			{
				static_assert(TAttributeTypeTraits<T>::GetType() != EAttributeTypes::None, "T is not a supported type for the attributes. Check EAttributeTypes for the supported types");

				//Lock the storage
				FScopeLock ScopeLock(&StorageMutex);


				const EAttributeTypes ValueType = TAttributeTypeTraits<T>::GetType();
				const uint64 ValueSize = GetValueSize(DefaultValue);

				FAttributeAllocationInfo* AttributeAllocationInfo = AttributeAllocationTable.Find(ElementAttributeKey);
				if (AttributeAllocationInfo)
				{
					if (AttributeAllocationInfo->Type != ValueType)
					{
						return EAttributeStorageResult::Operation_Error_WrongType;
					}
					if (!AttributeStorage.IsValidIndex(AttributeAllocationInfo->Offset))
					{
						return EAttributeStorageResult::Operation_Error_AttributeAllocationCorrupted;
					}
				}
				else
				{
					AttributeAllocationInfo = &AttributeAllocationTable.Add(ElementAttributeKey);
					AttributeAllocationInfo->Type = ValueType;
					AttributeAllocationInfo->Size = ValueSize;
					AttributeAllocationInfo->Offset = AttributeStorage.AddZeroed(ValueSize);
				}

				//Force the specified attribute property
				AttributeAllocationInfo->Property = AttributeProperty;

				//Use template SetAttribute which support all the types
				const EAttributeStorageResult Result = SetAttribute(AttributeAllocationInfo, DefaultValue);
				if (!IsAttributeStorageResultSuccess(Result))
				{
					//An error occured, unregister the key from the storage
					UnregisterAttribute(ElementAttributeKey);
				}
				return Result;
			}

			/**
			 * Remove an attribute from the storage.
			 *
			 * @param ElementAttributeKey - is the storage key (the path) of the attribute to remove.
			 *
			 * @note Possible errors
			 * - Key do not exist
			 * - Internal storage structure remove error
			 */
			EAttributeStorageResult UnregisterAttribute(const FAttributeKey& ElementAttributeKey);

			/**
			 * Return an attribute handle for the specified attribute. This handle is compile type check and is use to Get and Set the attribute value type.
			 * The function will assert if the key is missing or the type doesnt match the specified template type.
			 *
			 * @param ElementAttributeKey is the storage key (the path) of the attribute.
			 */
			template<typename T>
			TAttributeHandle<T> GetAttributeHandle(const FAttributeKey& ElementAttributeKey) const
			{
				TAttributeHandle<T> AttributeHandle(ElementAttributeKey, this);
				return AttributeHandle;
			}


			/**
			 * Return the attribute type if the key exist, return None if the key is missing
			 *
			 * @param ElementAttributeKey - is the storage key (the path) of the attribute.
			 *
			 */
			EAttributeTypes GetAttributeType(const FAttributeKey& ElementAttributeKey) const;

			/**
			 * Return true if the attribute key point on an existing attribute in the storage. Return false otherwise
			 *
			 * @param ElementAttributeKey - is the storage key (the path) of the attribute.
			 *
			 */
			bool ContainAttribute(const FAttributeKey& ElementAttributeKey) const;

			/**
			 * This function allow the user to retrieve the array of keys to iterate and do reflection on the storage content
			 *
			 */
			void GetAttributeKeys(TArray<FAttributeKey>& AttributeKeys) const;
	
			/**
			 * Return a FGuid build from the FSHA1 of the specified attribute data. If the Attribute do not exist it return an empty FGUID
			 *
			 * @param ElementAttributeKey is the storage key (the path) of the attribute.
			 *
			 */
			FGuid GetAttributeHash(const FAttributeKey& ElementAttributeKey) const;
	
			/**
			 * This function fill the OutGuid with the hash of the specified attribute. Return true if the attribute exist and the OutGuid was assign, return false otherwise without touching the OutGuid.
			 *
			 * @param ElementAttributeKey - is the storage key (the path) of the attribute.
			 * @param OutGuid - is where we put the attribute hash.
			 *
			 */
			bool GetAttributeHash(const FAttributeKey& ElementAttributeKey, FGuid& OutGuid) const;

			/**
			 * Return a FGuid build from the FSHA1 of all the attribute data contain in the node.
			 * The data include the UniqueID and the DisplayLabel.
			 *
			 * @note the attribute are sorted by key when building the FSHA1 data. The hash will be deterministic for the same data whatever
			 * the order we add the attributes.
			 */
			FGuid GetStorageHash() const;

			/**
			 * Compare two storage to know which properties was modified/added/removed.
			 *
			 * @param BaseStorage - The storage reference
			 * @param VersionStorage - The storage with the changes
			 * @param RemovedAttribute - All attributes that are in base storage but not in version storage, contain key only valid for base storage
			 * @param AddedAttributes - All attributes that are in version storage but not in base storage, contain key only valid for version storage
			 * @param ModifiedAttributes - All attributes that are in both storage base storage but have a different hash (different value)
			 *
			 */
			static void CompareStorage(const FAttributeStorage& BaseStorage, const FAttributeStorage& VersionStorage, TArray<FAttributeKey>& RemovedAttributes, TArray<FAttributeKey>& AddedAttributes, TArray<FAttributeKey>& ModifiedAttributes);

			/**
			 * Copy an array of attributes from the source storage to the destination storage. If the attribute already exist in the destination, the value will be updated.
			 * If a key do not exist in the source it will not be copy/created in the destination
			 *
			 * @param SourceStorage - The storage source
			 * @param DestinationStorage - The storage destination
			 * @param AttributeKeys - All attributes that must be copy from the source to the destination.
			 *
			 */
			static void CopyStorageAttributes(const FAttributeStorage& SourceStorage, FAttributeStorage& DestinationStorage, const TArray<FAttributeKey>& AttributeKeys);

			/**
			 * Return the defrag ratio. This ratio is use to know when we need to defrag the storage
			 * @example - a ratio of 0.1f will defrag the storage if the memory lost is bigger then 10% of the storage allocation.
			 * Defrag is call when we remove an attribute or when we set the defrag ratio
			 */
			FORCEINLINE float GetDefragRatio() const
			{
				return DefragRatio;
			}
	
			/** Set the defrag ratio. See the GetDefragRatio for the defrag documentation. */
			void SetDefragRatio(const float InDefragRatio);
	
			friend FArchive& operator<<(FArchive& Ar, FAttributeStorage& Storage)
			{
				Ar << Storage.FragmentedMemoryCost;
				Ar << Storage.DefragRatio;
				Ar << Storage.AttributeAllocationTable;
				Ar << Storage.AttributeStorage;
				return Ar;
			}

			/**
			 * Reserve the allocation table and the storage data.
			 * 
			 * @param NewAttributeCount: The number of attribute we want to reserve. Zero value do not reserve attribute count
			 * @param NewStorageSize: The size on the storage all those new attribute will need. Passing zero value do not reserve storage size
			 */
			void Reserve(int64 NewAttributeCount, int64 NewStorageSize);

		protected:
			/** Structure use to hold the attribute information store in the attribute allocation table. */
			struct FAttributeAllocationInfo
			{
        		//The offset in the storage
        		uint64 Offset = 0;
        		//The size of the data in the storage
        		uint64 Size = 0;
        		//The real type of the attribute
        		EAttributeTypes Type = EAttributeTypes::None;
        		//The attribute properties
        		EAttributeProperty Property = EAttributeProperty::None;
        		//128 bit Attribute hash
        		FGuid Hash = FGuid();

        		bool operator==(const FAttributeAllocationInfo& Other) const
        		{
        			//Offset is a unique key
        			return Offset == Other.Offset
        				&& Size == Other.Size
        				&& Type == Other.Type
        				&& Property == Other.Property
        				&& Hash == Other.Hash;
        		}
        	
        		//Serialization
        		friend FArchive& operator<<(FArchive& Ar, FAttributeAllocationInfo& AttributeAllocationInfo)
        		{
        			Ar << AttributeAllocationInfo.Offset;
        			Ar << AttributeAllocationInfo.Size;
        			Ar << AttributeAllocationInfo.Type;
        			Ar << AttributeAllocationInfo.Property;
        			Ar << AttributeAllocationInfo.Hash;
        			return Ar;
        		}
			};
		
			/**
			 * Set an attribute value into the storage. Return success if the attribute was properly set.
			 *
			 * @param ElementAttributeKey is the storage key (the path) of the attribute
			 * @param Value is the value we want to add to the storage
			 *
			 * @note Possible errors
			 * - Key exist with a different type
			 * - Key exist with a wrong size
			 */
			template<typename T>
			EAttributeStorageResult SetAttribute(const FAttributeKey& ElementAttributeKey, const T& Value)
			{
				FAttributeAllocationInfo* AttributeAllocationInfo = AttributeAllocationTable.Find(ElementAttributeKey);
				return SetAttribute(AttributeAllocationInfo, Value);
			}

			template<typename T>
			EAttributeStorageResult SetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const T& Value)
			{
				return SetAttribute(AttributeAllocationInfo, Value, TSpecializeType<T>());
			}

			/**
			 * Retrieve a copy of an attribute value. Return success if the attribute is added.
			 *
			 * @param ElementAttributeKey is the storage key (the path) of the attribute.
			 * @param OutValue the reference value where we copy of the attribute value.
			 *
			 * @note Possible errors
			 * - Key do not exist
			 * - Key exist with a different type
			 * - Key exist with a wrong size
			 */
			template<typename T>
			EAttributeStorageResult GetAttribute(const FAttributeKey& ElementAttributeKey, T& OutValue) const
			{
				return GetAttribute(ElementAttributeKey, OutValue,TSpecializeType<T>());
			}
	
			/** Defrag the storage by using memmove on the attribute store after a hole in the storage. */
			void DefragInternal();

			static FGuid GetValueHash(const uint8* Value, uint64 ValueSize);

			/** Return the storage size of the value. */
			template<typename T>
			static uint64 GetValueSize(const T& Value)
			{
				return GetValueSize(Value, TSpecializeType<T>());
			}
	
			//Begin specialize templates
	
			template<typename T>
			EAttributeStorageResult SetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const T& Value, TSpecializeType<T>)
			{
				static_assert(TAttributeTypeTraits<T>::GetType() != EAttributeTypes::None, "T is not a supported type for the attributes. Check EAttributeTypes for the supported types");

				//Lock the storage
				FScopeLock ScopeLock(&StorageMutex);

				if (!AttributeAllocationInfo)
				{
					return EAttributeStorageResult::Operation_Error_CannotFoundKey;
				}
				const EAttributeTypes ValueType = TAttributeTypeTraits<T>::GetType();
				const uint64 ValueSize = GetValueSize(Value);

				if (AttributeAllocationInfo->Type != ValueType)
				{
					return EAttributeStorageResult::Operation_Error_WrongType;
				}
				if (AttributeAllocationInfo->Size != ValueSize)
				{
					return EAttributeStorageResult::Operation_Error_WrongSize;
				}
				if (!AttributeStorage.IsValidIndex(AttributeAllocationInfo->Offset))
				{
					return EAttributeStorageResult::Operation_Error_AttributeAllocationCorrupted;
				}

				//Set the hash from the value
				AttributeAllocationInfo->Hash = GetValueHash(reinterpret_cast<const uint8*>(&Value), ValueSize);

				uint8* StorageData = AttributeStorage.GetData();
				FMemory::Memcpy(&StorageData[AttributeAllocationInfo->Offset], &Value, ValueSize);
				return EAttributeStorageResult::Operation_Success;
			}

			EAttributeStorageResult SetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const TArray<uint8>& Value, TSpecializeType<TArray<uint8> >)
			{
				return MultiSizeSetAttribute(AttributeAllocationInfo, Value, [&Value]()->uint8*
				{
					return const_cast<uint8*>(Value.GetData());
				});
			}

			EAttributeStorageResult SetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const TArray64<uint8>& Value, TSpecializeType<TArray64<uint8> >)
			{
				return MultiSizeSetAttribute(AttributeAllocationInfo, Value, [&Value]()->uint8*
				{
					return const_cast<uint8*>(Value.GetData());
				});
			}

			EAttributeStorageResult SetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const FString& Value, TSpecializeType<FString >)
			{
				return MultiSizeSetAttribute(AttributeAllocationInfo, Value, [&Value]()->uint8*
				{
					return (uint8*)(*Value);
				});
			}

			EAttributeStorageResult SetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const FName& Value, TSpecializeType<FName >)
			{
				//FName must be store has a FString for persistence
				FString ValueStr = Value.ToString();
				return MultiSizeSetAttribute(AttributeAllocationInfo, Value, [&ValueStr]()->uint8*
				{
					return (uint8*)(*ValueStr);
				});
			}

			template<typename T>
			EAttributeStorageResult GetAttribute(const FAttributeKey& ElementAttributeKey, T& OutValue, TSpecializeType<T>) const
			{
				static_assert(TAttributeTypeTraits<T>::GetType() != EAttributeTypes::None, "T is not a supported type for the attributes. Check EAttributeTypes for the supported types");

				//Lock the storage
				FScopeLock ScopeLock(&StorageMutex);

				const EAttributeTypes ValueType = TAttributeTypeTraits<T>::GetType();
				const uint64 ValueSize = GetValueSize(OutValue);

				const FAttributeAllocationInfo* AttributeAllocationInfo = AttributeAllocationTable.Find(ElementAttributeKey);
				if (AttributeAllocationInfo)
				{
					if (AttributeAllocationInfo->Type != ValueType && !((ValueType == EAttributeTypes::UInt8) && (ValueType == EAttributeTypes::Enum)))
					{
						return EAttributeStorageResult::Operation_Error_WrongType;
					}
					if (AttributeAllocationInfo->Size != ValueSize)
					{
						return EAttributeStorageResult::Operation_Error_WrongSize;
					}
					if (!AttributeStorage.IsValidIndex(AttributeAllocationInfo->Offset))
					{
						return EAttributeStorageResult::Operation_Error_AttributeAllocationCorrupted;
					}
				}
				else
				{
					//The key do not exist
					return EAttributeStorageResult::Operation_Error_CannotFoundKey;
				}

				const uint8* StorageData = AttributeStorage.GetData();
				FMemory::Memcpy(&OutValue, &StorageData[AttributeAllocationInfo->Offset], AttributeAllocationInfo->Size);
				return EAttributeStorageResult::Operation_Success;
			}
	
			EAttributeStorageResult GetAttribute(const FAttributeKey& ElementAttributeKey, TArray<uint8>& OutValue, TSpecializeType<TArray<uint8> >) const
			{
				return GenericArrayGetAttribute(ElementAttributeKey, OutValue);
			}
    
			EAttributeStorageResult GetAttribute(const FAttributeKey& ElementAttributeKey, TArray64<uint8>& OutValue, TSpecializeType<TArray64<uint8> >) const
			{
				return GenericArrayGetAttribute(ElementAttributeKey, OutValue);
			}
	
			void ExtractFStringAttributeFromStorage(const uint8* StorageData, const FAttributeAllocationInfo* AttributeAllocationInfo, FString& OutValue) const;

			EAttributeStorageResult GetAttribute(const FAttributeKey& ElementAttributeKey, FString& OutValue, TSpecializeType<FString >) const;

			EAttributeStorageResult GetAttribute(const FAttributeKey& ElementAttributeKey, FName& OutValue, TSpecializeType<FName >) const;

			template<typename T>
			static uint64 GetValueSize(const T& Value, TSpecializeType<T>)
			{
				const EAttributeTypes ValueType = TAttributeTypeTraits<T>::GetType();
				if (ValueType == EAttributeTypes::None)
				{
					return 0;
				}
				return sizeof(T);
			}
	
			static uint64 GetValueSize(const FString& Value, TSpecializeType<FString >);

			static uint64 GetValueSize(const FName& Value, TSpecializeType<FName>)
			{
				FString ValueStr = Value.ToString();
				return GetValueSize(ValueStr, TSpecializeType<FString >());
			}

			static uint64 GetValueSize(const TArray<uint8>& Value, TSpecializeType<TArray<uint8> >);

			static uint64 GetValueSize(const TArray64<uint8>& Value, TSpecializeType<TArray64<uint8> >);
	
			// End specialize templates

			/** Set a multisize(TArray<uint8>, FString ...) attribute value into the storage. Return success if the attribute was properly set. */
			template<typename MultiSizeType>
			EAttributeStorageResult MultiSizeSetAttribute(FAttributeAllocationInfo* AttributeAllocationInfo, const MultiSizeType& Value, TFunctionRef<uint8* ()> GetValuePointer)
			{
				//Lock the storage
				FScopeLock ScopeLock(&StorageMutex);

				const EAttributeTypes ValueType = TAttributeTypeTraits<MultiSizeType>::GetType();

				if (!AttributeAllocationInfo)
				{
					return EAttributeStorageResult::Operation_Error_CannotFoundKey;
				}

				if (AttributeAllocationInfo->Type != ValueType)
				{
					return EAttributeStorageResult::Operation_Error_WrongType;
				}

				const uint64 ValueSize = GetValueSize(Value);

				uint8* DataPtr = GetValuePointer();
				//We must have a valid data pointer if we have something to copy into the storage.
				//This case support empty array or empty string because the ValueSize will be 0.
				if (!DataPtr && ValueSize > 0)
				{
					return EAttributeStorageResult::Operation_Error_InvalidMultiSizeValueData;
				}

				//If the new size is greater then the old size we have to create a new allocation info entry and delete the old one
				if (ValueSize > AttributeAllocationInfo->Size)
				{
					//Find the key to remove it
					const FAttributeKey* ElementAttributeKeyPtr = AttributeAllocationTable.FindKey(*AttributeAllocationInfo);
					if (!ElementAttributeKeyPtr)
					{
						return EAttributeStorageResult::Operation_Error_CannotFoundKey;
					}
					const FAttributeKey OriginalKey = *ElementAttributeKeyPtr;
					const EAttributeProperty AttributePropertyBackup = AttributeAllocationInfo->Property;
					const EAttributeStorageResult RemoveResult = UnregisterAttribute(OriginalKey);
					if (!IsAttributeStorageResultSuccess(RemoveResult))
					{
						return RemoveResult;
					}
					AttributeAllocationInfo = nullptr;
					//Reallocate a new one
					AttributeAllocationInfo = &AttributeAllocationTable.Add(OriginalKey);
					AttributeAllocationInfo->Type = ValueType;
					AttributeAllocationInfo->Offset = AttributeStorage.AddZeroed(ValueSize);
					//Force the specified attribute property
					AttributeAllocationInfo->Property = AttributePropertyBackup;
				}
				else
				{
					//In case we reuse the allocation table, simply adjust the waste memory counter
					FragmentedMemoryCost += AttributeAllocationInfo->Size - ValueSize;
				}

				//We can recycle allocation table if the size is equal or smaller, so we have to always set it
				AttributeAllocationInfo->Size = ValueSize;

				//Set the hash from the value
				AttributeAllocationInfo->Hash = GetValueHash(DataPtr, ValueSize);

				if (ValueSize > 0)
				{
					uint8* StorageData = AttributeStorage.GetData();
					FMemory::Memcpy(&StorageData[AttributeAllocationInfo->Offset], DataPtr, ValueSize);
				}

				return EAttributeStorageResult::Operation_Success;
			}
	
			template<typename ArrayType>
			EAttributeStorageResult GenericArrayGetAttribute(const FAttributeKey& ElementAttributeKey, ArrayType& OutValue) const
			{
				//Lock the storage
				FScopeLock ScopeLock(&StorageMutex);

				const EAttributeTypes ValueType = TAttributeTypeTraits<ArrayType>::GetType();
				if (ValueType != EAttributeTypes::ByteArray && ValueType != EAttributeTypes::ByteArray64)
				{
					return EAttributeStorageResult::Operation_Error_WrongType;
				}

				const FAttributeAllocationInfo* AttributeAllocationInfo = AttributeAllocationTable.Find(ElementAttributeKey);
				if (AttributeAllocationInfo)
				{
					if (AttributeAllocationInfo->Type != ValueType)
					{
						return EAttributeStorageResult::Operation_Error_WrongType;
					}
					if (!AttributeStorage.IsValidIndex(AttributeAllocationInfo->Offset))
					{
						return EAttributeStorageResult::Operation_Error_AttributeAllocationCorrupted;
					}
				}
				else
				{
					//The key do not exist
					return EAttributeStorageResult::Operation_Error_CannotFoundKey;
				}

				if (AttributeAllocationInfo->Size == 0)
				{
					OutValue.Empty();
					return EAttributeStorageResult::Operation_Success;
				}

				//Allocate the ByteArray
				OutValue.AddZeroed(AttributeAllocationInfo->Size);
				//Copy into the buffer
				const uint8* StorageData = AttributeStorage.GetData();
				FMemory::Memcpy(OutValue.GetData(), &StorageData[AttributeAllocationInfo->Offset], AttributeAllocationInfo->Size);
				return EAttributeStorageResult::Operation_Success;
			}

			/** The attribute allocation table is use to index the attributes into the storage. */
			TMap<FAttributeKey, FAttributeAllocationInfo> AttributeAllocationTable;

			/** The storage of the data point by the attribute allocation table */
			TArray64<uint8> AttributeStorage;

			/**
			 * The total size of the fragmented holes in the AttributeStorage (memory waste).
			 * A Hole is create each time we remove an attribute.
			 */
			uint64 FragmentedMemoryCost = 0;
	
			/**
			 * if FragmentedMemoryCost > AttributeStorage.Num*DefragRatio then defrag.
			 * This is use whenever we remove attribute or change DefragRatio value.
			 */
			float DefragRatio = 0.1;

			/**
			 * Mutex use when accessing or modifying the storage
			 */
			mutable FCriticalSection StorageMutex;
		};

		/************************************************************************/
		/* Default FVariant traits for built-in types */

		/** Implements variant type traits for the built-in bool type. */
		template<> struct TAttributeTypeTraits<bool>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Bool; }
		};


		/** Implements variant type traits for the built-in FBox type. */
		template<> struct TAttributeTypeTraits<FBox>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Box; }
		};


		/** Implements variant type traits for the built-in FBoxSphereBounds type. */
		template<> struct TAttributeTypeTraits<FBoxSphereBounds>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::BoxSphereBounds; }
		};


		/** Implements variant type traits for byte arrays. */
		template<> struct TAttributeTypeTraits<TArray<uint8> >
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::ByteArray; }
		};

		/** Implements variant type traits for byte array64s. */
		template<> struct TAttributeTypeTraits<TArray64<uint8> >
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::ByteArray64; }
		};


		/** Implements variant type traits for the built-in FColor type. */
		template<> struct TAttributeTypeTraits<FColor>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Color; }
		};


		/** Implements variant type traits for the built-in FDateTime type. */
		template<> struct TAttributeTypeTraits<FDateTime>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::DateTime; }
		};


		/** Implements variant type traits for the built-in double type. */
		template<> struct TAttributeTypeTraits<double>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Double; }
		};


		/** Implements variant type traits for enumeration types. */
		template<typename EnumType> struct TAttributeTypeTraits<TEnumAsByte<EnumType> >
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Enum; }
		};


		/** Implements variant type traits for the built-in float type. */
		template<> struct TAttributeTypeTraits<float>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Float; }
		};


		/** Implements variant type traits for the built-in FGuid type. */
		template<> struct TAttributeTypeTraits<FGuid>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Guid; }
		};


		/** Implements variant type traits for the built-in int8 type. */
		template<> struct TAttributeTypeTraits<int8>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Int8; }
		};


		/** Implements variant type traits for the built-in int16 type. */
		template<> struct TAttributeTypeTraits<int16>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Int16; }
		};


		/** Implements variant type traits for the built-in int32 type. */
		template<> struct TAttributeTypeTraits<int32>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Int32; }
		};


		/** Implements variant type traits for the built-in int64 type. */
		template<> struct TAttributeTypeTraits<int64>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Int64; }
		};


		/** Implements variant type traits for the built-in FIntPoint type. */
		template<> struct TAttributeTypeTraits<FIntPoint>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::IntPoint; }
		};


		/** Implements variant type traits for the built-in FIntVector type. */
		template<> struct TAttributeTypeTraits<FIntVector>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::IntVector; }
		};


		/** Implements variant type traits for the built-in FIntRect type. */
		template<> struct TAttributeTypeTraits<FIntRect>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::IntRect; }
		};


		/** Implements variant type traits for the built-in FLinearColor type. */
		template<> struct TAttributeTypeTraits<FLinearColor>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::LinearColor; }
		};


		/** Implements variant type traits for the built-in FMatrix type. */
		template<> struct TAttributeTypeTraits<FMatrix>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Matrix; }
		};


		/** Implements variant type traits for the built-in FPlane type. */
		template<> struct TAttributeTypeTraits<FPlane>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Plane; }
		};


		/** Implements variant type traits for the built-in FQuat type. */
		template<> struct TAttributeTypeTraits<FQuat>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Quat; }
		};


		/** Implements variant type traits for the built-in FName type. */
		template<> struct TAttributeTypeTraits<FName>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Name; }
		};


		/** Implements variant type traits for the built-in FRandomStream type. */
		template<> struct TAttributeTypeTraits<FRandomStream>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::RandomStream; }
		};


		/** Implements variant type traits for the built-in FRotator type. */
		template<> struct TAttributeTypeTraits<FRotator>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Rotator; }
		};


		/** Implements variant type traits for the built-in FString type. */
		template<> struct TAttributeTypeTraits<FString>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::String; }
		};


		/** Implements variant type traits for the built-in FTimespan type. */
		template<> struct TAttributeTypeTraits<FTimespan>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Timespan; }
		};


		/** Implements variant type traits for the built-in FTransform type. */
		template<> struct TAttributeTypeTraits<FTransform>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Transform; }
		};


		/** Implements variant type traits for the built-in FTwoVectors type. */
		template<> struct TAttributeTypeTraits<FTwoVectors>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::TwoVectors; }
		};


		/** Implements variant type traits for the built-in uint8 type. */
		template<> struct TAttributeTypeTraits<uint8>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::UInt8; }
		};


		/** Implements variant type traits for the built-in uint16 type. */
		template<> struct TAttributeTypeTraits<uint16>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::UInt16; }
		};


		/** Implements variant type traits for the built-in uint32 type. */
		template<> struct TAttributeTypeTraits<uint32>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::UInt32; }
		};


		/** Implements variant type traits for the built-in uint64 type. */
		template<> struct TAttributeTypeTraits<uint64>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::UInt64; }
		};


		/** Implements variant type traits for the built-in FVector type. */
		template<> struct TAttributeTypeTraits<FVector>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Vector; }
		};


		/** Implements variant type traits for the built-in FVector2D type. */
		template<> struct TAttributeTypeTraits<FVector2D>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Vector2d; }
		};


		/** Implements variant type traits for the built-in FVector4 type. */
		template<> struct TAttributeTypeTraits<FVector4>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Vector4; }
		};


		/** Implements variant type traits for the built-in NetworkGUID type. */
		template<> struct TAttributeTypeTraits<FNetworkGUID>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::NetworkGUID; }
		};

		/** Implements variant type traits for the built-in Vector2DHalf type. */
		template<> struct TAttributeTypeTraits<FVector2DHalf>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Vector2DHalf; }
		};

		/** Implements variant type traits for the built-in Float16 type. */
		template<> struct TAttributeTypeTraits<FFloat16>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Float16; }
		};

		/** Implements variant type traits for the built-in CapsuleShape type. */
		template<> struct TAttributeTypeTraits<FCapsuleShape>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::CapsuleShape; }
		};

		/** Implements variant type traits for the built-in DualQuat type. */
		template<> struct TAttributeTypeTraits<FDualQuat>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::DualQuat; }
		};

		/** Implements variant type traits for the built-in OrientedBox type. */
		template<> struct TAttributeTypeTraits<FOrientedBox>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::OrientedBox; }
		};

		/** Implements variant type traits for the built-in Ray type. */
		template<> struct TAttributeTypeTraits<FRay>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Ray; }
		};

		/** Implements variant type traits for the built-in Sphere type. */
		template<> struct TAttributeTypeTraits<FSphere>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::Sphere; }
		};

		/** Implements variant type traits for the built-in FrameNumber type. */
		template<> struct TAttributeTypeTraits<FFrameNumber>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::FrameNumber; }
		};

		/** Implements variant type traits for the built-in FrameRate type. */
		template<> struct TAttributeTypeTraits<FFrameRate>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::FrameRate; }
		};

		/** Implements variant type traits for the built-in FrameTime type. */
		template<> struct TAttributeTypeTraits<FFrameTime>
		{
			static CONSTEXPR EAttributeTypes GetType() { return EAttributeTypes::FrameTime; }
		};

	} //ns interchange
}
