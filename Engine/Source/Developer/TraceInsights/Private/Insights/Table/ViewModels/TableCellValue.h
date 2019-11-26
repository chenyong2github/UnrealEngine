// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETableCellDataType : uint32
{
	Unknown,

	Bool,
	Int64,
	Float,
	Double,
	CString,
	Text,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTableCellValue
{
public:
	FTableCellValue() : DataType(ETableCellDataType::Unknown) {}

	FTableCellValue(bool Value) : DataType(ETableCellDataType::Bool), Bool(Value) {}
	FTableCellValue(int64 Value) : DataType(ETableCellDataType::Int64), Int64(Value) {}
	FTableCellValue(float Value) : DataType(ETableCellDataType::Float), Float(Value) {}
	FTableCellValue(double Value) : DataType(ETableCellDataType::Double), Double(Value) {}
	FTableCellValue(const TCHAR* Value) : DataType(ETableCellDataType::CString), CString(Value) {}
	FTableCellValue(const FText& Value) : DataType(ETableCellDataType::Text), TextPtr(MakeShared<FText>(Value)) {}

	ETableCellDataType DataType;

	union
	{
		bool Bool;
		int64 Int64;
		float Float;
		double Double;
		const TCHAR* CString;
	};
	TSharedPtr<FText> TextPtr;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
