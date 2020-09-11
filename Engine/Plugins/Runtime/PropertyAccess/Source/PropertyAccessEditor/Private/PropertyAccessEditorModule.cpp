// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IPropertyAccessEditor.h"
#include "Modules/ModuleManager.h"
#include "SPropertyBinding.h"
#include "EdGraphUtilities.h"
#include "PropertyAccessEditor.h"
#include "Modules/ModuleInterface.h"
#include "Features/IModularFeatures.h"
#include "IAnimBlueprintCompilerHandlerCollection.h"
#include "AnimBlueprintCompilerHandler_PropertyAccess.h"

class FPropertyAccessEditorModule : public IPropertyAccessEditor, public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature("PropertyAccessEditor", this);

		// Register node compilation handlers
		IAnimBlueprintCompilerHandlerCollection::RegisterHandler("PropertyAccessCompilerHandler", [](IAnimBlueprintCompilerCreationContext& InCreationContext)
		{
			return MakeUnique<FAnimBlueprintCompilerHandler_PropertyAccess>(InCreationContext);
		});
	}

	virtual void ShutdownModule() override
	{
		// Register node compilation handlers
		IAnimBlueprintCompilerHandlerCollection::UnregisterHandler("PropertyAccessCompilerHandler");

		IModularFeatures::Get().UnregisterModularFeature("PropertyAccessEditor", this);
	}

	// IPropertyAccessEditor interface
	virtual TSharedRef<SWidget> MakePropertyBindingWidget(UBlueprint* InBlueprint, const FPropertyBindingWidgetArgs& InArgs) const override
	{
		return SNew(SPropertyBinding, InBlueprint)
			.Args(InArgs);
	}

	virtual EPropertyAccessResolveResult ResolveLeafProperty(const UStruct* InStruct, TArrayView<FString> InPath, FProperty*& OutProperty, int32& OutArrayIndex) const override
	{
		return PropertyAccess::ResolveLeafProperty(InStruct, InPath, OutProperty, OutArrayIndex);
	}

	virtual EPropertyAccessCompatibility GetPropertyCompatibility(const FProperty* InPropertyA, const FProperty* InPropertyB) const override
	{
		return PropertyAccess::GetPropertyCompatibility(InPropertyA, InPropertyB);
	}

	virtual EPropertyAccessCompatibility GetPinTypeCompatibility(const FEdGraphPinType& InPinTypeA, const FEdGraphPinType& InPinTypeB) const override
	{
		return PropertyAccess::GetPinTypeCompatibility(InPinTypeA, InPinTypeB);
	}

	virtual void MakeStringPath(const TArray<FBindingChainElement>& InBindingChain, TArray<FString>& OutStringPath) const override
	{
		PropertyAccess::MakeStringPath(InBindingChain, OutStringPath);
	}
};

IMPLEMENT_MODULE(FPropertyAccessEditorModule, PropertyAccessEditor)