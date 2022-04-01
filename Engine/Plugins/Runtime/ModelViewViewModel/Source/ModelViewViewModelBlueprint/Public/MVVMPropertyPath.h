// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Types/MVVMBindingName.h"

#include "MVVMPropertyPath.generated.h"

/**
 *
 */
USTRUCT()
struct FMVVMPropertyPathBase
{
	GENERATED_BODY()

private:
	/** Name we are looking for. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FMVVMBindingName BindingName;

	/** The path of the property (either the getter or the set action). */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FString GetterPropertyPath;

	/** The path of the property (either the setter or the set action). */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FString SetterPropertyPath;

public:
	FMVVMBindingName GetBindingName() const
	{
		return BindingName;
	}

	void SetBindingName(FName InBindingName)
	{
		BindingName = FMVVMBindingName(InBindingName);
	}

	FString GetGetterPropertyPath() const
	{
		return !GetterPropertyPath.IsEmpty() ? GetterPropertyPath : BindingName.ToString();
	}

	FString GetSetterPropertyPath() const
	{
		return !SetterPropertyPath.IsEmpty() ? SetterPropertyPath : BindingName.ToString();
	}

	bool operator==(const FMVVMPropertyPathBase& Other) const
	{
		return BindingName == Other.BindingName &&
			GetterPropertyPath == Other.GetterPropertyPath &&
			SetterPropertyPath == Other.SetterPropertyPath;
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
