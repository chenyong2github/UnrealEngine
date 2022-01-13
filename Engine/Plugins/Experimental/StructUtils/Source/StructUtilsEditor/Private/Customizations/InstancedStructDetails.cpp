// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstancedStructDetails.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailCustomNodeBuilder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "Styling/SlateIconFinder.h"
#include "Editor.h"
#include "Engine/UserDefinedStruct.h"
#include "InstancedStruct.h"

#define LOCTEXT_NAMESPACE "StructUtilsEditor"

class FInstancedStructFilter : public IStructViewerFilter
{
public:
	/** The base struct for the property that classes must be a child-of. */
	const UScriptStruct* BaseStruct = nullptr;

	// A flag controlling whether we allow UserDefinedStructs
	bool bAllowUserDefinedStructs = false;

	// A flag controlling whether we allow to select the BaseStruct
	bool bAllowBaseStruct = true;

	virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		if (InStruct->IsA<UUserDefinedStruct>())
		{
			return bAllowUserDefinedStructs;
		}

		if (InStruct == BaseStruct)
		{
			return bAllowBaseStruct;
		}

		if (InStruct->HasMetaData(TEXT("Hidden")))
		{
			return false;
		}

		// Query the native struct to see if it has the correct parent type (if any)
		return !BaseStruct || InStruct->IsChildOf(BaseStruct);
	}

	virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FName InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		// User Defined Structs don't support inheritance, so only include them requested
		return bAllowUserDefinedStructs;
	}
};


////////////////////////////////////

FInstancedStructDataDetails::FInstancedStructDataDetails(TSharedPtr<IPropertyHandle> InStructProperty)
{
#if DO_CHECK
	FStructProperty* StructProp = CastFieldChecked<FStructProperty>(InStructProperty->GetProperty());
	check(StructProp);
	check(StructProp->Struct == FInstancedStruct::StaticStruct());
#endif

	StructProperty = InStructProperty;
}

FInstancedStruct* FInstancedStructDataDetails::GetSingleInstancedStruct()
{
	if (!StructProperty)
	{
		return nullptr;
	}
	TArray<void*> RawData;
	StructProperty->AccessRawData(RawData);
	return RawData.Num() == 1 ? static_cast<FInstancedStruct*>(RawData[0]) : nullptr;
}

void FInstancedStructDataDetails::OnStructPreChange()
{
	if (StructProperty)
	{
		TArray<UObject*> OuterObjects;
		StructProperty->GetOuterObjects(OuterObjects);
		for (UObject* Outer : OuterObjects)
		{
			if (Outer)
			{
				Outer->Modify();
			}
		}
	}
}

void FInstancedStructDataDetails::OnStructChanged()
{
	if (StructProperty)
	{
		StructProperty->NotifyFinishedChangingProperties();
	}
}

void FInstancedStructDataDetails::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
	OnRegenerateChildren = InOnRegenerateChildren;
}

void FInstancedStructDataDetails::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
}

void FInstancedStructDataDetails::GenerateChildContent(IDetailChildrenBuilder& ChildBuilder)
{
	FInstancedStruct* InstancedStruct = GetSingleInstancedStruct();
	if (!InstancedStruct)
	{
		return;
	}

	const UScriptStruct* ScriptStruct = InstancedStruct->GetScriptStruct();
	uint8* Memory = InstancedStruct->GetMutableMemory();
	if (!ScriptStruct || !Memory)
	{
		return;
	}

	// Note: this is potentially dangerous. It puts pointer to external memory on a shared ref.
	TSharedRef<FStructOnScope> Struct = MakeShareable(new FStructOnScope(ScriptStruct, Memory));

	TArray<UPackage*> OuterPackages;
	StructProperty->GetOuterPackages(OuterPackages);
	if (OuterPackages.Num() > 0)
	{
		Struct->SetPackage(OuterPackages[0]);
	}

	static const FName ShowOnlyInnerPropertiesMeta(TEXT("ShowOnlyInnerProperties"));
	StructProperty->SetInstanceMetaData(ShowOnlyInnerPropertiesMeta, FString());

	TArray<TSharedPtr<IPropertyHandle>> ChildProperties = StructProperty->AddChildStructure(Struct);
	for (TSharedPtr<IPropertyHandle> ChildHandle : ChildProperties)
	{
		ChildHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateSP(this, &FInstancedStructDataDetails::OnStructPreChange));
		ChildHandle->SetOnChildPropertyValuePreChange(FSimpleDelegate::CreateSP(this, &FInstancedStructDataDetails::OnStructPreChange));
		ChildHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FInstancedStructDataDetails::OnStructChanged));
		ChildHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FInstancedStructDataDetails::OnStructChanged));

		IDetailPropertyRow& Row = ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		OnChildRowAdded(Row);
	}
}

void FInstancedStructDataDetails::Tick(float DeltaTime)
{
	if (bRefresh)
	{
		OnRegenerateChildren.ExecuteIfBound();
		bRefresh = false;
	}
}

FName FInstancedStructDataDetails::GetName() const
{
	static const FName Name("InstancedStructDataDetails");
	return Name;
}

void FInstancedStructDataDetails::PostUndo(bool bSuccess)
{
	// Regen here directly so that we do not access stale data from FStructOnScope created on GenerateChildContent().
	OnRegenerateChildren.ExecuteIfBound();
	bRefresh = false;
}

void FInstancedStructDataDetails::PostRedo(bool bSuccess)
{
	// Regen here directly so that we do not access stale data from FStructOnScope created on GenerateChildContent().
	OnRegenerateChildren.ExecuteIfBound();
	bRefresh = false;
}

////////////////////////////////////


TSharedRef<IPropertyTypeCustomization> FInstancedStructDetails::MakeInstance()
{
	return MakeShareable(new FInstancedStructDetails);
}

void FInstancedStructDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	// Find base class from meta data.
	static const FName BaseClassMetaName(TEXT("BaseStruct"));
	FString BaseClassName = StructProperty->GetMetaData(BaseClassMetaName);
	BaseScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseClassName);

	static const FName StructTypeConst(TEXT("StructTypeConst"));
	const bool bEnableStructSelection = !StructProperty->HasMetaData(StructTypeConst);

	const FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FInstancedStructDetails::ShouldResetToDefault);
	const FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FInstancedStructDetails::ResetToDefault);
	const FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(ComboButton, SComboButton)
			.OnGetMenuContent(this, &FInstancedStructDetails::GenerateStructPicker)
			.ContentPadding(0)
			.IsEnabled(bEnableStructSelection)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SImage)
					.Image(this, &FInstancedStructDetails::GetDisplayValueIcon)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &FInstancedStructDetails::GetDisplayValueString)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		]
		.OverrideResetToDefault(ResetOverride);
}


bool FInstancedStructDetails::ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	check(StructProperty);
	
	bool bAnyValid = false;
	
	TArray<void*> RawData;
	StructProperty->AccessRawData(RawData);
	for (void* Data : RawData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			if (Struct->IsValid())
			{
				bAnyValid = true;
				break;
			}
		}
	}
	
	// Assume that the default value is empty. Any valid means that some can be reset to empty.
	return bAnyValid;
}

void FInstancedStructDetails::ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	check(StructProperty);
	
	GEditor->BeginTransaction(LOCTEXT("OnResetToDefault", "Reset to default"));

	StructProperty->NotifyPreChange();

	TArray<void*> RawData;
	StructProperty->AccessRawData(RawData);
	for (void* Data : RawData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			// Assume that the default value is empty.
			Struct->Reset();
		}
	}

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	GEditor->EndTransaction();
	StructProperty->NotifyFinishedChangingProperties();

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

void FInstancedStructDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedRef<FInstancedStructDataDetails> DataDetails = MakeShareable(new FInstancedStructDataDetails(StructProperty));
	StructBuilder.AddCustomBuilder(DataDetails);
}

const UScriptStruct* FInstancedStructDetails::GetCommonScriptStruct() const
{
	if (!StructProperty)
	{
		return nullptr;
	}
	const UScriptStruct* CommonScriptStruct = nullptr;
	bool bMultipleValues = false;
	TArray<void*> RawData;
	StructProperty->AccessRawData(RawData);
	for (void* Data : RawData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			const UScriptStruct* ScriptStruct = Struct->GetScriptStruct();
			if (!bMultipleValues && !CommonScriptStruct)
			{
				CommonScriptStruct = ScriptStruct;
			}
			else if (ScriptStruct != CommonScriptStruct)
			{
				CommonScriptStruct = nullptr;
				bMultipleValues = true;
			}
		}
	}

	return CommonScriptStruct;
}

FText FInstancedStructDetails::GetDisplayValueString() const
{
	const UScriptStruct* ScriptStruct = GetCommonScriptStruct();
	if (ScriptStruct)
	{
		return ScriptStruct->GetDisplayNameText();
	}
	return FText();
}

const FSlateBrush* FInstancedStructDetails::GetDisplayValueIcon() const
{
	return FSlateIconFinder::FindIconBrushForClass(UScriptStruct::StaticClass());
}

TSharedRef<SWidget> FInstancedStructDetails::GenerateStructPicker()
{
	static const FName ExcludeBaseStruct(TEXT("ExcludeBaseStruct"));
	const bool bExcludeBaseStruct = StructProperty->HasMetaData(ExcludeBaseStruct);

	TSharedRef<FInstancedStructFilter> StructFilter = MakeShared<FInstancedStructFilter>();
	StructFilter->BaseStruct = BaseScriptStruct;
	StructFilter->bAllowUserDefinedStructs = false;
	StructFilter->bAllowBaseStruct = !bExcludeBaseStruct;

	FStructViewerInitializationOptions Options;
	Options.bShowUnloadedStructs = true;
	Options.bShowNoneOption = true;
	Options.StructFilter = StructFilter;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EStructViewerDisplayMode::ListView;
	Options.bAllowViewOptions = true;

	FOnStructPicked OnPicked(FOnStructPicked::CreateRaw(this, &FInstancedStructDetails::OnStructPicked));

	return SNew(SBox)
		.WidthOverride(280)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(500)
			[
				FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer").CreateStructViewer(Options, OnPicked)
			]
		];
}

void FInstancedStructDetails::OnStructPicked(const UScriptStruct* InStruct)
{
	if (StructProperty)
	{
		GEditor->BeginTransaction(LOCTEXT("OnStructPicked", "Set Struct"));

		StructProperty->NotifyPreChange();

		TArray<void*> RawData;
		StructProperty->AccessRawData(RawData);
		for (void* Data : RawData)
		{
			if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
			{
				if (InStruct)
				{
					Struct->InitializeAs(InStruct);
				}
				else
				{
					Struct->Reset();
				}
			}
		}

		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
		GEditor->EndTransaction();
		StructProperty->NotifyFinishedChangingProperties();

		GEditor->EndTransaction();
	}

	ComboButton->SetIsOpen(false);

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
