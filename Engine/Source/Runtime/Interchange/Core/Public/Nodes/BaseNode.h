// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"

/**
 * Internal Helper to get set custom property for class that derive from FBaseNode. This is use by the macro IMPLEMENT_UOD_ATTRIBUTE.
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
	bool GetCustomAttribute(const Interchange::FAttributeStorage& Attributes, const Interchange::FAttributeKey& AttributeKey, const FString& OperationName, ValueType& OutAttributeValue)
	{
		if (!Attributes.ContainAttribute(AttributeKey))
		{
			return false;
		}
		Interchange::FAttributeStorage::TAttributeHandle<ValueType> AttributeHandle = Attributes.GetAttributeHandle<ValueType>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			return false;
		}
		Interchange::EAttributeStorageResult Result = AttributeHandle.Get(OutAttributeValue);
		if (!Interchange::IsAttributeStorageResultSuccess(Result))
		{
			Interchange::LogAttributeStorageErrors(Result, OperationName, AttributeKey);
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
	bool SetCustomAttribute(Interchange::FAttributeStorage& Attributes, const Interchange::FAttributeKey& AttributeKey, const FString& OperationName, const ValueType& AttributeValue)
	{
		Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(AttributeKey, AttributeValue);
		if (!Interchange::IsAttributeStorageResultSuccess(Result))
		{
			Interchange::LogAttributeStorageErrors(Result, OperationName, AttributeKey);
			return false;
		}
		return true;
	}
}

/**
 * Use those macro to create Get/Set/ApplyToAsset for an attribute you want to support in your custom UOD node that derive from FBaseNode.
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
#if WITH_ENGINE
#define IMPLEMENT_NODE_ATTRIBUTE(NodeClassName, AttributeName, AttributeType, AssetType, EnumType)										\
private:																																\
const FAttributeKey Macro_Custom##AttributeName##Key = Interchange::FAttributeKey(TEXT(#AttributeName));								\
bool ApplyCustom##AttributeName##ToAsset(UObject* Asset) const																			\
{																																		\
	if (!Asset)																															\
	{																																	\
		return false;																													\
	}																																	\
	AssetType* TypedObject = Cast<AssetType>(Asset);																					\
	if (!TypedObject)																													\
	{																																	\
		return false;																													\
	}																																	\
	AttributeType ValueData;																											\
	if (GetCustom##AttributeName(ValueData))																							\
	{																																	\
		TypedObject->AttributeName = EnumType(ValueData);																				\
		return true;																													\
	}																																	\
	return false;																														\
}																																		\
																																		\
public:																																	\
bool GetCustom##AttributeName(AttributeType& AttributeValue) const																		\
{																																		\
	FString OperationName = GetTypeName() + TEXT(".Get##AttributeName");																\
	return InterchangePrivateNodeBase::GetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);	\
}																																		\
																																		\
bool SetCustom##AttributeName(const AttributeType& AttributeValue, bool bAddApplyDelegate = true)										\
{																																		\
	FString OperationName = GetTypeName() + TEXT(".Set##AttributeName");																\
	if(InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue))	\
	{																																	\
		if(bAddApplyDelegate)																											\
		{																																\
			TArray<FApplyAttributeToAsset>& Delegates = ApplyCustomAttributeDelegates.FindOrAdd(AssetType::StaticClass());				\
			Delegates.Add(FApplyAttributeToAsset::CreateRaw(this, &NodeClassName::ApplyCustom##AttributeName##ToAsset));				\
		}																																\
		return true;																													\
	}																																	\
	return false;																														\
}
#else //WITH_ENGINE
//AssetType is not use when we are not compiling with Engine
//We then skip the ApplyCustom##AttributeName##ToAsset function
//Because of this we also skip to add the delegate to ApplyCustomAttributeDelegates
//Example UTexture is define in engine, a TextureNode compile without the engine will have the attribute
//setter/getter but will not be able to apply it to a UTexture asset. We use this in out of prcess parser and the graph is reconstruct
//in the interchange framework which compile with engine so it has the UObject setter call by the interchange factory
#define IMPLEMENT_NODE_ATTRIBUTE(NodeClassName, AttributeName, AttributeType, AssetType, EnumType)										\
private:																																\
const FAttributeKey Macro_Custom##AttributeName##Key = Interchange::FAttributeKey(TEXT(#AttributeName));								\
																																		\
public:																																	\
bool GetCustom##AttributeName(AttributeType& AttributeValue) const																		\
{																																		\
	FString OperationName = GetTypeName() + TEXT(".Get##AttributeName");																\
	return InterchangePrivateNodeBase::GetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);	\
}																																		\
																																		\
bool SetCustom##AttributeName(const AttributeType& AttributeValue, bool bAddApplyDelegate = true)										\
{																																		\
	FString OperationName = GetTypeName() + TEXT(".Set##AttributeName");																\
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);	\
}
#endif //else WITH_ENGINE


//Interchange namespace
namespace Interchange
{

DECLARE_DELEGATE_RetVal_OneParam(bool, FApplyAttributeToAsset, UObject*);

typedef FName FNodeUniqueID;


/**
 * Helper struct use to declare static const data we use in the FBaseNode
 * Node that derive from FBaseNode can also add a struct that derive from this one to add there static data
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

/**
 * This class is used to store and retrieve key value attributes. The attributes are store in a generic FAttributeStorage which serialize the value in a TArray64<uint8>
 * See Interchange::EAttributeTypes to know the supported template types
 * This is an abstract class. This is the base class of the interchange node graph format, all class in this format should derive from this class
 */

class FBaseNode
{
public:
	virtual ~FBaseNode() = default;

 	FBaseNode(const FBaseNode& Other)
 	{
 		Attributes = Other.Attributes;
 	}

	/**
	 * Constructor
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 *
	 */
	FBaseNode(const FNodeUniqueID& UniqueID, const FName& DisplayLabel)
	{
		EAttributeStorageResult Result = Attributes.RegisterAttribute(FBaseNodeStaticData::UniqueIDKey(), UniqueID, EAttributeProperty::NoHash);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), FBaseNodeStaticData::UniqueIDKey());
		}

		Result = Attributes.RegisterAttribute(FBaseNodeStaticData::DisplayLabelKey(), DisplayLabel, EAttributeProperty::NoHash);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), FBaseNodeStaticData::DisplayLabelKey());
		}
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const
	{
		const FString TypeName = TEXT("BaseNode");
		return TypeName;
	}

	/**
	 * Add an attribute to the node
	 * @param NodeAttributeKey - The key of the attribute
	 * @param Value - The attribute value.
	 *
	 */
	template<typename T>
	FAttributeStorage::TAttributeHandle<T> RegisterAttribute(const FAttributeKey& NodeAttributeKey, const T& Value)
	{
		const EAttributeStorageResult Result = Attributes.RegisterAttribute(NodeAttributeKey, Value);
		
		if (IsAttributeStorageResultSuccess(Result))
		{
			return Attributes.GetAttributeHandle<T>(NodeAttributeKey);
		}
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), NodeAttributeKey);
		return FAttributeStorage::TAttributeHandle<T>();
	}

	/**
	 * Return true if the node contain an attribute with the specified Key
	 * @param nodeAttributeKey - The key of the searched attribute
	 *
	 */
	virtual bool HasAttribute(const FAttributeKey& NodeAttributeKey) const
	{
		return Attributes.ContainAttribute(NodeAttributeKey);
	}


	/**
	 * This function returnan  attribute handle for the specified Key.
	 * If there is an issue with the KEY or storage the method will trip a check, always make sure you have a valid key before calling this
	 * 
	 * @param NodeAttributeKey - The key of the attribute
	 */
	template<typename T>
	FAttributeStorage::TAttributeHandle<T> GetAttributeHandle(const FAttributeKey& NodeAttributeKey) const
	{
		return Attributes.GetAttributeHandle<T>(NodeAttributeKey);
	}

	/**
	 * Return the unique id pass in the constructor.
	 *
	 */
	virtual FNodeUniqueID GetUniqueID() const
	{
		FNodeUniqueID UniqueID = NAME_None;
		Attributes.GetAttributeHandle<FNodeUniqueID>(FBaseNodeStaticData::UniqueIDKey()).Get(UniqueID);
		return UniqueID;
	}

	/**
	 * Return the display label pass in the constructor.
	 *
	 */
	virtual FName GetDisplayLabel() const
	{
		checkSlow(Attributes.ContainAttribute(FBaseNodeStaticData::DisplayLabelKey()));
		FName DisplayLabel = NAME_None;
		Attributes.GetAttributeHandle<FName>(FBaseNodeStaticData::DisplayLabelKey()).Get(DisplayLabel);
		return DisplayLabel;
	}

	/**
	 * Return the parent unique id. In case the attribute does not exist it will return InvalidNodeUID()
	 */
	virtual FNodeUniqueID GetParentUID() const
	{
		if (!Attributes.ContainAttribute(FBaseNodeStaticData::ParentIDKey()))
		{
			return InvalidNodeUID();
		}

		FNodeUniqueID ParentUniqueID = NAME_None;
		FAttributeStorage::TAttributeHandle<FNodeUniqueID> Handle = Attributes.GetAttributeHandle<FNodeUniqueID>(FBaseNodeStaticData::ParentIDKey());
		if(Handle.IsValid())
		{
			Handle.Get(ParentUniqueID);
			return ParentUniqueID;
		}
		return InvalidNodeUID();
	}

	/**
	 * Set the parent unique id.
	 */
	virtual bool SetParentUID(FNodeUniqueID ParentUID)
	{
		EAttributeStorageResult Result = Attributes.RegisterAttribute(FBaseNodeStaticData::ParentIDKey(), ParentUID);
		if(IsAttributeStorageResultSuccess(Result))
		{
			FAttributeStorage::TAttributeHandle<FNodeUniqueID> Handle = Attributes.GetAttributeHandle<FNodeUniqueID>(FBaseNodeStaticData::ParentIDKey());
			return Handle.IsValid();
		}
		return false;
	}

	/**
	 * This function allow to retrieve the dependency for this object.
	 * 
	 */
	virtual void GetDependecies(TArray<FNodeUniqueID>& OutDependencies ) const
	{
		OutDependencies.Reset();

		int32 DepenedenciesCount = 0;
		if (!Attributes.ContainAttribute(FBaseNodeStaticData::DependencyCountKey()))
		{
			return;
		}
		FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(FBaseNodeStaticData::DependencyCountKey());
		if (!Handle.IsValid())
		{
			return;
		}
		Handle.Get(DepenedenciesCount);
		for (int32 DepIndex = 0; DepIndex < DepenedenciesCount; ++DepIndex)
		{
			FString DepIndexKeyString = FBaseNodeStaticData::DependencyBaseKey().Key.ToString() + FString::FromInt(DepIndex);
			FAttributeKey DepIndexKey(*DepIndexKeyString);
			FAttributeStorage::TAttributeHandle<FNodeUniqueID> HandleDep = Attributes.GetAttributeHandle<FNodeUniqueID>(DepIndexKey);
			if (!HandleDep.IsValid())
			{
				continue;
			}
			FNodeUniqueID& NodeUniqueID = OutDependencies.AddDefaulted_GetRef();
			HandleDep.Get(NodeUniqueID);
		}
	}

	/**
	 * Add one dependency to this object.
	 */
	virtual bool SetDependencyUID(FNodeUniqueID DependencyUID)
	{
		if (!Attributes.ContainAttribute(FBaseNodeStaticData::DependencyCountKey()))
		{
			const int32 DependencyCount = 0;
			EAttributeStorageResult Result = Attributes.RegisterAttribute<int32>(FBaseNodeStaticData::DependencyCountKey(), DependencyCount);
		}
		FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(FBaseNodeStaticData::DependencyCountKey());
		if(!ensure(Handle.IsValid()))
		{
			return false;
		}
		int32 DepIndex = 0;
		Handle.Get(DepIndex);
		FString DepIndexKeyString = FBaseNodeStaticData::DependencyBaseKey().Key.ToString() + FString::FromInt(DepIndex);
		FAttributeKey DepIndexKey(*DepIndexKeyString);
		//Increment the counter
		DepIndex++;
		Handle.Set(DepIndex);

		EAttributeStorageResult DepResult = Attributes.RegisterAttribute<FNodeUniqueID>(DepIndexKey, DependencyUID);
		return true;
	}

	/**
	 * IsEnable true mean that the node will be import/export, if false it will be discarded.
	 * Return false if this node was disabled. Return true if the attribute is not there or if it was enabled.
	 */
	virtual bool IsEnabled() const
	{
		FAttributeStorage::TAttributeHandle<bool> Handle = Attributes.GetAttributeHandle<bool>(FBaseNodeStaticData::IsEnabledKey());
		if (Handle.IsValid())
		{
			bool bIsEnabled = false;
			Handle.Get(bIsEnabled);
			return bIsEnabled;
		}
		return true;
	}

	/**
	 * Set the IsEnable attribute to determine if this node should be part of the import/export process
	 * @param bIsEnabled - The enabled state we want to set this node. True will import/export the node, fals will not.
	 * @return true if it was able to set the attribute, false otherwise.
	 */
	virtual bool SetEnabled(const bool bIsEnabled)
	{
		EAttributeStorageResult Result = Attributes.RegisterAttribute(FBaseNodeStaticData::IsEnabledKey(), bIsEnabled);
		if (IsAttributeStorageResultSuccess(Result))
		{
			FAttributeStorage::TAttributeHandle<bool> Handle = Attributes.GetAttributeHandle<bool>(FBaseNodeStaticData::IsEnabledKey());
			return Handle.IsValid();
		}
		return false;
	}

	/**
	 * Return a FGuid build from the FSHA1 of all the attribute data contain in the node.
	 *
	 * @note the attribute are sorted by key when building the FSHA1 data. The hash will be deterministic for the same data whatever
	 * the order we add the attributes.
	 * This function interface is pure virtual
	 */
	virtual FGuid GetHash() const
	{
		return Attributes.GetStorageHash();
	}

	/**
	 * Optional, Any node that can import/export an asset should return the UClass of the asset so we can find factory/writer
	 */
	virtual class UClass* GetAssetClass() const
	{
		return nullptr;
	}

	/**
	 * Normally there should be no other data need to be serialize even in the derived class
	 * Everything should be in the attribute storage. Payload source data path should be store in the attribute storage.
	 * We add this virtual function in case an node need to serialize more stuff.
	 */
	virtual FArchive& Serialize(FArchive& Ar)
	{
		Ar << Attributes;
		return Ar;
	}
	
	friend FArchive& operator<<(FArchive& Ar, FBaseNode& Node)
	{
		//Call the  virtual version of the serialize.
		return Node.Serialize(Ar);
	}

	/** Return the invalid unique ID */
	static FNodeUniqueID InvalidNodeUID()
	{
		return NAME_None;
	}

	void ApplyAllCustomAttributeToAsset(UObject* Object) const
	{
		UClass* ObjectClass = Object->GetClass();
		for (const TPair<UClass*, TArray<FApplyAttributeToAsset>>& ClassDelegatePair : ApplyCustomAttributeDelegates)
		{
			if(ObjectClass->IsChildOf(ClassDelegatePair.Key))
			{
				for (const FApplyAttributeToAsset& Delegate : ClassDelegatePair.Value)
				{
					if(Delegate.IsBound())
					{
						Delegate.Execute(Object);
					}
				}
			}
		}
	}

protected:
	/** The storage use to store the Key value attribute for this node. */
	FAttributeStorage Attributes;

	/* This array hold the delegate to apply the attribute that has to be set on an UObject */
	TMap<UClass*, TArray<FApplyAttributeToAsset>> ApplyCustomAttributeDelegates;
};

}
