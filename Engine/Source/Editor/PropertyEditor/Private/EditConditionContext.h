// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IEditConditionContext
{
public:
	virtual TOptional<bool> GetBoolValue(const FString& PropertyName) const = 0;
	virtual TOptional<int64> GetIntegerValue(const FString& PropertyName) const = 0;
	virtual TOptional<double> GetNumericValue(const FString& PropertyName) const = 0;
	virtual TOptional<FString> GetEnumValue(const FString& PropertyName) const = 0;
	virtual TOptional<UObject*> GetPointerValue(const FString& PropertyName) const = 0;
	virtual TOptional<FString> GetTypeName(const FString& PropertyName) const = 0;
	virtual TOptional<int64> GetIntegerValueOfEnum(const FString& EnumType, const FString& EnumValue) const = 0;
};

class UProperty;
class FPropertyNode;
class FComplexPropertyNode;
class FEditConditionExpression;

class FEditConditionContext : public IEditConditionContext
{
public:
	FEditConditionContext(FPropertyNode& InPropertyNode);
	virtual ~FEditConditionContext() {}

	virtual TOptional<bool> GetBoolValue(const FString& PropertyName) const override;
	virtual TOptional<int64> GetIntegerValue(const FString& PropertyName) const override;
	virtual TOptional<double> GetNumericValue(const FString& PropertyName) const override;
	virtual TOptional<FString> GetEnumValue(const FString& PropertyName) const override;
	virtual TOptional<UObject*> GetPointerValue(const FString& PropertyName) const override;
	virtual TOptional<FString> GetTypeName(const FString& PropertyName) const override;
	virtual TOptional<int64> GetIntegerValueOfEnum(const FString& EnumType, const FString& EnumValue) const override;

	/**
	 * Fetch the single boolean property referenced.
	 * Returns nullptr if more than one property is referenced.
	 */
	const UBoolProperty* GetSingleBoolProperty(const TSharedPtr<FEditConditionExpression>& Expression) const;

private:
	TWeakPtr<FPropertyNode> PropertyNode;
};
