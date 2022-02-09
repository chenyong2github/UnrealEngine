// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/InterchangeUserDefinedAttribute.h"

#include "Nodes/InterchangeBaseNode.h"

const FString UInterchangeUserDefinedAttributesAPI::UserDefinedAttributeBaseKey = TEXT("UserDefined_");
const FString UInterchangeUserDefinedAttributesAPI::UserDefinedAttributeValuePostKey = TEXT("_Value");
const FString UInterchangeUserDefinedAttributesAPI::UserDefinedAttributePayLoadPostKey = TEXT("_Payload");

bool UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute_Boolean(UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, const bool& Value, const FString& PayloadKey)
{
	TOptional<FString> OptionalPayload;
	if (!PayloadKey.IsEmpty())
	{
		OptionalPayload = PayloadKey;
	}
	return UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, Value, OptionalPayload);
}

bool UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute_Float(UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, const float& Value, const FString& PayloadKey)
{
	TOptional<FString> OptionalPayload;
	if (!PayloadKey.IsEmpty())
	{
		OptionalPayload = PayloadKey;
	}
	return UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, Value, OptionalPayload);
}

bool UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute_Double(UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, const double& Value, const FString& PayloadKey)
{
	TOptional<FString> OptionalPayload;
	if (!PayloadKey.IsEmpty())
	{
		OptionalPayload = PayloadKey;
	}
	return UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, Value, OptionalPayload);
}

bool UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute_Int32(UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, const int32& Value, const FString& PayloadKey)
{
	TOptional<FString> OptionalPayload;
	if (!PayloadKey.IsEmpty())
	{
		OptionalPayload = PayloadKey;
	}
	return UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, Value, OptionalPayload);
}

bool UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute_FString(UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, const FString& Value, const FString& PayloadKey)
{
	TOptional<FString> OptionalPayload;
	if (!PayloadKey.IsEmpty())
	{
		OptionalPayload = PayloadKey;
	}
	return UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, Value, OptionalPayload);
}

bool UInterchangeUserDefinedAttributesAPI::RemoveUserDefinedAttribute(UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName)
{

	FString StorageBaseKey = UserDefinedAttributeBaseKey + UserDefinedAttributeName;
	UE::Interchange::FAttributeKey UserDefinedValueKey = UE::Interchange::FAttributeKey(StorageBaseKey + UserDefinedAttributeValuePostKey);
	if (InterchangeNode->HasAttribute(UserDefinedValueKey))
	{
		if (!InterchangeNode->RemoveAttribute(UserDefinedValueKey.Key))
		{
			return false;
		}
	}

	UE::Interchange::FAttributeKey UserDefinedPayloadKey = UE::Interchange::FAttributeKey(StorageBaseKey + UserDefinedAttributePayLoadPostKey);
	if (InterchangeNode->HasAttribute(UserDefinedPayloadKey))
	{
		if (!InterchangeNode->RemoveAttribute(UserDefinedPayloadKey.Key))
		{
			return false;
		}
	}

	//Attribute was successfuly removed
	return true;
}

bool UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute_Boolean(const UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, bool& OutValue, FString& OutPayloadKey)
{
	TOptional<FString> OptionalPayload;
	bool bResult = UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, OutValue, OptionalPayload);
	if (OptionalPayload.IsSet())
	{
		OutPayloadKey = OptionalPayload.GetValue();
	}
	return bResult;
}

bool UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute_Float(const UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, float& OutValue, FString& OutPayloadKey)
{
	TOptional<FString> OptionalPayload;
	bool bResult = UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, OutValue, OptionalPayload);
	if (OptionalPayload.IsSet())
	{
		OutPayloadKey = OptionalPayload.GetValue();
	}
	return bResult;
}

bool UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute_Double(const UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, double& OutValue, FString& OutPayloadKey)
{
	TOptional<FString> OptionalPayload;
	bool bResult = UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, OutValue, OptionalPayload);
	if (OptionalPayload.IsSet())
	{
		OutPayloadKey = OptionalPayload.GetValue();
	}
	return bResult;
}

bool UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute_Int32(const UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, int32& OutValue, FString& OutPayloadKey)
{
	TOptional<FString> OptionalPayload;
	bool bResult = UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, OutValue, OptionalPayload);
	if (OptionalPayload.IsSet())
	{
		OutPayloadKey = OptionalPayload.GetValue();
	}
	return bResult;
}

bool UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute_FString(const UInterchangeBaseNode* InterchangeNode, const FString& UserDefinedAttributeName, FString& OutValue, FString& OutPayloadKey)
{
	TOptional<FString> OptionalPayload;
	bool bResult = UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttribute(InterchangeNode, UserDefinedAttributeName, OutValue, OptionalPayload);
	if (OptionalPayload.IsSet())
	{
		OutPayloadKey = OptionalPayload.GetValue();
	}
	return bResult;
}

TArray<FString> UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttributeNames(const UInterchangeBaseNode* InterchangeNode)
{
	check(InterchangeNode);
	TArray<UE::Interchange::FAttributeKey> AttributeKeys;
	InterchangeNode->GetAttributeKeys(AttributeKeys);
	TArray<FString> UserDefinedAttributeNames;
	int32 RightChopIndex = UserDefinedAttributeBaseKey.Len();
	int32 LeftChopIndex = UserDefinedAttributeValuePostKey.Len();
	for (UE::Interchange::FAttributeKey& AttributeKey : AttributeKeys)
	{
		if (AttributeKey.Key.StartsWith(UserDefinedAttributeBaseKey) && AttributeKey.Key.EndsWith(UserDefinedAttributeValuePostKey))
		{
			FString UserDefineAttributeName = AttributeKey.Key.RightChop(RightChopIndex).LeftChop(LeftChopIndex);
			UserDefinedAttributeNames.Add(UserDefineAttributeName);
		}
	}
	return UserDefinedAttributeNames;
}

void UInterchangeUserDefinedAttributesAPI::GetUserDefinedAttributeNames(const UInterchangeBaseNode* InterchangeNode, TArray<FString>& UserDefinedAttributeNames)
{
	check(InterchangeNode);
	UserDefinedAttributeNames = GetUserDefinedAttributeNames(InterchangeNode);
}
