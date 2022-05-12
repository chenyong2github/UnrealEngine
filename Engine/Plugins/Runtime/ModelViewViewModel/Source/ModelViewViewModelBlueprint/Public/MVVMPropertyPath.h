// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Types/MVVMBindingName.h"
#include "Types/MVVMFieldVariant.h"

#include "MVVMPropertyPath.generated.h"

/**
 * Base path to properties for MVVM view models and widgets.
 * 
 * Used to associate properties within MVVM bindings in editor & during MVVM compilation
 */
USTRUCT()
struct FMVVMPropertyPathBase
{
	GENERATED_BODY()

private:
	/** Reference to property for this binding. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMemberReference BindingReference;

	/** If we are referencing a UFunction or FProperty */
	UPROPERTY()
	EBindingKind BindingKind = EBindingKind::Function;

public:
	/** Get the binding name, resolves reference deprecation / redirectors / etc before returning */
	FMVVMBindingName GetBindingName() const
	{
		// Resolve any redirectors 
		FName Path = FName();
		if (!BindingReference.GetMemberName().IsNone())
		{
			if (BindingKind == EBindingKind::Property && BindingReference.ResolveMember<FProperty>())
			{
				Path = BindingReference.GetMemberName();
			}
			else if (BindingKind == EBindingKind::Function && BindingReference.ResolveMember<UFunction>())
			{
				Path = BindingReference.GetMemberName();
			}
		}

		return FMVVMBindingName(Path);
	}

	FMemberReference GetBindingReference() const
	{
		// Note: Prefer copy since FName & FGuid, FString should be empty based on our usage
		return BindingReference;
	}

	void SetBindingReference(const UE::MVVM::FMVVMConstFieldVariant& InField)
	{
		if (InField.IsEmpty())
		{
			Reset();
			return;
		}

		BindingReference = InField.CreateMemberReference();

		if (InField.IsProperty())
		{
			BindingKind = EBindingKind::Property;
		}
		else if (InField.IsFunction())
		{
			BindingKind = EBindingKind::Function;
		}
		else
		{
			ensureAlwaysMsgf(false, TEXT("Binding to field of unknown type!"));
		}
	}

	void Reset()
	{
		BindingReference = FMemberReference();
	}

	FString GetGetterPropertyPath() const
	{
		return GetBindingName().ToString();
	}

	FString GetSetterPropertyPath() const
	{
		return GetBindingName().ToString();
	}

	bool operator==(const FMVVMPropertyPathBase& Other) const
	{
		return BindingReference.GetMemberName() == Other.BindingReference.GetMemberName();
	}
	bool operator!=(const FMVVMPropertyPathBase& Other) const
	{
		return !operator==(Other);
	}
};

USTRUCT(BlueprintType)
struct FMVVMViewModelPropertyPath : public FMVVMPropertyPathBase
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FGuid ContextId;

	bool operator==(const FMVVMViewModelPropertyPath& Other) const
	{
		return ContextId == Other.ContextId &&
			FMVVMPropertyPathBase::operator==(Other);
	}

	bool operator!=(const FMVVMViewModelPropertyPath& Other) const
	{
		return ContextId != Other.ContextId &&
			FMVVMPropertyPathBase::operator!=(Other);
	}
};

USTRUCT(BlueprintType)
struct FMVVMWidgetPropertyPath : public FMVVMPropertyPathBase
{
	GENERATED_BODY();

	/** The context from which the destination path will be evaluated from. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FName WidgetName;

	bool operator==(const FMVVMWidgetPropertyPath& Other) const
	{
		return WidgetName == Other.WidgetName &&
			FMVVMPropertyPathBase::operator==(Other);
	}


	bool operator!=(const FMVVMWidgetPropertyPath& Other) const
	{
		return WidgetName != Other.WidgetName ||
			FMVVMPropertyPathBase::operator!=(Other);
	}
};
