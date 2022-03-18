// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveExpressionDetailsCustomization.h"
#include "CurveExpressionEditorStyle.h"
#include "K2Node_MakeCurveExpressionMap.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

namespace UE::CurveExpressionEditor
{

class FModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		auto RegisterPropertyCustomization = [&](FName InStructName, auto InCustomizationFactory)
		{
			PropertyModule.RegisterCustomPropertyTypeLayout(
				InStructName, 
				FOnGetPropertyTypeCustomizationInstance::CreateStatic(InCustomizationFactory)
				);
			CustomizedProperties.Add(InStructName);
		};
		
		FCurveExpressionEditorStyle::Register();
		
		RegisterPropertyCustomization(
			FCurveExpressionList::StaticStruct()->GetFName(),
			&FCurveExpressionListCustomization::MakeInstance
			);
	}
	virtual void ShutdownModule() override
	{
		if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			for (const FName& PropertyName: CustomizedProperties)
			{
				PropertyModule->UnregisterCustomPropertyTypeLayout(PropertyName);
			}
		}
		
		FCurveExpressionEditorStyle::Unregister();
	}

private:
	TArray<FName> CustomizedProperties;
};

}

IMPLEMENT_MODULE(UE::CurveExpressionEditor::FModule, CurveExpressionEditor)
