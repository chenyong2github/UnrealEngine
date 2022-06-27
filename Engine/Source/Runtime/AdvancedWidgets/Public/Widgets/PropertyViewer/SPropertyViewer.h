// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/StructOnScope.h"

namespace UE::PropertyViewer
{

class IFieldIterator;
class IFieldExpander;
class INotifyHook;

namespace Private
{
class FPropertyViewerImpl;
}

/** */
class ADVANCEDWIDGETS_API SPropertyViewer : public SCompoundWidget
{
public:
	class FHandle
	{
	private:
		friend SPropertyViewer;
		int32 Id = 0;
	public:
		bool operator==(FHandle Other) const
		{
			return Id == Other.Id;
		}
		bool operator!=(FHandle Other) const
		{
			return Id != Other.Id;
		}
		bool IsValid() const
		{
			return Id != 0;
		}
	};

	enum class EPropertyVisibility
	{
		Hidden,
		Visible,
		Editable,
	};

public:
	DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<SWidget>, FGetFieldWidget, FHandle, const FFieldVariant);
	DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<SWidget>, FOnContextMenuOpening, FHandle, const FFieldVariant);
	DECLARE_DELEGATE_ThreeParams(FOnSelectionChanged, FHandle, const FFieldVariant, ESelectInfo::Type);

	SLATE_BEGIN_ARGS(SPropertyViewer)
	{}
		/** Allow to edit the instance property. */
		SLATE_ARGUMENT_DEFAULT(EPropertyVisibility, PropertyVisibility) = EPropertyVisibility::Hidden;
		/** Sanitize the field and container name. */
		SLATE_ARGUMENT_DEFAULT(bool, bSanitizeName) = false;
		/** Show the icon next to the field name. */
		SLATE_ARGUMENT_DEFAULT(bool, bShowFieldIcon) = true;
		/** Show a search box. */
		SLATE_ARGUMENT_DEFAULT(bool, bShowSearchBox) = false;
		/** Which properties/functions to show. FFieldIterator_BlueprintVisible is the default. */
		SLATE_ARGUMENT_DEFAULT(IFieldIterator*, FieldIterator) = nullptr;
		/** Which properties/functions that allow expansion. FFieldIterator_NoExpand is the default. */
		SLATE_ARGUMENT_DEFAULT(IFieldExpander*, FieldExpander) = nullptr;
		/** Callback when a property is modified. */
		SLATE_ARGUMENT_DEFAULT(INotifyHook*, NotifyHook) = nullptr;
		/** Slot for additional widget to go before the search box. */
		SLATE_NAMED_SLOT(FArguments, SearchBoxPreSlot)
		/** Slot for additional widget to go after the search box. */
		SLATE_NAMED_SLOT(FArguments, SearchBoxPostSlot)
		/** Slot for additional widget to go before the field or container widget. */
		SLATE_EVENT(FGetFieldWidget, OnGetPreSlot)
		/** Slot for additional widget to go after the field or container widget. */
		SLATE_EVENT(FGetFieldWidget, OnGetPostSlot)
		/** Context menu widget for the selected item. */
		SLATE_EVENT(FOnContextMenuOpening, OnContextMenuOpening)
		/** Delegate to invoke when selection changes. */
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Construct(const FArguments& InArgs, const UScriptStruct* Struct);
	void Construct(const FArguments& InArgs, const UScriptStruct* Struct, void* Data);
	void Construct(const FArguments& InArgs, const UClass* Class);
	void Construct(const FArguments& InArgs, UObject* ObjectInstance);
	void Construct(const FArguments& InArgs, const UFunction* Function);

public:
	FHandle AddContainer(const UScriptStruct* Struct);
	FHandle AddContainer(const UClass* Class);
	FHandle AddContainer(const UFunction* Function);
	FHandle AddInstance(const UScriptStruct* Struct, void* Data);
	FHandle AddInstance(UObject* ObjectInstance);

	void Remove(FHandle Identifier);
	void RemoveAll();

private:
	void ConstructInternal(const FArguments& InArgs);
	static FHandle MakeContainerIdentifier();
	TSharedPtr<Private::FPropertyViewerImpl> Implementation;
};

} //namespace
