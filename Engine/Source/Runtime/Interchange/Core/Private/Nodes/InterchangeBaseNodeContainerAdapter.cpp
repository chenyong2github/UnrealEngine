// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/InterchangeBaseNodeContainerAdapter.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Nodes/BaseNode.h"
#include "Nodes/BaseNodeContainer.h"

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueBool(const FName NodeIdentity, const FName AttributeName, bool& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueFloat(const FName NodeIdentity, const FName AttributeName, float& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueDouble(const FName NodeIdentity, const FName AttributeName, double& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueInt8(const FName NodeIdentity, const FName AttributeName, int8& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueInt16(const FName NodeIdentity, const FName AttributeName, int16& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueInt32(const FName NodeIdentity, const FName AttributeName, int32& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueInt64(const FName NodeIdentity, const FName AttributeName, int64& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueUint8(const FName NodeIdentity, const FName AttributeName, uint8& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueUint16(const FName NodeIdentity, const FName AttributeName, uint16& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueUint32(const FName NodeIdentity, const FName AttributeName, uint32& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueUint64(const FName NodeIdentity, const FName AttributeName, uint64& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueVector(const FName NodeIdentity, const FName AttributeName, FVector& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueVector2D(const FName NodeIdentity, const FName AttributeName, FVector2D& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueMatrix(const FName NodeIdentity, const FName AttributeName, FMatrix& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueBox(const FName NodeIdentity, const FName AttributeName, FBox& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueBoxSphereBound(const FName NodeIdentity, const FName AttributeName, FBoxSphereBounds& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueArray(const FName NodeIdentity, const FName AttributeName, TArray<uint8>& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueArray64(const FName NodeIdentity, const FName AttributeName, TArray64<uint8>& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueColor(const FName NodeIdentity, const FName AttributeName, FColor& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueDateTime(const FName NodeIdentity, const FName AttributeName, FDateTime& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueEnum(const FName NodeIdentity, const FName AttributeName, uint8& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueGuid(const FName NodeIdentity, const FName AttributeName, FGuid& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueIntPoint(const FName NodeIdentity, const FName AttributeName, FIntPoint& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueIntVecor(const FName NodeIdentity, const FName AttributeName, FIntVector& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueLinearColor(const FName NodeIdentity, const FName AttributeName, FLinearColor& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValuePlane(const FName NodeIdentity, const FName AttributeName, FPlane& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueQuat(const FName NodeIdentity, const FName AttributeName, FQuat& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueName(const FName NodeIdentity, const FName AttributeName, FName& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueRandomStream(const FName NodeIdentity, const FName AttributeName, FRandomStream& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueRotator(const FName NodeIdentity, const FName AttributeName, FRotator& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueString(const FName NodeIdentity, const FName AttributeName, FString& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueTimespan(const FName NodeIdentity, const FName AttributeName, FTimespan& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueTransform(const FName NodeIdentity, const FName AttributeName, FTransform& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueTwoVectors(const FName NodeIdentity, const FName AttributeName, FTwoVectors& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::GetNodeAttributeValueVector4(const FName NodeIdentity, const FName AttributeName, FVector4& OutValue)
{
	return GetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}









bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueBool(const FName NodeIdentity, const FName AttributeName, const bool OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueFloat(const FName NodeIdentity, const FName AttributeName, const float OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueDouble(const FName NodeIdentity, const FName AttributeName, const double OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueInt8(const FName NodeIdentity, const FName AttributeName, const int8 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueInt16(const FName NodeIdentity, const FName AttributeName, const int16 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueInt32(const FName NodeIdentity, const FName AttributeName, const int32 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueInt64(const FName NodeIdentity, const FName AttributeName, const int64 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueUint8(const FName NodeIdentity, const FName AttributeName, const uint8 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueUint16(const FName NodeIdentity, const FName AttributeName, const uint16 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueUint32(const FName NodeIdentity, const FName AttributeName, const uint32 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueUint64(const FName NodeIdentity, const FName AttributeName, const uint64 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueVector(const FName NodeIdentity, const FName AttributeName, const FVector& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueVector2D(const FName NodeIdentity, const FName AttributeName, const FVector2D& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueMatrix(const FName NodeIdentity, const FName AttributeName, const FMatrix& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueBox(const FName NodeIdentity, const FName AttributeName, const FBox& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueBoxSphereBound(const FName NodeIdentity, const FName AttributeName, const FBoxSphereBounds& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueArray(const FName NodeIdentity, const FName AttributeName, const TArray<uint8>& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueArray64(const FName NodeIdentity, const FName AttributeName, const TArray64<uint8>& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueColor(const FName NodeIdentity, const FName AttributeName, const FColor& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueDateTime(const FName NodeIdentity, const FName AttributeName, const FDateTime& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueEnum(const FName NodeIdentity, const FName AttributeName, const uint8 OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueGuid(const FName NodeIdentity, const FName AttributeName, const FGuid& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueIntPoint(const FName NodeIdentity, const FName AttributeName, const FIntPoint& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueIntVecor(const FName NodeIdentity, const FName AttributeName, const FIntVector& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueLinearColor(const FName NodeIdentity, const FName AttributeName, const FLinearColor& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValuePlane(const FName NodeIdentity, const FName AttributeName, const FPlane& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueQuat(const FName NodeIdentity, const FName AttributeName, const FQuat& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueName(const FName NodeIdentity, const FName AttributeName, const FName OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueRandomStream(const FName NodeIdentity, const FName AttributeName, const FRandomStream& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueRotator(const FName NodeIdentity, const FName AttributeName, const FRotator& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueString(const FName NodeIdentity, const FName AttributeName, const FString& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueTimespan(const FName NodeIdentity, const FName AttributeName, const FTimespan& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueTransform(const FName NodeIdentity, const FName AttributeName, const FTransform& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueTwoVectors(const FName NodeIdentity, const FName AttributeName, const FTwoVectors& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}

bool UInterchangeBaseNodeContainerAdapter::SetNodeAttributeValueVector4(const FName NodeIdentity, const FName AttributeName, const FVector4& OutValue)
{
	return SetNodeAttributeValue(NodeIdentity, AttributeName, OutValue);
}