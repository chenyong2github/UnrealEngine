// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
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
		virtual ~IFieldPathHelper() {}

		virtual void GetAvailableFields(TSet<FMVVMConstFieldVariant>& Fields) const = 0;
		virtual FMVVMConstFieldVariant GetSelectedField() const = 0;

		virtual void GetAvailableSources(TSet<FBindingSource>& OutSources) const = 0;
		virtual FBindingSource GetSelectedSource() const = 0;
		virtual void SetSelectedSource(const FBindingSource& Source) const = 0;

		virtual FMVVMBindingName GetBindingName() const = 0;
		virtual void SetBindingReference(const FMVVMConstFieldVariant& InField) const = 0;
		virtual void ResetBinding() const = 0;
	};

	class FWidgetFieldPathHelper : public IFieldPathHelper
	{
	public:
		FWidgetFieldPathHelper() {}
		virtual ~FWidgetFieldPathHelper() {}
		FWidgetFieldPathHelper(FMVVMBlueprintPropertyPath* WidgetPath, const UWidgetBlueprint* InWidgetBlueprint) :
			Path(WidgetPath),
			WidgetBlueprint(InWidgetBlueprint)
		{
			check(Path);
			check(Path->IsFromWidget() || Path->IsEmpty());
		}

		virtual void GetAvailableFields(TSet<FMVVMConstFieldVariant>& Fields) const override;
		virtual FMVVMConstFieldVariant GetSelectedField() const override;

		virtual void GetAvailableSources(TSet<FBindingSource>& OutSources) const override;
		virtual FBindingSource GetSelectedSource() const override;
		virtual void SetSelectedSource(const FBindingSource& Source) const override;

		virtual FMVVMBindingName GetBindingName() const override;
		virtual void SetBindingReference(const FMVVMConstFieldVariant& InField) const override;
		virtual void ResetBinding() const override;

	private:
		FMVVMBlueprintPropertyPath* Path;
		const UWidgetBlueprint* WidgetBlueprint = nullptr;
	};

	class FViewModelFieldPathHelper : public IFieldPathHelper
	{
	public:
		FViewModelFieldPathHelper() {}
		virtual ~FViewModelFieldPathHelper() {}
		FViewModelFieldPathHelper(FMVVMBlueprintPropertyPath* ViewModelPath, const UWidgetBlueprint* InWidgetBlueprint) :
			Path(ViewModelPath),
			WidgetBlueprint(InWidgetBlueprint)
		{
			check(Path);
			check(Path->IsFromViewModel() || Path->IsEmpty());
		}

		virtual void GetAvailableFields(TSet<FMVVMConstFieldVariant>& Fields) const override;
		virtual FMVVMConstFieldVariant GetSelectedField() const override;

		virtual void GetAvailableSources(TSet<FBindingSource>& OutSources) const override;
		virtual FBindingSource GetSelectedSource() const override;
		virtual void SetSelectedSource(const FBindingSource& Source) const override;

		virtual FMVVMBindingName GetBindingName() const override;
		virtual void SetBindingReference(const FMVVMConstFieldVariant& InField) const override;
		virtual void ResetBinding() const override;

	private:
		FMVVMBlueprintPropertyPath* Path;
		const UWidgetBlueprint* WidgetBlueprint = nullptr;
	};
}
