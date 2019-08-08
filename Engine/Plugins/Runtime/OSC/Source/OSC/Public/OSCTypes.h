// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

enum class EOSCTypeTag {

	INT32 = 'i',
	FLOAT = 'f',
	DOUBLE = 'd',
	STRING = 's',
	BLOB = 'b',
	TIME = 't',
	INT64 = 'h',
	CHAR = 'c',
	TRUE = 'T',
	FALSE = 'F',
	NIL = 'N',
	INFINITUM = 'I',
	COLOR = 'r',
	TERMINATE = '\0'
};

struct FOSCColor
{
	explicit FOSCColor() 
	{}

	explicit FOSCColor(uint8 R, uint8 G, uint8 B, uint8 A) 
		: Red(R), Green(G), Blue(B), Alpha(A)
	{}

	explicit FOSCColor(int32 c)
	{
		Red = (uint8)(c >> 24);
		Green = (uint8)(c >> 16);
		Blue = (uint8)(c >> 8),
		Alpha = (uint8)c;
	}

	uint8 Red, Green, Blue, Alpha;

	int32 GetInt32() const
	{
		return	(static_cast<int32>(Red)) | (static_cast<int32>(Green) << 8) | 
				(static_cast<int32>(Blue) << 16) | (static_cast<int32>(Alpha) << 24);
	}
};

class FOSCType
{
public:
	explicit FOSCType(int32 Value)
		: TypeTag(EOSCTypeTag::INT32)
		, Data(Value)
		, Blob()
	{
	}
	explicit FOSCType(int64 Value)
		: TypeTag(EOSCTypeTag::INT64)
		, Data(Value)
		, Blob()
	{
	}
	explicit FOSCType(ANSICHAR Value)
		: TypeTag(EOSCTypeTag::CHAR)
		, Data(Value)
		, Blob()
	{
	}
	explicit FOSCType(uint64 Value)
		: TypeTag(EOSCTypeTag::TIME)
		, Data(Value)
		, Blob()
	{
	}
	explicit FOSCType(float Value)
		: TypeTag(EOSCTypeTag::FLOAT)
		, Data(Value)
		, Blob()
	
	{
	}
	explicit FOSCType(double Value)
		: TypeTag(EOSCTypeTag::DOUBLE)
		, Data(Value)
		, Blob()
	{
	}
	explicit FOSCType(bool Value)
		: TypeTag(Value ? EOSCTypeTag::TRUE : EOSCTypeTag::FALSE)
		, Data(Value)
		, Blob()
	{
	}
	explicit FOSCType(FString Value)
		: TypeTag(EOSCTypeTag::STRING)
		, Data(0)
		, String(Value)
		, Blob()
	{
	}
	explicit FOSCType(TArray<uint8> Value)
		: TypeTag(EOSCTypeTag::BLOB)
		, Data(0)
		, Blob(Value)
	{
	}
	explicit FOSCType(FOSCColor color)
		: TypeTag(EOSCTypeTag::COLOR)
		, Data(0)
		, Blob()
		, Color(color)
	{
	}

	explicit FOSCType(EOSCTypeTag typeTag)
		: TypeTag(typeTag)
		, Data(0)
		, Blob()
	{
	}

	EOSCTypeTag GetTypeTag() const { return TypeTag; }

	bool IsInt32() const { return TypeTag == EOSCTypeTag::INT32; }
	int32 GetInt32() const { return Data.Int32; }

	bool IsInt64() const { return TypeTag == EOSCTypeTag::INT64; }
	int64 GetInt64() const { return Data.Int64; }

	bool IsTimeTag() const { return TypeTag == EOSCTypeTag::TIME; }
	uint64 GetTimeTag() const { return Data.Time; }

	bool IsBool() const { return TypeTag == EOSCTypeTag::TRUE || TypeTag == EOSCTypeTag::FALSE; }
	bool GetBool() const { return Data.Bool; }

	bool IsChar() const { return TypeTag == EOSCTypeTag::CHAR; }
	ANSICHAR GetChar() const { return Data.Char; }

	bool IsFloat() const { return TypeTag == EOSCTypeTag::FLOAT; }
	float GetFloat() const { return Data.Float; }
	
	bool IsDouble() const { return TypeTag == EOSCTypeTag::DOUBLE; }
	double GetDouble() const { return Data.Double; }

	bool IsString() const { return TypeTag == EOSCTypeTag::STRING; }
	FString GetString() const { return String; }

	bool IsBlob() const { return TypeTag == EOSCTypeTag::BLOB; }
	TArray<uint8> GetBlob() const { return Blob; }

	bool IsColor() const { return TypeTag == EOSCTypeTag::COLOR; }
	FOSCColor GetColor()  const { return Color; }

	bool IsNil() const { return TypeTag == EOSCTypeTag::NIL; }
	bool IsInfinitum() const { return TypeTag == EOSCTypeTag::INFINITUM; }

private:
	union DataTypes
	{
		explicit DataTypes(int32 Value)
			: Int32(Value)
		{
		}
		explicit DataTypes(int64 Value)
			: Int64(Value)
		{
		}
		explicit DataTypes(uint64 Value)
			: Time(Value)
		{
		}
		explicit DataTypes(ANSICHAR Value)
			: Char(Value)
		{
		}
		explicit DataTypes(float Value)
			: Float(Value)
		{
		}
		explicit DataTypes(double Value)
			: Double(Value)
		{
		}

		int32 Int32;
		int64 Int64;
		uint64 Time;
		ANSICHAR Char;
		float Float;
		double Double;
		bool Bool;
	};

	
private:
	EOSCTypeTag TypeTag;
	DataTypes Data;
	FString String;
	TArray<uint8> Blob;
	FOSCColor Color;
};
