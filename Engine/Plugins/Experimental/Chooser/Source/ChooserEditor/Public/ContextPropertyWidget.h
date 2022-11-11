// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ScopedTransaction.h"
#include "IPropertyAccessEditor.h"
#include "Features/IModularFeatures.h"
#include "Styling/AppStyle.h"

namespace UE::ChooserEditor
{
	// Filter class for things which implement a particular interface
	template <typename PropertyType>
    TSharedRef<SWidget> CreatePropertyWidget(UObject* Object, UClass* ContextClass, const FLinearColor& BindingColor)
    {
    	TWeakObjectPtr<PropertyType> ContextProperty = Cast<PropertyType>(Object);
    	
    	FPropertyBindingWidgetArgs Args;
    	Args.bAllowPropertyBindings = true;
    	
    	Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([](FProperty* Property)
    	{
    		return Property == nullptr || PropertyType::CanBind(*Property);
    	});
    	Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([ContextClass ](UClass* InClass)
    	{
    		return true;
    	});
    	
    	Args.CurrentBindingColor = MakeAttributeLambda([ContextProperty, BindingColor]() {
    		 if (ContextProperty.IsValid() && ContextProperty->PropertyBindingChain.Num() > 0)
    		 {
    		 	return BindingColor;
    		 }
    		return FLinearColor::Gray;
    	});
    
    	Args.OnCanAcceptPropertyOrChildren = FOnCanBindProperty::CreateLambda([](FProperty* InProperty)
    		{
    			// Make only editor visible properties visible for binding.
    			return InProperty->HasAnyPropertyFlags(CPF_Edit);
    		});	
    
    	Args.OnAddBinding = FOnAddBinding::CreateLambda([ContextProperty](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
    		{
    			if (ContextProperty.IsValid())
    			{
    				const FScopedTransaction Transaction(NSLOCTEXT("ContextPropertyWidget", "Change Property Binding", "Change Property Binding"));
    				ContextProperty->Modify(true);
    				ContextProperty->SetBinding(InBindingChain);
    			}
    		});
    
    	Args.CurrentBindingToolTipText = MakeAttributeLambda([ContextProperty]()
    			{
    				const FText Bind = NSLOCTEXT("ContextPropertyWidget", "Bind", "Bind");
    				FText CurrentValue = Bind;
    
    				if (ContextProperty.IsValid() && ContextProperty->PropertyBindingChain.Num()>0)
    				{
    					TArray<FText> BindingChainText;
                     	BindingChainText.Reserve(ContextProperty->PropertyBindingChain.Num());
                     
                     	for (const FName& Name : ContextProperty->PropertyBindingChain)
                     	{
                     		BindingChainText.Add(FText::FromName(Name));
                     	}
    					
    					CurrentValue = FText::Join(NSLOCTEXT("ContextPropertyWidget", "PropertyPathSeparator","."), BindingChainText);
    				}
    
    				return CurrentValue;	
    			});
    	
    	Args.CurrentBindingText = MakeAttributeLambda([ContextProperty]()
    			{
    				const FText Bind = NSLOCTEXT("ContextPropertyWidget", "Bind", "Bind");
    				FText CurrentValue = Bind;
    
    				if (ContextProperty.IsValid())
    				{
    					int BindingChainLength = ContextProperty->PropertyBindingChain.Num();
    					if (BindingChainLength > 0)
    					{
    						if (BindingChainLength == 1)
    						{
    							// single property, just use the property name
    							CurrentValue = FText::FromName(ContextProperty->PropertyBindingChain.Last());
    						}
    						else
    						{
    							// for longer chains always show the last struct/object name, and the final property name (full path in tooltip)
    							CurrentValue = FText::Join(NSLOCTEXT("ContextPropertyWidget", "PropertyPathSeparator","."),
    								TArray<FText>({
    									FText::FromName(ContextProperty->PropertyBindingChain[BindingChainLength-2]),
    									FText::FromName(ContextProperty->PropertyBindingChain[BindingChainLength-1])
    								}));
    						}
    					}
    				}
    
    				return CurrentValue;
    			});
    
    	Args.CurrentBindingImage = MakeAttributeLambda([]() -> const FSlateBrush*
    		{
    			static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
    			return FAppStyle::GetBrush(PropertyIcon);
    		});
    
    	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
    	FBindingContextStruct StructInfo;
    	StructInfo.Struct = ContextClass;
    	return PropertyAccessEditor.MakePropertyBindingWidget({StructInfo}, Args);
    }
				
	template <typename PropertyType>
	void ConvertToText_ContextProperty(const UObject* Object, FText& OutText)
	{
		if (const PropertyType* ContextProperty = Cast<PropertyType>(Object))
		{
			if (ContextProperty->PropertyBindingChain.Num()>0)
			{
				OutText = FText::FromName(ContextProperty->PropertyBindingChain.Last());
			}
		}
	}

}