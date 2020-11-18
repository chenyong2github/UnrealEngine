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
	return InterchangePrivateNodeBase::GetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AttributeName, AttributeType)															\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																				\
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER(NodeClassName, AttributeName, AttributeType, AssetType)														\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																			\
	if(InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue))	\
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
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#endif //#if WITH_ENGINE


//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		DECLARE_DELEGATE_RetVal_OneParam(bool, FApplyAttributeToAsset, UObject*);
		DECLARE_DELEGATE_RetVal_OneParam(bool, FFillAttributeToAsset, UObject*);

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

			static const FAttributeKey& DependencyCountKey()
			{
				static FAttributeKey AttributeKey(TEXT("__DEPENDENCY_COUNT_"));
				return AttributeKey;
			}

			static const FAttributeKey& DependencyBaseKey()
			{
				static FAttributeKey AttributeKey(TEXT("__DEPENDENCY_INDEX_"));
				return AttributeKey;
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
	{}

	virtual ~UInterchangeBaseNode() = default;

	/**
	 * Initialize the base data of the node
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void InitializeNode(const FName& UniqueID, const FName& DisplayLabel);

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
		const UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(NodeAttributeKey, Value);
		
		if (IsAttributeStorageResultSuccess(Result))
		{
			return Attributes.GetAttributeHandle<T>(NodeAttributeKey);
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
		return Attributes.GetAttributeType(NodeAttributeKey);
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
		return Attributes.GetAttributeHandle<T>(NodeAttributeKey);
	}

	/**
	 * Return the unique id pass in the constructor.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FName GetUniqueID() const;

	/**
	 * Return the display label.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FName GetDisplayLabel() const;

	/**
	 * Change the display label.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetDisplayLabel(FName DisplayName);

	/**
	 * Return the parent unique id. In case the attribute does not exist it will return InvalidNodeUID()
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FName GetParentUID() const;

	/**
	 * Set the parent unique id.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetParentUID(FName ParentUID);

	/**
	 * This function allow to retrieve the dependency for this object.
	 * 
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetDependecies(TArray<FName>& OutDependencies ) const;

	/**
	 * Add one dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetDependencyUID(FName DependencyUID);

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
	static FName InvalidNodeUID();

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
		UE::Interchange::FAttributeStorage::CompareStorage(NodeA->Attributes, NodeB->Attributes, RemovedAttributes, AddedAttributes, ModifiedAttributes);
	}
	
	static void CopyStorageAttributes(const UInterchangeBaseNode* SourceNode, UInterchangeBaseNode* DestinationNode, TArray<UE::Interchange::FAttributeKey>& AttributeKeys)
	{
		UE::Interchange::FAttributeStorage::CopyStorageAttributes(SourceNode->Attributes, DestinationNode->Attributes, AttributeKeys);
	}

	static void CopyStorage(const UInterchangeBaseNode* SourceNode, UInterchangeBaseNode* DestinationNode)
	{
		DestinationNode->Attributes = SourceNode->Attributes;
	}
	
	UPROPERTY()
	mutable FSoftObjectPath ReferenceObject;
protected:
	/** The storage use to store the Key value attribute for this node. */
	UE::Interchange::FAttributeStorage Attributes;

	/* This array hold the delegate to apply the attribute that has to be set on an UObject */
	TMap<UClass*, TArray<UE::Interchange::FApplyAttributeToAsset>> ApplyCustomAttributeDelegates;

	TMap<UClass*, TArray<UE::Interchange::FFillAttributeToAsset>> FillCustomAttributeDelegates;

	bool bIsInitialized = false;
};
