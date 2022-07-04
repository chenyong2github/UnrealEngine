// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/MemberReference.h"
#include "Misc/TVariant.h"
#include <type_traits>

class FProperty;
class UFunction;

namespace UE::MVVM
{

	/**
	 * Represents a possibly-const binding to either a UFunction or FProperty
	 */
	template<bool bConst>
	struct TMVVMFieldVariant
	{
	private:
		friend TMVVMFieldVariant<!bConst>;
		using FunctionType = std::conditional_t<bConst, const UFunction, UFunction>;
		using PropertyType = std::conditional_t<bConst, const FProperty, FProperty>;
		using VariantType = TVariant<FEmptyVariantState, PropertyType*, FunctionType*>;

	public:
		TMVVMFieldVariant() = default;

		explicit TMVVMFieldVariant(FFieldVariant InVariant)
		{
			if (PropertyType* Property = InVariant.Get<PropertyType>())
			{
				Binding = VariantType(TInPlaceType<PropertyType*>(), Property);
			}
			else if (FunctionType* Function = InVariant.Get<FunctionType>())
			{
				Binding = VariantType(TInPlaceType<FunctionType*>(), Function);
			}
		}

		explicit TMVVMFieldVariant(PropertyType* InValue)
		{
			Binding = VariantType(TInPlaceType<PropertyType*>(), InValue);
		}

		explicit TMVVMFieldVariant(FunctionType* InValue)
		{
			Binding = VariantType(TInPlaceType<FunctionType*>(), InValue);
		}

		UE_NODISCARD bool IsProperty() const
		{
			return Binding.template IsType<PropertyType*>();
		}

		UE_NODISCARD PropertyType* GetProperty() const
		{
			return Binding.template Get<PropertyType*>();
		}

		void SetProperty(PropertyType* InValue)
		{
			Binding.template Set<PropertyType*>(InValue);
		}

		UE_NODISCARD bool IsFunction() const
		{
			return Binding.template IsType<FunctionType*>();
		}

		UE_NODISCARD FunctionType* GetFunction() const
		{
			return Binding.template Get<FunctionType*>();
		}

		void SetFunction(FunctionType* InValue)
		{
			Binding.template Set<FunctionType*>(InValue);
		}

		UE_NODISCARD FName GetName() const
		{
			if (IsProperty())
			{
				PropertyType* Property = GetProperty();
				return Property ? Property->GetFName() : FName();
			}
			else if (IsFunction())
			{
				FunctionType* Function = GetFunction();
				return Function ? Function->GetFName() : FName();
			}
			return FName();
		}

		UE_NODISCARD UStruct* GetOwner() const
		{
			if (IsProperty())
			{
				PropertyType* Property = GetProperty();
				return Property ? Property->GetOwnerStruct() : nullptr;
			}
			else if (IsFunction())
			{
				FunctionType* Function = GetFunction();
				return Function ? Function->GetOwnerClass() : nullptr;
			}
			return nullptr;
		}

		UE_NODISCARD bool IsEmpty() const
		{
			return Binding.template IsType<FEmptyVariantState>();
		}

		void Reset()
		{
			Binding = VariantType();
		}

		template<bool bOtherConst>
		bool operator==(const TMVVMFieldVariant<bOtherConst>& B) const
		{
			if (Binding.GetIndex() != B.Binding.GetIndex())
			{
				return false;
			}
			if (IsEmpty())
			{
				return true;
			}
			if (IsProperty())
			{
				return GetProperty() == B.GetProperty();
			}
			return GetFunction() == B.GetFunction();
		}

		template<bool bOtherConst>
		bool operator!=(const TMVVMFieldVariant<bOtherConst>& B) const
		{
			return !(*this == B);
		}

		friend int32 GetTypeHash(const TMVVMFieldVariant<bConst>& Variant)
		{
			if (Variant.IsProperty())
			{
				return GetTypeHash(Variant.GetProperty());
			}
			if (Variant.IsFunction())
			{
				return GetTypeHash(Variant.GetFunction());
			}
			return 0;
		}

	private:
		VariantType Binding;
	};

	/** */
	struct FMVVMFieldVariant : public TMVVMFieldVariant<false>
	{
	public:
		using TMVVMFieldVariant<false>::TMVVMFieldVariant;
		using TMVVMFieldVariant<false>::operator==;
	};

	/** */
	struct FMVVMConstFieldVariant : public TMVVMFieldVariant<true>
	{
	public:
		using TMVVMFieldVariant<true>::TMVVMFieldVariant;
		using TMVVMFieldVariant<true>::operator==;

		FMVVMConstFieldVariant(const FMVVMFieldVariant& OtherVariant)
		{
			if (OtherVariant.IsProperty())
			{
				SetProperty(OtherVariant.GetProperty());
			}
			else if (OtherVariant.IsFunction())
			{
				SetFunction(OtherVariant.GetFunction());
			}
		}

		FMVVMConstFieldVariant& operator=(const FMVVMFieldVariant& OtherVariant)
		{
			*this = FMVVMConstFieldVariant(OtherVariant);
			return *this;
		}
	};

} //namespace
