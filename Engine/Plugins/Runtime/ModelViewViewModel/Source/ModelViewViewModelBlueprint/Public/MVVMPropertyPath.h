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
struct FMVVMBlueprintPropertyPath
{
	GENERATED_BODY()

private:
	/** Reference to property for this binding. */
	UPROPERTY(EditAnywhere, Category = "MVVM")
	TArray<FMVVMBlueprintFieldPath> Paths;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FName WidgetName;

	UPROPERTY(EditAnywhere, Category = "MVVM")
	FGuid ContextId;

#if WITH_EDITORONLY_DATA
	// Use the Paths. BindingReference and BindingKind are deprecated.
	UPROPERTY()
	FMemberReference BindingReference;
	UPROPERTY()
	EBindingKind BindingKind = EBindingKind::Function;
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
	}

	void ResetBasePropertyPath()
	{
		Paths.Reset();
	}

	void ResetSource()
	{
		ContextId = FGuid();
		WidgetName = FName();
	}

	bool IsFromWidget() const
	{
		return !WidgetName.IsNone();
	}

	bool IsFromViewModel() const
	{
		return ContextId.IsValid();
	}

	FGuid GetViewModelId() const
	{
		return ContextId;
	}

	void SetViewModelId(FGuid InViewModelId)
	{
		WidgetName = FName();
		ContextId = InViewModelId;
	}

	FName GetWidgetName() const
	{
		return WidgetName;
	}

	void SetWidgetName(FName InWidgetName)
	{
		ContextId = FGuid();
		WidgetName = InWidgetName;
	}

	bool IsEmpty() const
	{
		return !IsFromWidget() && !IsFromViewModel() && BindingReference.GetMemberName() == FName();
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

	bool operator==(const FMVVMBlueprintPropertyPath& Other) const
	{
		return Paths == Other.Paths;
	}

	bool operator!=(const FMVVMBlueprintPropertyPath& Other) const
	{
		return !operator==(Other);
	}

	void PostSerialize(const FArchive& Ar)
	{
		if (Ar.IsLoading())
		{
			if (!BindingReference.GetMemberName().IsNone())
			{
				Paths.AddDefaulted_GetRef().SetDeprecatedBindingReference(BindingReference, BindingKind);
				BindingReference = FMemberReference();
			}
		}
	}
};

template<>
struct TStructOpsTypeTraits<FMVVMBlueprintPropertyPath> : public TStructOpsTypeTraitsBase2<FMVVMBlueprintPropertyPath>
{
	enum
	{
		WithPostSerialize = true,
	};
};