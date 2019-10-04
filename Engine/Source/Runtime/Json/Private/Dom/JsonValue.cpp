// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"




double FJsonValue::AsNumber() const
{
	double Number = 0.0;

	if (!TryGetNumber(Number))
	{
		ErrorMessage(TEXT("Number"));
	}

	return Number;
}


FString FJsonValue::AsString() const 
{
	FString String;

	if (!TryGetString(String))
	{
		ErrorMessage(TEXT("String"));
	}

	return String;
}


bool FJsonValue::AsBool() const 
{
	bool Bool = false;

	if (!TryGetBool(Bool))
	{
		ErrorMessage(TEXT("Boolean")); 
	}

	return Bool;
}


const TArray< TSharedPtr<FJsonValue> >& FJsonValue::AsArray() const
{
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;

	if (!TryGetArray(Array))
	{
		static const TArray< TSharedPtr<FJsonValue> > EmptyArray;
		Array = &EmptyArray;
		ErrorMessage(TEXT("Array"));
	}

	return *Array;
}


const TSharedPtr<FJsonObject>& FJsonValue::AsObject() const
{
	const TSharedPtr<FJsonObject>* Object = nullptr;

	if (!TryGetObject(Object))
	{
		static const TSharedPtr<FJsonObject> EmptyObject = MakeShared<FJsonObject>();
		Object = &EmptyObject;
		ErrorMessage(TEXT("Object"));
	}

	return *Object;
}

template <typename T>
bool TryConvertNumber(const FJsonValue& InValue, T& OutNumber)
{
	double Double;

	if (InValue.TryGetNumber(Double) && (Double >= TNumericLimits<T>::Min()) && (Double <= TNumericLimits<T>::Max()))
	{
		OutNumber = static_cast<T>(FMath::RoundHalfFromZero(Double));

		return true;
	}

	return false;
}

bool FJsonValue::TryGetNumber(float& OutNumber) const
{
	double Double;

	if (TryGetNumber(Double))
	{
		OutNumber = static_cast<float>(Double);
		return true;
	}

	return false;
}

bool FJsonValue::TryGetNumber(uint8& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(uint16& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(uint32& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(uint64& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(int8& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(int16& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(int32& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

bool FJsonValue::TryGetNumber(int64& OutNumber) const
{
	return TryConvertNumber(*this, OutNumber);
}

//static 
bool FJsonValue::CompareEqual( const FJsonValue& Lhs, const FJsonValue& Rhs )
{
	if (Lhs.Type != Rhs.Type)
	{
		return false;
	}

	switch (Lhs.Type)
	{
	case EJson::None:
	case EJson::Null:
		return true;

	case EJson::String:
		return Lhs.AsString() == Rhs.AsString();

	case EJson::Number:
		return Lhs.AsNumber() == Rhs.AsNumber();

	case EJson::Boolean:
		return Lhs.AsBool() == Rhs.AsBool();

	case EJson::Array:
		{
			const TArray< TSharedPtr<FJsonValue> >& LhsArray = Lhs.AsArray();
			const TArray< TSharedPtr<FJsonValue> >& RhsArray = Rhs.AsArray();

			if (LhsArray.Num() != RhsArray.Num())
			{
				return false;
			}

			// compare each element
			for (int32 i = 0; i < LhsArray.Num(); ++i)
			{
				if (!CompareEqual(*LhsArray[i], *RhsArray[i]))
				{
					return false;
				}
			}
		}
		return true;

	case EJson::Object:
		{
			const TSharedPtr<FJsonObject>& LhsObject = Lhs.AsObject();
			const TSharedPtr<FJsonObject>& RhsObject = Rhs.AsObject();

			if (LhsObject.IsValid() != RhsObject.IsValid())
			{
				return false;
			}

			if (LhsObject.IsValid())
			{
				if (LhsObject->Values.Num() != RhsObject->Values.Num())
				{
					return false;
				}

				// compare each element
				for (const auto& It : LhsObject->Values)
				{
					const FString& Key = It.Key;
					const TSharedPtr<FJsonValue>* RhsValue = RhsObject->Values.Find(Key);
					if (RhsValue == NULL)
					{
						// not found in both objects
						return false;
					}

					const TSharedPtr<FJsonValue>& LhsValue = It.Value;

					if (LhsValue.IsValid() != RhsValue->IsValid())
					{
						return false;
					}

					if (LhsValue.IsValid())
					{
						if (!CompareEqual(*LhsValue.Get(), *RhsValue->Get()))
						{
							return false;
						}
					}
				}
			}
		}
		return true;

	default:
		return false;
	}
}

void FJsonValue::ErrorMessage(const FString& InType) const
{
	UE_LOG(LogJson, Error, TEXT("Json Value of type '%s' used as a '%s'."), *GetType(), *InType);
}
