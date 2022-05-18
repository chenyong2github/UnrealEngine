// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Types/MVVMBindingName.h"
#include "Types/MVVMFieldVariant.h"

#include "MVVMPropertyPath.generated.h"

/**
 * A single item in a Property Path
 */
USTRUCT()
struct FMVVMBlueprintFieldPath
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
	FName GetFieldName() const
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

		return Path;
	}

	/** */
	UE::MVVM::FMVVMConstFieldVariant GetField() const
	{
		if (!BindingReference.GetMemberName().IsNone())
		{
			if (BindingKind == EBindingKind::Property)
			{
				return UE::MVVM::FMVVMConstFieldVariant(BindingReference.ResolveMember<FProperty>());
			}
			else if (BindingKind == EBindingKind::Function)
			{
				return UE::MVVM::FMVVMConstFieldVariant(BindingReference.ResolveMember<UFunction>());
			}
		}
		return UE::MVVM::FMVVMConstFieldVariant();
	}

	FMemberReference GetBindingReference() const
	{
		// Note: Prefer copy since FName & FGuid, FString should be empty based on our usage
		return BindingReference;
	}

	void SetBindingReference(UE::MVVM::FMVVMConstFieldVariant InField)
	{
		if (InField.IsEmpty())
		{
			Reset();
		}
		else
		{
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
	}

	void Reset()
	{
		BindingReference = FMemberReference();
	}

	void SetDeprecatedBindingReference(const FMemberReference& InBindingReference, EBindingKind InBindingKind)
	{
		BindingReference = InBindingReference;
		BindingKind = InBindingKind;
	}

public:
	bool operator==(const FMVVMBlueprintFieldPath& Other) const
	{
		return BindingReference.GetMemberName() == Other.BindingReference.GetMemberName();
	}
	bool operator!=(const FMVVMBlueprintFieldPath& Other) const
	{
		return !operator==(Other);
	}
};


/**
 * Base path to properties for MVVM view models and widgets.
 * 
 * Used to associate properties within MVVM bindings in editor & during MVVM compilation
 */
USTRUCT()
struct FMVVMBlueprintPropertyPathBase
{
	GENERATED_BODY()

private:
	/** Reference to property for this binding. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	TArray<FMVVMBlueprintFieldPath> Paths;

#if WITH_EDITORONLY_DATA
	// Use the Paths. BindingReference and BindingKind are deprecated.
	UPROPERTY()
	FMemberReference BindingReference;
	UPROPERTY()
	EBindingKind BindingKind;
#endif

public:
	/** Get the binding name, resolves reference deprecation / redirectors / etc before returning */
	TArray<FName> GetPaths() const
	{
		TArray<FName> Result;
		Result.Reserve(Paths.Num());

		for (const FMVVMBlueprintFieldPath& Path : Paths)
		{
			Result.Add(Path.GetFieldName());
		}

		return Result;
	}

	bool BasePropertyPathContains(UE::MVVM::FMVVMConstFieldVariant Field) const
	{
		return Paths.ContainsByPredicate([Field](const FMVVMBlueprintFieldPath& FieldPath) { return FieldPath.GetField() == Field;  });
	}

	void SetBasePropertyPath(UE::MVVM::FMVVMConstFieldVariant InField)
	{
		Paths.Reset();
		Paths.AddDefaulted_GetRef().SetBindingReference(InField);

		if (InField.IsProperty() && InField.GetProperty()->IsA(FStructProperty::StaticClass()))
		{
			if (FProperty* Pro = ((FStructProperty*)InField.GetProperty())->Struct->FindPropertyByName("A"))
				Paths.AddDefaulted_GetRef().SetBindingReference(UE::MVVM::FMVVMConstFieldVariant(Pro));
		}
		if (InField.IsProperty() && InField.GetProperty()->IsA(FObjectPropertyBase::StaticClass()))
		{
			if (FProperty* Pro = ((FObjectPropertyBase*)InField.GetProperty())->PropertyClass->FindPropertyByName("Dummy"))
				Paths.AddDefaulted_GetRef().SetBindingReference(UE::MVVM::FMVVMConstFieldVariant(Pro));
		}
	}

	void ResetBasePropertyPath()
	{
		Paths.Reset();
	}

	/**
	 * Get the full path without the first property name.
	 * returns Field.SubProperty.SubProperty from ViewModel.Field.SubProeprty.SubProperty
	 */
	FString GetBasePropertyPath() const
	{
		TStringBuilder<512> Result;
		for (const FMVVMBlueprintFieldPath& Path : Paths)
		{
			if (Result.Len() > 0)
			{
				Result << TEXT('.');
			}
			Result << Path.GetFieldName().ToString();
		}
		return Result.ToString();
	}

public:
	bool operator==(const FMVVMBlueprintPropertyPathBase& Other) const
	{
		return Paths == Other.Paths;
	}
	bool operator!=(const FMVVMBlueprintPropertyPathBase& Other) const
	{
		return !operator==(Other);
	}

public:
	void PostSerialize(const FArchive& Ar)
	{
		if (Ar.IsLoading() && !BindingReference.GetMemberName().IsNone())
		{
			Paths.AddDefaulted_GetRef().SetDeprecatedBindingReference(BindingReference, BindingKind);
			BindingReference = FMemberReference();
		}
	}
};

template<>
struct TStructOpsTypeTraits<FMVVMBlueprintPropertyPathBase> : public TStructOpsTypeTraitsBase2<FMVVMBlueprintPropertyPathBase>
{
	enum
	{
		WithPostSerialize = true,
	};
};


/** */
USTRUCT(BlueprintType)
struct FMVVMViewModelPropertyPath : public FMVVMBlueprintPropertyPathBase
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FGuid ContextId;

	bool operator==(const FMVVMViewModelPropertyPath& Other) const
	{
		return ContextId == Other.ContextId &&
			FMVVMBlueprintPropertyPathBase::operator==(Other);
	}

	bool operator!=(const FMVVMViewModelPropertyPath& Other) const
	{
		return ContextId != Other.ContextId &&
			FMVVMBlueprintPropertyPathBase::operator!=(Other);
	}
};


template<>
struct TStructOpsTypeTraits<FMVVMViewModelPropertyPath> : public TStructOpsTypeTraitsBase2<FMVVMViewModelPropertyPath>
{
	enum
	{
		WithPostSerialize = true,
	};
};


/** */
USTRUCT(BlueprintType)
struct FMVVMWidgetPropertyPath : public FMVVMBlueprintPropertyPathBase
{
	GENERATED_BODY();

	/** The context from which the destination path will be evaluated from. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	FName WidgetName;

	bool operator==(const FMVVMWidgetPropertyPath& Other) const
	{
		return WidgetName == Other.WidgetName &&
			FMVVMBlueprintPropertyPathBase::operator==(Other);
	}


	bool operator!=(const FMVVMWidgetPropertyPath& Other) const
	{
		return WidgetName != Other.WidgetName ||
			FMVVMBlueprintPropertyPathBase::operator!=(Other);
	}
};


template<>
struct TStructOpsTypeTraits<FMVVMWidgetPropertyPath> : public TStructOpsTypeTraitsBase2<FMVVMWidgetPropertyPath>
{
	enum
	{
		WithPostSerialize = true,
	};
};
