// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterDetailsCustomization.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorModule.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "FNiagaraEmitterDetails"

TSharedRef<IDetailCustomization> FNiagaraEmitterDetails::MakeInstance(UNiagaraSystem* System)
{
	return MakeShareable(new FNiagaraEmitterDetails(System));
}

PRAGMA_DISABLE_OPTIMIZATION
void FNiagaraEmitterDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TSharedPtr<IPropertyHandle> EventHandlersPropertyHandle = DetailBuilder.GetProperty(UNiagaraEmitter::PrivateMemberNames::EventHandlerScriptProps);
	EventHandlersPropertyHandle->MarkHiddenByCustomization();

 	if (System && System->bFixedBounds)
 	{
 		static const FName EmitterCategoryName = TEXT("Emitter");
 		IDetailCategoryBuilder* SourceCategory = &DetailBuilder.EditCategory(EmitterCategoryName);
 		{
 			TArray<TSharedRef<IPropertyHandle>> Properties;
 			SourceCategory->GetDefaultProperties(Properties, true, true);
 			
 			for (TSharedPtr<IPropertyHandle> Property : Properties)
 			{
 				FProperty* PropertyPtr = Property->GetProperty();
 				if (PropertyPtr->GetName()== "FixedBounds")
 				{
					SourceCategory->AddCustomRow(Property->GetPropertyDisplayName())
 					.NameContent()
 					[
 						SNew(STextBlock)
						.Text(Property->GetPropertyDisplayName())
						.Font(DetailBuilder.GetDetailFont())
 					]
 					.ValueContent()
 					[
 						SNew(STextBlock)
						.Text(LOCTEXT("FixedBoundsOverridenBySystem", "Fixed bounds cannot be set here while overridden by system FixedBounds."))
						.AutoWrapText(true)
						.Font(DetailBuilder.GetDetailFontItalic())
 					];

 					DetailBuilder.HideProperty(Property);
 				}
 				else
 				{
 					SourceCategory->AddProperty(Property);
 				}
 			}
 		}
 	}
}

#undef LOCTEXT_NAMESPACE

