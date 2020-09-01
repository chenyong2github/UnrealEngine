// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPropertyAccessNode.h"
#include "SLevelOfDetailBranchNode.h"
#include "PropertyAccess.h"
#include "EditorStyleSet.h"
#include "EdGraphSchema_K2.h"
#include "IPropertyAccessEditor.h"
#include "Widgets/Layout/SSpacer.h"
#include "K2Node_PropertyAccess.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Features/IModularFeatures.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SPropertyAccessNode"

void SPropertyAccessNode::Construct(const FArguments& InArgs, UK2Node_PropertyAccess* InNode)
{
	GraphNode = InNode;
	UpdateGraphNode();
}

bool SPropertyAccessNode::CanBindProperty(FProperty* InProperty) const
{
	UK2Node_PropertyAccess* K2Node_PropertyAccess = CastChecked<UK2Node_PropertyAccess>(GraphNode);

	if(InProperty == nullptr)
	{
		return true;
	}

	if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		FEdGraphPinType ResolvedPinType = K2Node_PropertyAccess->GetResolvedPinType();
		FEdGraphPinType PropertyPinType;
		if(ResolvedPinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			// If a property is not already resolved, we allow any type...
			return true;
		}
		else if(Schema->ConvertPropertyToPinType(InProperty, PropertyPinType))
		{
			// ...unless we have a valid pin type already
			return PropertyAccessEditor.GetPinTypeCompatibility(PropertyPinType, ResolvedPinType) != EPropertyAccessCompatibility::Incompatible;
		}
		else
		{
			// Otherwise fall back on a resolved property
			const FProperty* ResolvedProperty = K2Node_PropertyAccess->GetResolvedProperty();
			if(ResolvedProperty != nullptr)
			{
				const FProperty* PropertyToUse = ResolvedProperty;
				if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ResolvedProperty))
				{
					if(K2Node_PropertyAccess->GetResolvedArrayIndex() != INDEX_NONE)
					{
						PropertyToUse = ArrayProperty->Inner;
					}
				}

				// Note: We support type promotion here
				return PropertyAccessEditor.GetPropertyCompatibility(InProperty, PropertyToUse) != EPropertyAccessCompatibility::Incompatible;
			}
		}
	}

	return false;
}

void SPropertyAccessNode::CreateBelowPinControls(TSharedPtr<SVerticalBox> InMainBox)
{
	UK2Node_PropertyAccess* K2Node_PropertyAccess = CastChecked<UK2Node_PropertyAccess>(GraphNode);

	FPropertyBindingWidgetArgs Args;

	Args.OnCanBindProperty = FOnCanBindProperty::CreateSP(this, &SPropertyAccessNode::CanBindProperty);

	Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda([this](UFunction* InFunction)
	{
		if(InFunction->NumParms != 1 || InFunction->GetReturnProperty() == nullptr || !InFunction->HasAnyFunctionFlags(FUNC_BlueprintPure))
		{
			return false;
		}

		// check the return property directly
		return CanBindProperty(InFunction->GetReturnProperty());
	});

	Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
	{
		return true;
	});

	Args.OnAddBinding = FOnAddBinding::CreateLambda([K2Node_PropertyAccess](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
	{
		if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

			TArray<FString> StringPath;
			PropertyAccessEditor.MakeStringPath(InBindingChain, StringPath);
			K2Node_PropertyAccess->SetPath(MoveTemp(StringPath));
		}
	});

	Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([K2Node_PropertyAccess](FName InPropertyName)
	{
		return K2Node_PropertyAccess->ClearPath();
	});

	Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([K2Node_PropertyAccess](FName InPropertyName)
	{
		return K2Node_PropertyAccess->GetPath().Num() > 0;
	});

	Args.CurrentBindingText = MakeAttributeLambda([K2Node_PropertyAccess]()
	{
		const FText& TextPath = K2Node_PropertyAccess->GetTextPath();
		return TextPath.IsEmpty() ? LOCTEXT("Bind", "Bind") : TextPath;
	});

	Args.CurrentBindingImage = MakeAttributeLambda([K2Node_PropertyAccess]()
	{
		if(const FProperty* Property = K2Node_PropertyAccess->GetResolvedProperty())
		{
			if(Cast<UFunction>(Property->GetOwnerUField()) != nullptr)
			{
				static FName FunctionIcon(TEXT("GraphEditor.Function_16x"));
				return FEditorStyle::GetBrush(FunctionIcon);
			}
			else
			{
				const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

				FEdGraphPinType PinType;
				Schema->ConvertPropertyToPinType(K2Node_PropertyAccess->GetResolvedProperty(), PinType);
				return FBlueprintEditorUtils::GetIconFromPin(PinType, true);
			}
		}

		static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
		return FEditorStyle::GetBrush(PropertyIcon);
	});

	Args.CurrentBindingColor = MakeAttributeLambda([K2Node_PropertyAccess]()
	{
		if(K2Node_PropertyAccess->GetResolvedProperty())
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

			FEdGraphPinType PinType;
			Schema->ConvertPropertyToPinType(K2Node_PropertyAccess->GetResolvedProperty(), PinType);

			return Schema->GetPinTypeColor(PinType);
		}
		else
		{
			return FLinearColor(0.5f, 0.5f, 0.5f);
		}
	});

	Args.bAllowArrayElementBindings = true;
	Args.bAllowNewBindings = false;
	Args.bAllowUObjectFunctions = true;

	TSharedPtr<SWidget> PropertyBindingWidget;

	if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
		PropertyBindingWidget = PropertyAccessEditor.MakePropertyBindingWidget(K2Node_PropertyAccess->GetBlueprint(), Args);
	}
	else
	{
		PropertyBindingWidget = SNullWidget::NullWidget;
	}

	InMainBox->AddSlot()
	.AutoHeight()
	.Padding(5.0f)
	[
		SNew(SLevelOfDetailBranchNode)
		.UseLowDetailSlot(this, &SPropertyAccessNode::UseLowDetailNodeTitles)
		.LowDetail()
		[
			SNew(SSpacer)
		]
		.HighDetail()
		[
			PropertyBindingWidget.ToSharedRef()
		]
	];
}

#undef LOCTEXT_NAMESPACE