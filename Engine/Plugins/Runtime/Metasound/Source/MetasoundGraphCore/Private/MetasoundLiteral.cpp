// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundLiteral.h"

#include "IAudioProxyInitializer.h"
#include "Misc/TVariant.h"
#include <type_traits>

namespace Metasound
{
#if METASOUND_DEBUG_LITERALS
	void FLiteral::InitDebugString()
	{
		switch (GetType())
		{
			case ELiteralType::None:
				DebugString = TEXT("NONE");
			break;

			case ELiteralType::Boolean:
				DebugString = FString::Printf(TEXT("Boolean: %s"), Value.Get<bool>() ? TEXT("true") : TEXT("false"));
			break;

			case ELiteralType::Integer:
				DebugString = FString::Printf(TEXT("Int32: %d"), Value.Get<int32>());
			break;

			case ELiteralType::Float:
				DebugString = FString::Printf(TEXT("Float: %f"), Value.Get<float>());
			break;

			case ELiteralType::String:
				DebugString = FString::Printf(TEXT("String: %s"), *Value.Get<FString>());
			break;

			case ELiteralType::UObjectProxy:
			{
				FString ProxyType = TEXT("nullptr");
				if (Value.Get<Audio::IProxyDataPtr>().IsValid())
				{
					ProxyType = Value.Get<Audio::IProxyDataPtr>()->GetProxyTypeName().ToString();
				}
				DebugString = FString::Printf(TEXT("Audio::IProxyDataPtr: %s"), *ProxyType);
			}
			break;

			case ELiteralType::NoneArray:
				DebugString = TEXT("TArray<NONE>");
			break;

			case ELiteralType::BooleanArray:
				DebugString = FString::Printf(TEXT("TArray<Boolean>"));
			break;

			case ELiteralType::IntegerArray:
				DebugString = FString::Printf(TEXT("TArray<int32>"));
			break;

			case ELiteralType::FloatArray:
				DebugString = FString::Printf(TEXT("TArray<float>"));
			break;

			case ELiteralType::StringArray:
				DebugString = FString::Printf(TEXT("TArray<FString>"));
			break;

			case ELiteralType::UObjectProxyArray:
				DebugString = FString::Printf(TEXT("TArray<Audio::IProxyDataPtr>"));
			break;

			case ELiteralType::Invalid:
				DebugString = TEXT("INVALID");
			break;

			default:
				static_assert(static_cast<int32>(ELiteralType::Invalid) == 12, "Possible missing ELiteralType case coverage");
				checkNoEntry();
		}
	}
#endif // METASOUND_DEBUG_LITERALS


	// builds an invalid FLiteral.
	FLiteral FLiteral::CreateInvalid()
	{
		return FLiteral(FInvalid());
	}

	FLiteral FLiteral::GetDefaultForType(ELiteralType InType)
	{
		switch (InType)
		{
			case ELiteralType::None:
				return FLiteral(TLiteralTypeInfo<FLiteral::FNone>::GetDefaultValue());

			case ELiteralType::Boolean:
				return FLiteral(TLiteralTypeInfo<bool>::GetDefaultValue());

			case ELiteralType::Integer:
				return FLiteral(TLiteralTypeInfo<int32>::GetDefaultValue());

			case ELiteralType::Float:
				return FLiteral(TLiteralTypeInfo<float>::GetDefaultValue());

			case ELiteralType::String:
				return FLiteral(TLiteralTypeInfo<FString>::GetDefaultValue());

			case ELiteralType::UObjectProxy:
				return FLiteral(TLiteralTypeInfo<Audio::IProxyDataPtr>::GetDefaultValue());
			
			case ELiteralType::NoneArray:
				return FLiteral(TLiteralTypeInfo<TArray<FLiteral::FNone>>::GetDefaultValue());
			
			case ELiteralType::BooleanArray:
				return FLiteral(TLiteralTypeInfo<TArray<bool>>::GetDefaultValue());
			
			case ELiteralType::IntegerArray:
				return FLiteral(TLiteralTypeInfo<TArray<int32>>::GetDefaultValue());
			
			case ELiteralType::FloatArray:
				return FLiteral(TLiteralTypeInfo<TArray<float>>::GetDefaultValue());

			case ELiteralType::StringArray:
				return FLiteral(TLiteralTypeInfo<TArray<FString>>::GetDefaultValue());

			case ELiteralType::UObjectProxyArray:
				return FLiteral(TLiteralTypeInfo<TArray<Audio::IProxyDataPtr>>::GetDefaultValue());

			case ELiteralType::Invalid:
			default:
				static_assert(static_cast<int32>(ELiteralType::Invalid) == 12, "Possible missing ELiteralType case coverage");
				return FLiteral::CreateInvalid();

		}
	}

	bool FLiteral::IsValid() const
	{
		return GetType() != ELiteralType::Invalid;
	}

	ELiteralType FLiteral::GetType() const
	{
		switch (Value.GetIndex())
		{
			case 0:
				return ELiteralType::None;

			case 1:
				return ELiteralType::Boolean;

			case 2:
				return ELiteralType::Integer;

			case 3:
				return ELiteralType::Float;

			case 4:
				return ELiteralType::String;

			case 5:
				return ELiteralType::UObjectProxy;

			case 6:
				return ELiteralType::NoneArray;

			case 7:
				return ELiteralType::BooleanArray;

			case 8:
				return ELiteralType::IntegerArray;

			case 9:
				return ELiteralType::FloatArray;

			case 10:
				return ELiteralType::StringArray;

			case 11:
				return ELiteralType::UObjectProxyArray;

			case 12:
			default:
				static_assert(TVariantSize<FVariantType>::Value == 13, "Possible missing FVariantType case coverage");
				return ELiteralType::Invalid;
		}
	}

	FLiteral FLiteral::Clone() const
	{
		// TODO: If Clone() is supported after reworking vertices,
		// wrap the Audio::IProxyData in a object that manages virtual
		// copy construction.  Then replace this switch with simple
		// FLiteral copy constructor.
		switch (GetType())
		{
		case ELiteralType::Invalid:
			return CreateInvalid();

		case ELiteralType::None:
			return FLiteral(FNone());

		case ELiteralType::Boolean:
			return FLiteral(Value.Get<bool>());

		case ELiteralType::Float:
			return FLiteral(Value.Get<float>());

		case ELiteralType::Integer:
			return FLiteral(Value.Get<int32>());

		case ELiteralType::String:
			return FLiteral(Value.Get<FString>());

		case ELiteralType::UObjectProxy:
			if (const Audio::IProxyDataPtr& ProxyPtr = Value.Get<Audio::IProxyDataPtr>())
			{
				return FLiteral(ProxyPtr->Clone());
			}
			return CreateInvalid();

		case ELiteralType::NoneArray:
			return FLiteral(Value.Get<TArray<FNone>>());

		case ELiteralType::BooleanArray:
			return FLiteral(Value.Get<TArray<bool>>());

		case ELiteralType::FloatArray:
			return FLiteral(Value.Get<TArray<float>>());

		case ELiteralType::IntegerArray:
			return FLiteral(Value.Get<TArray<int32>>());

		case ELiteralType::StringArray:
			return FLiteral(Value.Get<TArray<FString>>());

		case ELiteralType::UObjectProxyArray:
			{
				TArray<Audio::IProxyDataPtr> ProxyPtrArrayCopy;
				for (const Audio::IProxyDataPtr& Ptr : Value.Get<TArray<Audio::IProxyDataPtr>>())
				{
					if (Ptr.IsValid())
					{
						ProxyPtrArrayCopy.Add(Ptr->Clone());
					}
				}
				return FLiteral(MoveTemp(ProxyPtrArrayCopy));
			}
			return CreateInvalid();

		default:
			checkNoEntry();
			return CreateInvalid();
		}
	}

}


