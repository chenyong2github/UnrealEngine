// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyAccessChainCustomization.h"

#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "GraphEditorSettings.h"
#include "SClassViewer.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyAccessEditor.h"
#include "ScopedTransaction.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "PropertyAccessChainCustomization"

namespace UE::ChooserEditor
{
	
// Wrapper widget for Property access widget, which can update when the target Class changes
class SPropertyAccessChainWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPropertyAccessChainWidget)
	{}

	SLATE_ARGUMENT(IHasContextClass*, ContextClassOwner)
	SLATE_ARGUMENT(FString, TypeFilter);
	SLATE_ARGUMENT(FString, BindingColor);
            
	SLATE_END_ARGS()

	TSharedRef<SWidget> CreatePropertyAccessWidget()
	{
		FPropertyBindingWidgetArgs Args;
		Args.bAllowPropertyBindings = true;

		UClass* ContextClass = UObject::StaticClass();
		if (ContextClassOwner)
		{
			ContextClass = ContextClassOwner->GetContextClass();
		}
		
		Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([this](FProperty* Property)
		{
			if (TypeFilter == "" || Property == nullptr)
			{
				return true;
			}
			else if (TypeFilter == "enum")
			{
				if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
				{
					return true;
				}
				else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
				{
					return ByteProperty->Enum != nullptr;
				}
				return false;
			}
			
			return Property->GetCPPType() == TypeFilter;
		});
		
		Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
		{
			return true;
		});


		FLinearColor BindingColorValue = FLinearColor::Gray;
		if (BindingColor != "")
		{
			const UGraphEditorSettings* GraphEditorSettings = GetDefault<UGraphEditorSettings>();
			if (const FStructProperty* ColorProperty = FindFProperty<FStructProperty>(GraphEditorSettings->GetClass(), FName(BindingColor)))
			{
				BindingColorValue = *ColorProperty->ContainerPtrToValuePtr<FLinearColor>(GraphEditorSettings);
			}
		}

		Args.CurrentBindingColor = MakeAttributeLambda([BindingColorValue]() {
			return BindingColorValue;
		});

		Args.OnCanAcceptPropertyOrChildren = FOnCanBindProperty::CreateLambda([](FProperty* InProperty)
			{
				// Make only blueprint visible properties visible for binding.
				return InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible);
			});	

		Args.OnAddBinding = FOnAddBinding::CreateLambda([this](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
			{
				TArray<UObject*> OuterObjects;
				PropertyHandle->GetOuterObjects(OuterObjects);
				const FScopedTransaction Transaction(NSLOCTEXT("ChooserPropertyBinding", "Change Property Binding", "Change Property Binding"));
			
				for (uint32 i=0; i<PropertyHandle->GetNumOuterObjects(); i++)
				{
					void* ValuePtr = nullptr;
					PropertyHandle->GetValueData(ValuePtr);// todo get per object value data?
					FChooserPropertyBinding* PropertyValue = reinterpret_cast<FChooserPropertyBinding*>(ValuePtr);
				
					if (PropertyValue != nullptr)
					{
						PropertyHandle->NotifyPreChange();
						
						OuterObjects[i]->Modify(true);
						Chooser::CopyPropertyChain(InBindingChain, PropertyValue->PropertyBindingChain);

						if (TypeFilter == "enum")
						{
							FField* Property = InBindingChain.Last().Field.ToField();
							FChooserEnumPropertyBinding* EnumPropertyValue = static_cast<FChooserEnumPropertyBinding*>(PropertyValue);
							
							if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
							{
								EnumPropertyValue->Enum = EnumProperty->GetEnum();
							}
							else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
							{
								EnumPropertyValue->Enum = ByteProperty->Enum;
							}
						}

						PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
					}
				}
			});

		Args.CurrentBindingToolTipText = MakeAttributeLambda([this]()
				{
					const FText Bind = NSLOCTEXT("ContextPropertyWidget", "Bind", "Bind");
					FText CurrentValue = Bind;
					
					void* ValuePtr = nullptr;
					PropertyHandle->GetValueData(ValuePtr);// todo get per object value data?
					FChooserPropertyBinding* PropertyValue = reinterpret_cast<FChooserPropertyBinding*>(ValuePtr);

					if (PropertyValue != nullptr && PropertyValue->PropertyBindingChain.Num()>0)
					{
						TArray<FText> BindingChainText;
						BindingChainText.Reserve(PropertyValue->PropertyBindingChain.Num());
					 
						for (const FName& Name : PropertyValue->PropertyBindingChain)
						{
							BindingChainText.Add(FText::FromName(Name));
						}
						
						CurrentValue = FText::Join(NSLOCTEXT("ContextPropertyWidget", "PropertyPathSeparator","."), BindingChainText);
					}
		
					return CurrentValue;	
				});
		
		Args.CurrentBindingText = MakeAttributeLambda([this]()
				{
					const FText Bind = NSLOCTEXT("ContextPropertyWidget", "Bind", "Bind");
					FText CurrentValue = Bind;
			
					void* ValuePtr = nullptr;
					PropertyHandle->GetValueData(ValuePtr);// todo get per object value data?
					FChooserPropertyBinding* PropertyValue = reinterpret_cast<FChooserPropertyBinding*>(ValuePtr);
		
					int BindingChainLength = PropertyValue->PropertyBindingChain.Num();
					if (BindingChainLength > 0)
					{
						if (BindingChainLength == 1)
						{
							// single property, just use the property name
							CurrentValue = FText::FromName(PropertyValue->PropertyBindingChain.Last());
						}
						else
						{
							// for longer chains always show the last struct/object name, and the final property name (full path in tooltip)
							CurrentValue = FText::Join(NSLOCTEXT("ContextPropertyWidget", "PropertyPathSeparator","."),
								TArray<FText>({
									FText::FromName(PropertyValue->PropertyBindingChain[BindingChainLength-2]),
									FText::FromName(PropertyValue->PropertyBindingChain[BindingChainLength-1])
								}));
						}
					}
		
					return CurrentValue;
				});

		Args.CurrentBindingImage = MakeAttributeLambda([]() -> const FSlateBrush*
			{
				static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
				return FAppStyle::GetBrush(PropertyIcon);
			});

		const IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
		FBindingContextStruct StructInfo;
		StructInfo.Struct = ContextClass;
		return  PropertyAccessEditor.MakePropertyBindingWidget({StructInfo}, Args);
	}

	void UpdateWidget()
	{
		ChildSlot[ CreatePropertyAccessWidget() ];
	}

	void ContextClassChanged(UClass* NewContextClass)
	{
		UpdateWidget();
	}

	void Construct( const FArguments& InArgs, TSharedRef<IPropertyHandle> InPropertyHandle)
	{
		PropertyHandle = InPropertyHandle;
		TypeFilter = InArgs._TypeFilter;
		BindingColor = InArgs._BindingColor;
		ContextClassOwner = InArgs._ContextClassOwner;
		UpdateWidget();

		if (ContextClassOwner)
		{
			ContextClassOwner->OnContextClassChanged.AddSP(this, &SPropertyAccessChainWidget::ContextClassChanged);
		}
	}

	~SPropertyAccessChainWidget()
	{
	}

private:
	FString TypeFilter;
	FString BindingColor;
	FDelegateHandle ContextClassChangedHandle();
	TSharedPtr<IPropertyHandle> PropertyHandle;
	IHasContextClass* ContextClassOwner = nullptr;
};

void FPropertyAccessChainCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FProperty* Property = PropertyHandle->GetProperty();

	UClass* ContextClass = nullptr;
	static FName TypeMetaData = "BindingType";
	static FName ColorMetaData = "BindingColor";

	FString TypeFilter = PropertyHandle->GetMetaData(TypeMetaData);
	FString BindingColor = PropertyHandle->GetMetaData(ColorMetaData);

	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);
	UObject* OuterObject = OuterObjects[0];
	
	while (OuterObject && !OuterObject->Implements<UHasContextClass>())
	{
		OuterObject = OuterObject->GetOuter();
	}
	
	IHasContextClass* HasContext = nullptr;
	if (OuterObject)
	{
		HasContext = static_cast<IHasContextClass*>(OuterObject->GetInterfaceAddress(UHasContextClass::StaticClass()));
	}

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SPropertyAccessChainWidget, PropertyHandle).ContextClassOwner(HasContext).TypeFilter(TypeFilter).BindingColor(BindingColor)
	];
}
	
void FPropertyAccessChainCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

}

#undef LOCTEXT_NAMESPACE