// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewModelContextCustomization.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "WidgetBlueprintEditor.h"
#include "IPropertyAccessEditor.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Features/IModularFeatures.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SMVVMViewModelPanel.h"

#define LOCTEXT_NAMESPACE "BlueprintViewModelContextDetailCustomization"

namespace UE::MVVM
{
namespace Private
{
	FText BindingWidgetForVM_GetName()
	{
		return FText::GetEmpty();
	}

	bool BindingWidgetForVM_CanBindProperty(const FProperty* Property, const UClass* ClassToLookFor)
	{
		const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property);
		return (ObjectProperty != nullptr && ObjectProperty->PropertyClass->IsChildOf(ClassToLookFor));
	}
}

/**
 *
 */
bool FViewModelPropertyAccessEditor::CanBindProperty(FProperty* Property) const
{
	// Property == GeneratePureBindingsProperty is only to start the algo
	return ViewModelProperty != Property && (Private::BindingWidgetForVM_CanBindProperty(Property, ClassToLookFor.Get()) || Property == GeneratePureBindingsProperty);
}

bool FViewModelPropertyAccessEditor::CanBindFunction(UFunction* Function) const
{
	return Private::BindingWidgetForVM_CanBindProperty(BindingHelper::GetReturnProperty(Function), ClassToLookFor.Get());
}

bool FViewModelPropertyAccessEditor::CanBindToClass(UClass* Class) const
{
	return true;
}

void FViewModelPropertyAccessEditor::AddBinding(FName, const TArray<FBindingChainElement>& BindingChain)
{
	TStringBuilder<256> Path;
	for (const FBindingChainElement& Binding : BindingChain)
	{
		if (Path.Len() != 0)
		{
			Path << TEXT('.');
		}
		Path << Binding.Field.GetFName();
	}

	AssignToProperty->SetValue(Path.ToString());
}

bool FViewModelPropertyAccessEditor::HasValidClassToLookFor() const
{
	return ClassToLookFor.Get() != nullptr;
}

TSharedRef<SWidget> FViewModelPropertyAccessEditor::MakePropertyBindingWidget(TSharedRef<FWidgetBlueprintEditor> WidgetBlueprintEditor, FProperty* PropertyToMatch, TSharedRef<IPropertyHandle> InAssignToProperty, FName ViewModelPropertyName)
{
	UClass* SkeletonClass = WidgetBlueprintEditor->GetBlueprintObj()->SkeletonGeneratedClass.Get();
	if (!SkeletonClass)
	{
		return SNullWidget::NullWidget;
	}
	ViewModelProperty = SkeletonClass->FindPropertyByName(ViewModelPropertyName);
	AssignToProperty = InAssignToProperty;

	if (!IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		return SNullWidget::NullWidget;
	}

	GeneratePureBindingsProperty = PropertyToMatch;
	FPropertyBindingWidgetArgs Args;
	Args.Property = GeneratePureBindingsProperty;
	Args.bAllowArrayElementBindings = false;
	Args.bAllowStructMemberBindings = true;
	Args.bAllowUObjectFunctions = true;
	Args.bAllowStructFunctions = true;
	Args.bAllowNewBindings = true;
	Args.bGeneratePureBindings = true;

	Args.CurrentBindingText.BindStatic(&Private::BindingWidgetForVM_GetName);
	Args.OnCanBindProperty.BindRaw(this, &FViewModelPropertyAccessEditor::CanBindProperty);
	Args.OnCanBindFunction.BindRaw(this, &FViewModelPropertyAccessEditor::CanBindFunction);
	Args.OnCanBindToClass.BindRaw(this, &FViewModelPropertyAccessEditor::CanBindToClass);
	Args.OnAddBinding.BindRaw(this, &FViewModelPropertyAccessEditor::AddBinding);

	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
	TSharedRef<SWidget> Result = PropertyAccessEditor.MakePropertyBindingWidget(WidgetBlueprintEditor->GetBlueprintObj(), Args);
	Result->SetEnabled(MakeAttributeRaw(this, &FViewModelPropertyAccessEditor::HasValidClassToLookFor));
	return Result;
}

/**
 * 
 */
FBlueprintViewModelContextDetailCustomization::FBlueprintViewModelContextDetailCustomization(TWeakPtr<FWidgetBlueprintEditor> InEditor)
	: WidgetBlueprintEditor(InEditor)
{}


void FBlueprintViewModelContextDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	FName ViewModelPropertyName;
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIndex);
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();

		if (PropertyName == "NotifyFieldValueClass")
		{
			ensure(CastField<FClassProperty>(ChildHandle->GetProperty()));
			NotifyFieldValueClassHandle = ChildHandle;
			UObject* Object = nullptr;
			if (ChildHandle->GetValue(Object) == FPropertyAccess::Success)
			{
				UClass* ViewModelClass = Cast<UClass>(Object);
				if (ViewModelClass)
				{
					AllowedCreationTypes = GetAllowedContextCreationType(ViewModelClass);
				}
				PropertyAccessEditor.ClassToLookFor = ViewModelClass;
			}
			ChildHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FBlueprintViewModelContextDetailCustomization::HandleClassChanged));
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewModelContext, ViewModelName))
		{
			ensure(CastField<FNameProperty>(ChildHandle->GetProperty()));
			ChildHandle->GetValue(ViewModelPropertyName);
		}
		
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewModelContext, ViewModelPropertyPath))
		{
			ensure(CastField<FStrProperty>(ChildHandle->GetProperty()));
			PropertyPathHandle = ChildHandle;
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewModelContext, CreationType))
		{
			ensure(CastField<FEnumProperty>(ChildHandle->GetProperty()));
			CreationTypeHandle = ChildHandle;
		}
	}

	if (NotifyFieldValueClassHandle)
	{
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIndex);
			const FName PropertyName = ChildHandle->GetProperty()->GetFName();

			if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewModelContext, ViewModelPropertyPath))
			{
				ensure(CastField<FStrProperty>(ChildHandle->GetProperty()));
				if (TSharedPtr<FWidgetBlueprintEditor> SharedWidgetBlueprintEditor = WidgetBlueprintEditor.Pin())
				{
					IDetailPropertyRow& PropertyRow = ChildBuilder.AddProperty(ChildHandle.ToSharedRef());

					TSharedPtr<SWidget> NameWidget, ValueWidget;
					PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget);
					PropertyRow.CustomWidget()
					.NameContent()
					[
						NameWidget.ToSharedRef()
					]
					.ValueContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							ValueWidget.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							PropertyAccessEditor.MakePropertyBindingWidget(SharedWidgetBlueprintEditor.ToSharedRef(), NotifyFieldValueClassHandle->GetProperty(), PropertyPathHandle.ToSharedRef(), ViewModelPropertyName)
						]
					];
				}
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewModelContext, CreationType))
			{
				IDetailPropertyRow& PropertyRow = ChildBuilder.AddProperty(ChildHandle.ToSharedRef());

				TSharedPtr<SWidget> NameWidget, ValueWidget;
				PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget);
				PropertyRow.CustomWidget()
					.NameContent()
					[
						NameWidget.ToSharedRef()
					]
					.ValueContent()
					[
						SNew(SComboButton)
						.ContentPadding(FMargin(4.f, 0.f))
						.OnGetMenuContent(this, &FBlueprintViewModelContextDetailCustomization::CreateExecutionTypeMenuContent)
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(this, &FBlueprintViewModelContextDetailCustomization::GetCreationTypeValue)
							.ToolTipText(this, &FBlueprintViewModelContextDetailCustomization::GetExecutionTypeValueToolTip)
						]
					];
			}
			else
			{
				ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
			}
		}
	}
}

void FBlueprintViewModelContextDetailCustomization::HandleClassChanged()
{
	UObject* Object = nullptr;
	AllowedCreationTypes.Reset();
	PropertyAccessEditor.ClassToLookFor.Reset();
	if (NotifyFieldValueClassHandle->GetValue(Object) == FPropertyAccess::Success)
	{
		if (UClass* ViewModelClass = Cast<UClass>(Object))
		{
			PropertyAccessEditor.ClassToLookFor = ViewModelClass;
			AllowedCreationTypes = GetAllowedContextCreationType(ViewModelClass);
		}
	}
}

TSharedRef<SWidget> FBlueprintViewModelContextDetailCustomization::CreateExecutionTypeMenuContent()
{
	const bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, nullptr, nullptr, true);

	const UEnum* EnumCreationType = StaticEnum<EMVVMBlueprintViewModelContextCreationType>();
	for (EMVVMBlueprintViewModelContextCreationType Type : AllowedCreationTypes)
	{
		int32 Index = EnumCreationType->GetIndexByValue((int64)Type);
		MenuBuilder.AddMenuEntry(
			EnumCreationType->GetDisplayNameTextByIndex(Index),
			EnumCreationType->GetToolTipTextByIndex(Index),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([this, Type]()
				{
					uint8 Value = static_cast<uint8>(Type);
					CreationTypeHandle->SetValue(Value);
				})
			)
		);
	}

	return MenuBuilder.MakeWidget();
}

FText FBlueprintViewModelContextDetailCustomization::GetCreationTypeValue() const
{
	uint8 Value = 0;
	if (CreationTypeHandle->GetValue(Value) == FPropertyAccess::Result::Success)
	{
		return StaticEnum<EMVVMBlueprintViewModelContextCreationType>()->GetDisplayNameTextByValue((int64)Value);
	}
	return FText::GetEmpty();
}

FText FBlueprintViewModelContextDetailCustomization::GetExecutionTypeValueToolTip() const
{
	uint8 Value = 0;
	if (CreationTypeHandle->GetValue(Value) == FPropertyAccess::Result::Success)
	{
		UEnum* EnumCreationType = StaticEnum<EMVVMBlueprintViewModelContextCreationType>();
		return EnumCreationType->GetToolTipTextByIndex(EnumCreationType->GetIndexByValue((int64)Value));
	}
	return FText::GetEmpty();
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
