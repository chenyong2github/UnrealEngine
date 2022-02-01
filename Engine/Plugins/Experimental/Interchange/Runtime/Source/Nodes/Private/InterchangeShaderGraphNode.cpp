// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeShaderGraphNode.h"

const TCHAR* UInterchangeShaderPortsAPI::InputPrefix = TEXT("Inputs");
const TCHAR* UInterchangeShaderPortsAPI::InputSeparator = TEXT(":");

FString UInterchangeShaderPortsAPI::MakeInputConnectionKey(const FString& InputName)
{
	TStringBuilder<128> StringBuilder;
	StringBuilder.Append(InputPrefix);
	StringBuilder.Append(InputSeparator);
	StringBuilder.Append(InputName);
	StringBuilder.Append(InputSeparator);
	StringBuilder.Append(TEXT("Connect"));

	return StringBuilder.ToString();
}

FString UInterchangeShaderPortsAPI::MakeInputValueKey(const FString& InputName)
{
	TStringBuilder<128> StringBuilder;
	StringBuilder.Append(InputPrefix);
	StringBuilder.Append(InputSeparator);
	StringBuilder.Append(InputName);
	StringBuilder.Append(InputSeparator);
	StringBuilder.Append(TEXT("Value"));

	return StringBuilder.ToString();
}

FString UInterchangeShaderPortsAPI::MakeInputName(const FString& InputKey)
{
	FString InputName;
	FString Discard;

	InputKey.Split(InputSeparator, &Discard, &InputName, ESearchCase::IgnoreCase, ESearchDir::FromStart);
	InputName.Split(InputSeparator, &InputName, &Discard, ESearchCase::IgnoreCase, ESearchDir::FromStart);

	return InputName;
}

bool UInterchangeShaderPortsAPI::IsAnInput(const FString& AttributeKey)
{
	TStringBuilder<128> StringBuilder;
	StringBuilder.Append(InputPrefix);
	StringBuilder.Append(InputSeparator);

	return AttributeKey.StartsWith(StringBuilder.ToString());
}

bool UInterchangeShaderPortsAPI::HasInput(const UInterchangeBaseNode* InterchangeNode, const FName& InInputName)
{
	TArray<FString> InputNames;
	GatherInputs(InterchangeNode, InputNames);

	for (const FString& InputName : InputNames)
	{
		if (InputName.Equals(InInputName.ToString(), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

void UInterchangeShaderPortsAPI::GatherInputs(const UInterchangeBaseNode* InterchangeNode, TArray<FString>& OutInputNames)
{
	TArray< UE::Interchange::FAttributeKey > AttributeKeys;
	InterchangeNode->GetAttributeKeys(AttributeKeys);

	for (const UE::Interchange::FAttributeKey& AttributeKey : AttributeKeys)
	{
		if (IsAnInput(AttributeKey.ToString()))
		{
			OutInputNames.Add(MakeInputName(AttributeKey.ToString()));
		}
	}
}

bool UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(UInterchangeBaseNode* InterchangeNode, const FString& InputName, const FString& ExpressionUid)
{
	return InterchangeNode->AddStringAttribute(MakeInputConnectionKey(InputName), ExpressionUid);
}

bool UInterchangeShaderPortsAPI::ConnectOuputToInput(UInterchangeBaseNode* InterchangeNode, const FString& InputName, const FString& ExpressionUid, const FString& OutputName)
{
	if (OutputName.IsEmpty())
	{
		return ConnectDefaultOuputToInput(InterchangeNode, InputName, ExpressionUid);
	}
	else
	{
		return InterchangeNode->AddStringAttribute(MakeInputConnectionKey(InputName), ExpressionUid + InputSeparator + OutputName);
	}
}

UE::Interchange::EAttributeTypes UInterchangeShaderPortsAPI::GetInputType(const UInterchangeBaseNode* InterchangeNode, const FString& InputName)
{
	return InterchangeNode->GetAttributeType(UE::Interchange::FAttributeKey(MakeInputValueKey(InputName)));
}

bool UInterchangeShaderPortsAPI::GetInputConnection(const UInterchangeBaseNode* InterchangeNode, const FString& InputName, FString& OutExpressionUid, FString& OutputName)
{
	if (InterchangeNode->GetStringAttribute(MakeInputConnectionKey(InputName), OutExpressionUid))
	{
		OutExpressionUid.Split(InputSeparator, &OutExpressionUid, &OutputName);
		return true;
	}

	return false;
}

FString UInterchangeShaderNode::GetTypeName() const
{
	const FString TypeName = TEXT("ShaderNode");
	return TypeName;
}

bool UInterchangeShaderNode::GetCustomShaderType(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ShaderType, FString);
}

bool UInterchangeShaderNode::SetCustomShaderType(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ShaderType, FString);
}

FString UInterchangeShaderGraphNode::GetTypeName() const
{
	const FString TypeName = TEXT("ShaderGraphNode");
	return TypeName;
}

bool UInterchangeShaderGraphNode::GetCustomTwoSided(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TwoSided, bool);
}

bool UInterchangeShaderGraphNode::SetCustomTwoSided(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TwoSided, bool);
}
