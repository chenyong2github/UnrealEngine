// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Nodes/BaseNode.h"
#include "Nodes/BaseNodeContainer.h"

#include "InterchangeBaseNodeContainerAdapter.generated.h"

/**
 * Interchange node container adapter is a helper class to do some operations on a FBaseNodeContainer. This adapter is available in python and blueprint.
 *
 */
UCLASS(BlueprintType)
class INTERCHANGECORE_API UInterchangeBaseNodeContainerAdapter : public UObject
{
	GENERATED_BODY()
public:
	
	/**
	 * Set the Container the adapter will work on.
	 *
	 * @param InBaseNodeContainer - Pointer on a FBaseNodeContainer, the adapter will change the container directly.
	 */
	void SetBaseNodeContainer(Interchange::FBaseNodeContainer* InBaseNodeContainer)
	{
		BaseNodeContainer = InBaseNodeContainer;
	}
	
	/**
	 * Return all root node(the one that do not have nay parent) contain in the BaseNodeContainer control by this adapter
	 *
	 * @param Roots - The array where we store the result.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	bool GetRoots(TArray<FName>& Roots) const
	{
		if (!BaseNodeContainer)
		{
			return false;
		}
		BaseNodeContainer->GetRoots(Roots);
		return true;
	}

	/** Return the first root node(the one that do not have nay parent) we find in the specified BaseNodeContainer */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	FName GetFirstRoot() const
	{
		if (!BaseNodeContainer)
		{
			return NAME_None;
		}
		TArray<Interchange::FNodeUniqueID> Roots;
		BaseNodeContainer->GetRoots(Roots);
		if (Roots.Num() > 0)
		{
			return Roots[0];
		}
		return NAME_None;
	}

	/**
	 * Return the parent of the specified node
	 *
	 * @param NodeIdentity - The specified node we query the parent
	 * @return the parent UID, if there is no parent it return NAME_None
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	FName GetNodeParent(const FName NodeIdentity) const
	{
		if (!BaseNodeContainer)
		{
			return NAME_None;
		}
		Interchange::FBaseNode* BaseNode = BaseNodeContainer->GetNode(NodeIdentity);
		if (!BaseNode)
		{
			return NAME_None;
		}
		return BaseNode->GetParentUID();
	}

	/**
	 * Find all the children of the specified node
	 *
	 * @param NodeIdentity - The specified node we query the child
	 * @param OutNodeChilds - The array of children where we store the result
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	void GetNodeChild(const FName NodeIdentity, TArray<FName>& OutNodeChilds) const
	{
		if (!BaseNodeContainer)
		{
			return;
		}
		OutNodeChilds = BaseNodeContainer->GetNodeChildrenUIDs(NodeIdentity);
		return;
	}

	/**
	 * Set the node parent.
	 *
	 * @param NodeIdentity - The node we want to set the parent
	 * @param ParentNodeIdentity - The parent node
	 * @return true if the parent was properly set, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	bool SetNodeParent(const FName NodeIdentity, const FName ParentNodeIdentity)
	{
		if (!BaseNodeContainer)
		{
			return false;
		}
		
		return BaseNodeContainer->SetNodeParentUID(NodeIdentity, ParentNodeIdentity);
	}

	/**
	 * Return true if the node should be imported/exported.
	 *
	 * @param NodeIdentity - The node we want to query the enabled state
	 * @return true if the node should be imported/exported.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	bool IsNodeEnabled(const FName NodeIdentity) const
	{
		if (!BaseNodeContainer)
		{
			return false;
		}
		Interchange::FBaseNode* BaseNode = BaseNodeContainer->GetNode(NodeIdentity);
		if (!BaseNode)
		{
			return false;
		}
		return BaseNode->IsEnabled();
	}

	/**
	 * Set the node imported/exported state.
	 *
	 * @param NodeIdentity - The node we want to set the enabled state
	 * @param bIsEnabled - The enabled state to set
	 * @return true if the node enabled state was set properly. false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Container")
	bool SetNodeEnabled(const FName NodeIdentity, const bool bIsEnabled)
	{
		if (!BaseNodeContainer)
		{
			return false;
		}
		Interchange::FBaseNode* BaseNode = BaseNodeContainer->GetNode(NodeIdentity);
		if (!BaseNode)
		{
			return false;
		}
		return BaseNode->SetEnabled(bIsEnabled);
	}

	/**
	 * Fill the OutValue parameter with the content of the attribute point by AttributeName
	 *
	 * @param NodeIdentity - The node we want to query the attribute
	 * @param AttributeName - The attribute name use to retrieve the actual attribute value
	 * @param OutValue - We store the attribute value into this parameter
	 * @return true if we successfully retrieve the attribute value, false otherwise.
	 */
	template<typename T>
	bool GetNodeAttributeValue(const FName NodeIdentity, const FName AttributeName, T& OutValue)
	{
		if (!BaseNodeContainer)
		{
			return false;
		}
		Interchange::FBaseNode* BaseNode = BaseNodeContainer->GetNode(NodeIdentity);
		if (!BaseNode)
		{
			return false;
		}
		Interchange::FAttributeKey AttributeKey(AttributeName);
		Interchange::FAttributeStorage::TAttributeHandle<T> AttributeHandle = BaseNode->GetAttributeHandle<T>(AttributeKey);
		AttributeHandle.Get(OutValue);
		return true;
	}

	/**
	 * Set the Value parameter in the attribute point by AttributeName
	 *
	 * @param NodeIdentity - The node we want to query the attribute
	 * @param AttributeName - The attribute name use to retrieve the actual attribute we want to set the value
	 * @param Value - The value we want to store
	 * @return true if we successfully set the attribute value, false otherwise.
	 */
	template<typename T>
	bool SetNodeAttributeValue(const FName NodeIdentity, const FName AttributeName, const T& Value)
	{
		if (!BaseNodeContainer)
		{
			return false;
		}
		Interchange::FBaseNode* BaseNode = BaseNodeContainer->GetNode(NodeIdentity);
		if (!BaseNode)
		{
			return false;
		}
		Interchange::FAttributeKey AttributeKey(AttributeName);
		Interchange::FAttributeStorage::TAttributeHandle<T> AttributeHandle = BaseNode->GetAttributeHandle<T>(AttributeKey);
		if(AttributeHandle.IsValid())
		{
			AttributeHandle.Set(Value);
		}
		else
		{
			BaseNode->RegisterAttribute<T>(AttributeKey, Value);
		}

		return true;
	}

	//////////////////////////////////////////////////////////////////////////
	//Attribute getter interface, blueprint callable for all supported type
	
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueBool(const FName NodeIdentity, const FName AttributeName, bool& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueFloat(const FName NodeIdentity, const FName AttributeName, float& OutValue);
	//type double not supported in blueprint
 	bool GetNodeAttributeValueDouble(const FName NodeIdentity, const FName AttributeName, double& OutValue);
	//type int8 not supported in blueprint
 	bool GetNodeAttributeValueInt8(const FName NodeIdentity, const FName AttributeName, int8& OutValue);
	//type int16 not supported in blueprint
 	bool GetNodeAttributeValueInt16(const FName NodeIdentity, const FName AttributeName, int16& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueInt32(const FName NodeIdentity, const FName AttributeName, int32& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueInt64(const FName NodeIdentity, const FName AttributeName, int64& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueUint8(const FName NodeIdentity, const FName AttributeName, uint8& OutValue);
	//type uint16 not supported in blueprint
 	bool GetNodeAttributeValueUint16(const FName NodeIdentity, const FName AttributeName, uint16& OutValue);
	//type uint32 not supported in blueprint
 	bool GetNodeAttributeValueUint32(const FName NodeIdentity, const FName AttributeName, uint32& OutValue);
	//type uint64 not supported in blueprint
 	bool GetNodeAttributeValueUint64(const FName NodeIdentity, const FName AttributeName, uint64& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueVector(const FName NodeIdentity, const FName AttributeName, FVector& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueVector2D(const FName NodeIdentity, const FName AttributeName, FVector2D& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueMatrix(const FName NodeIdentity, const FName AttributeName, FMatrix& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueBox(const FName NodeIdentity, const FName AttributeName, FBox& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueBoxSphereBound(const FName NodeIdentity, const FName AttributeName, FBoxSphereBounds& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueArray(const FName NodeIdentity, const FName AttributeName, TArray<uint8>& OutValue);
	//type TArray64<uint8> not supported in blueprint
 	bool GetNodeAttributeValueArray64(const FName NodeIdentity, const FName AttributeName, TArray64<uint8>& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueColor(const FName NodeIdentity, const FName AttributeName, FColor& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueDateTime(const FName NodeIdentity, const FName AttributeName, FDateTime& OutValue);
	//We use a uint8 to get the enum value, blueprint only support enum of uint8 type
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueEnum(const FName NodeIdentity, const FName AttributeName, uint8& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueGuid(const FName NodeIdentity, const FName AttributeName, FGuid& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueIntPoint(const FName NodeIdentity, const FName AttributeName, FIntPoint& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueIntVecor(const FName NodeIdentity, const FName AttributeName, FIntVector& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueLinearColor(const FName NodeIdentity, const FName AttributeName, FLinearColor& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValuePlane(const FName NodeIdentity, const FName AttributeName, FPlane& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueQuat(const FName NodeIdentity, const FName AttributeName, FQuat& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueName(const FName NodeIdentity, const FName AttributeName, FName& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueRandomStream(const FName NodeIdentity, const FName AttributeName, FRandomStream& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueRotator(const FName NodeIdentity, const FName AttributeName, FRotator& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueString(const FName NodeIdentity, const FName AttributeName, FString& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueTimespan(const FName NodeIdentity, const FName AttributeName, FTimespan& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueTransform(const FName NodeIdentity, const FName AttributeName, FTransform& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueTwoVectors(const FName NodeIdentity, const FName AttributeName, FTwoVectors& OutValue);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool GetNodeAttributeValueVector4(const FName NodeIdentity, const FName AttributeName, FVector4& OutValue);

	//////////////////////////////////////////////////////////////////////////
	//Attribute setter interface, blueprint callable for all supported type
	
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueBool(const FName NodeIdentity, const FName AttributeName, const bool Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueFloat(const FName NodeIdentity, const FName AttributeName, const float Value);
	//type double not supported in blueprint
 	bool SetNodeAttributeValueDouble(const FName NodeIdentity, const FName AttributeName, const double Value);
	//type int8 not supported in blueprint
 	bool SetNodeAttributeValueInt8(const FName NodeIdentity, const FName AttributeName, const int8 Value);
	//type int16 not supported in blueprint
 	bool SetNodeAttributeValueInt16(const FName NodeIdentity, const FName AttributeName, const int16 Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueInt32(const FName NodeIdentity, const FName AttributeName, const int32 Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueInt64(const FName NodeIdentity, const FName AttributeName, const int64 Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueUint8(const FName NodeIdentity, const FName AttributeName, const uint8 Value);
	//type uint16 not supported in blueprint
 	bool SetNodeAttributeValueUint16(const FName NodeIdentity, const FName AttributeName, const uint16 Value);
	//type uint32 not supported in blueprint
 	bool SetNodeAttributeValueUint32(const FName NodeIdentity, const FName AttributeName, const uint32 Value);
	//type uint64 not supported in blueprint
 	bool SetNodeAttributeValueUint64(const FName NodeIdentity, const FName AttributeName, const uint64 Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueVector(const FName NodeIdentity, const FName AttributeName, const FVector& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueVector2D(const FName NodeIdentity, const FName AttributeName, const FVector2D& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueMatrix(const FName NodeIdentity, const FName AttributeName, const FMatrix& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueBox(const FName NodeIdentity, const FName AttributeName, const FBox& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueBoxSphereBound(const FName NodeIdentity, const FName AttributeName, const FBoxSphereBounds& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueArray(const FName NodeIdentity, const FName AttributeName, const TArray<uint8>& Value);
	//type TArray64<uint8> not supported in blueprint
 	bool SetNodeAttributeValueArray64(const FName NodeIdentity, const FName AttributeName, const TArray64<uint8>& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueColor(const FName NodeIdentity, const FName AttributeName, const FColor& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueDateTime(const FName NodeIdentity, const FName AttributeName, const FDateTime& Value);
	//We use a uint8 to get the enum value, blueprint only support enum of uint8 type
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueEnum(const FName NodeIdentity, const FName AttributeName, const uint8 Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueGuid(const FName NodeIdentity, const FName AttributeName, const FGuid& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueIntPoint(const FName NodeIdentity, const FName AttributeName, const FIntPoint& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueIntVecor(const FName NodeIdentity, const FName AttributeName, const FIntVector& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueLinearColor(const FName NodeIdentity, const FName AttributeName, const FLinearColor& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValuePlane(const FName NodeIdentity, const FName AttributeName, const FPlane& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueQuat(const FName NodeIdentity, const FName AttributeName, const FQuat& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueName(const FName NodeIdentity, const FName AttributeName, const FName Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueRandomStream(const FName NodeIdentity, const FName AttributeName, const FRandomStream& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueRotator(const FName NodeIdentity, const FName AttributeName, const FRotator& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueString(const FName NodeIdentity, const FName AttributeName, const FString& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueTimespan(const FName NodeIdentity, const FName AttributeName, const FTimespan& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueTransform(const FName NodeIdentity, const FName AttributeName, const FTransform& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueTwoVectors(const FName NodeIdentity, const FName AttributeName, const FTwoVectors& Value);
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node Attribute")
	bool SetNodeAttributeValueVector4(const FName NodeIdentity, const FName AttributeName, const FVector4& Value);

public:
	Interchange::FBaseNodeContainer* BaseNodeContainer = nullptr;
};
