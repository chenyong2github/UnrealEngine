// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/PropertyViewer/IFieldIterator.h"
#include "Misc/EnumClassFlags.h"
#include "MVVMPropertyPath.h"
#include "Templates/SubclassOf.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/PropertyViewer/SPropertyViewer.h"

class UWidgetBlueprint;
struct FMVVMBlueprintViewModelContext;
class UWidgetBlueprint;

namespace UE::MVVM
{
	struct FBindingSource;

	enum class EFieldVisibility : uint8
	{
		None = 0,
		Readable = 1 << 1,
		Writable = 1 << 2,
		Notify = 1 << 3,
		All = 0xff
	};

	ENUM_CLASS_FLAGS(EFieldVisibility)

	/**
	 * 
	 */
	class FFieldIterator_Bindable : public UE::PropertyViewer::FFieldIterator_BlueprintVisible
	{
	public:
		FFieldIterator_Bindable(const UWidgetBlueprint* WidgetBlueprint, EFieldVisibility InVisibilityFlags);
		virtual TArray<FFieldVariant> GetFields(const UStruct*) const override;

	private:
		TWeakObjectPtr<const UWidgetBlueprint> WidgetBlueprint; 
		EFieldVisibility FieldVisibilityFlags = EFieldVisibility::All;
	};

	/** 
	 * 
	 */
	class SSourceBindingList : public SCompoundWidget
	{
	public:

		DECLARE_DELEGATE_OneParam(FOnDoubleClicked, const FMVVMBlueprintPropertyPath&);
	
		SLATE_BEGIN_ARGS(SSourceBindingList) {}
			SLATE_ARGUMENT_DEFAULT(bool, ShowSearchBox) = false;
			SLATE_ARGUMENT_DEFAULT(EFieldVisibility, FieldVisibilityFlags) = EFieldVisibility::All;
			SLATE_EVENT(FOnDoubleClicked, OnDoubleClicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint);

		void Clear();

		void AddSource(UClass* Class, FName Name, FGuid Guid);
		void AddSources(TArrayView<const FBindingSource> InSources);

		void AddWidgetBlueprint(const UWidgetBlueprint* WidgetBlueprint);
		void AddWidgets(TArrayView<const UWidget*> Widgets);
		void AddViewModels(TArrayView<const FMVVMBlueprintViewModelContext> ViewModels);

		FMVVMBlueprintPropertyPath GetSelectedProperty() const;

		void SetRawFilterText(const FText& InFilterText);

	private:
		using SPropertyViewer = UE::PropertyViewer::SPropertyViewer;

		void HandleSelectionChanged(SPropertyViewer::FHandle ViewModel, TArrayView<const FFieldVariant> Path, ESelectInfo::Type SelectionType);
		void HandleDoubleClicked(SPropertyViewer::FHandle ViewModel, TArrayView<const FFieldVariant> Path);

		FMVVMBlueprintPropertyPath CreateBlueprintPropertyPath(SPropertyViewer::FHandle Handle, TArrayView<const FFieldVariant> Path) const;

	private:
		FOnDoubleClicked OnDoubleClicked;
		TUniquePtr<FFieldIterator_Bindable> FieldIterator;
		TArray<TPair<FBindingSource, SPropertyViewer::FHandle>> Sources;
		FMVVMBlueprintPropertyPath SelectedPath;
		TSharedPtr<SPropertyViewer> PropertyViewer;
	};

} //namespace UE::MVVM