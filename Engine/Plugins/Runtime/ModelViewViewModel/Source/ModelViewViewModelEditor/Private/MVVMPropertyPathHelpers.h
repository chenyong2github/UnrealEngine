// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
#include "Misc/Optional.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingName.h"
#include "Types/MVVMBindingSource.h"
#include "Types/MVVMFieldVariant.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UE::MVVM
{
	class IFieldPathHelper
	{
	public:
		virtual void GetAvailableFields(TSet<FMVVMConstFieldVariant>& Fields) const = 0;
		virtual FMVVMConstFieldVariant GetSelectedField() const = 0;

		virtual void GetAvailableSources(TSet<FBindingSource>& OutSources) const = 0;
		virtual TOptional<FBindingSource> GetSelectedSource() const = 0;
		virtual void SetSelectedSource(const TOptional<FBindingSource>& Source) const = 0;

		virtual FMVVMBindingName GetBindingName() const = 0;
		virtual void SetBindingName(FName BindingName) const = 0;
	};

	class FWidgetFieldPathHelper : public IFieldPathHelper
	{
	public:
		FWidgetFieldPathHelper() {}
		virtual ~FWidgetFieldPathHelper() {}
		FWidgetFieldPathHelper(const TAttribute<FMVVMWidgetPropertyPath*>& WidgetPath, const UWidgetBlueprint* InWidgetBlueprint) :
			PathAttr(WidgetPath),
			WidgetBlueprint(InWidgetBlueprint)
		{
		}

		virtual void GetAvailableFields(TSet<FMVVMConstFieldVariant>& Fields) const override;
		virtual FMVVMConstFieldVariant GetSelectedField() const override;

		virtual void GetAvailableSources(TSet<FBindingSource>& OutSources) const override;
		virtual TOptional<FBindingSource> GetSelectedSource() const override;
		virtual void SetSelectedSource(const TOptional<FBindingSource>& Source) const override;

		virtual FMVVMBindingName GetBindingName() const override;
		virtual void SetBindingName(FName BindingName) const override;

	private:
		TAttribute<FMVVMWidgetPropertyPath*> PathAttr;
		const UWidgetBlueprint* WidgetBlueprint = nullptr;
	};

	class FViewModelFieldPathHelper : public IFieldPathHelper
	{
	public:
		FViewModelFieldPathHelper() {}
		virtual ~FViewModelFieldPathHelper() {}
		FViewModelFieldPathHelper(const TAttribute<FMVVMViewModelPropertyPath*>& ViewModelPath, const UWidgetBlueprint* InWidgetBlueprint) :
			PathAttr(ViewModelPath),
			WidgetBlueprint(InWidgetBlueprint)
		{
		}

		virtual void GetAvailableFields(TSet<FMVVMConstFieldVariant>& Fields) const override;
		virtual FMVVMConstFieldVariant GetSelectedField() const override;

		virtual void GetAvailableSources(TSet<FBindingSource>& OutSources) const override;
		virtual TOptional<FBindingSource> GetSelectedSource() const override;
		virtual void SetSelectedSource(const TOptional<FBindingSource>& Source) const override;

		virtual FMVVMBindingName GetBindingName() const override;
		virtual void SetBindingName(FName BindingName) const override;

	private:
		TAttribute<FMVVMViewModelPropertyPath*> PathAttr;
		const UWidgetBlueprint* WidgetBlueprint = nullptr;
	};
}
