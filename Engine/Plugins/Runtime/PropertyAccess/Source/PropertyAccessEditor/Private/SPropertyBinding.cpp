// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPropertyBinding.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"

#if WITH_EDITOR
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#endif // WITH_EDITOR

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"

#include "DetailLayoutBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Components/WidgetComponent.h"
#include "Binding/PropertyBinding.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Algo/Transform.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "PropertyBinding"

/////////////////////////////////////////////////////
// SPropertyBinding

void SPropertyBinding::Construct(const FArguments& InArgs, UBlueprint* InBlueprint)
{
	Blueprint = InBlueprint;

	Args = InArgs._Args;
	PropertyName = Args.Property != nullptr ? Args.Property->GetFName() : NAME_None;

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SBox)
			.MaxDesiredWidth(200.0f)
			[
				SNew(SComboButton)
				.ToolTipText(this, &SPropertyBinding::GetCurrentBindingText)
				.OnGetMenuContent(this, &SPropertyBinding::OnGenerateDelegateMenu)
				.ContentPadding(1)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					.Clipping(EWidgetClipping::ClipToBounds)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.HeightOverride(16.0f)
						[
							SNew(SImage)
							.Image(this, &SPropertyBinding::GetCurrentBindingImage)
							.ColorAndOpacity(this, &SPropertyBinding::GetCurrentBindingColor)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(this, &SPropertyBinding::GetCurrentBindingText)
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.Visibility(this, &SPropertyBinding::GetGotoBindingVisibility)
			.OnClicked(this, &SPropertyBinding::HandleGotoBindingClicked)
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("GotoFunction", "Goto Function"))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("PropertyWindow.Button_Browse"))
			]
		]
	];
}

bool SPropertyBinding::IsClassBlackListed(UClass* InClass) const
{
	if(Args.OnCanBindToClass.IsBound() && Args.OnCanBindToClass.Execute(InClass))
	{
		return false;
	}

	return true;
}

bool SPropertyBinding::IsFieldFromBlackListedClass(FFieldVariant Field) const
{
	return IsClassBlackListed(Field.GetOwnerClass());
}

template <typename Predicate>
void SPropertyBinding::ForEachBindableFunction(UClass* FromClass, Predicate Pred) const
{
	if(Args.OnCanBindFunction.IsBound())
	{
		// Walk up class hierarchy for native functions and properties
		for ( TFieldIterator<UFunction> FuncIt(FromClass, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt )
		{
			UFunction* Function = *FuncIt;

			// Stop processing functions after reaching a base class that it doesn't make sense to go beyond.
			if ( IsFieldFromBlackListedClass(Function) )
			{
				break;
			}

			// Only allow binding pure functions if we're limited to pure function bindings.
			if ( Args.bGeneratePureBindings && !Function->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure) )
			{
				continue;
			}

			// Only bind to functions that are callable from blueprints
			if ( !UEdGraphSchema_K2::CanUserKismetCallFunction(Function) )
			{
				continue;
			}

			bool bValidObjectFunction = false;
			if(Args.bAllowUObjectFunctions)
			{
				FObjectPropertyBase* ObjectPropertyBase = CastField<FObjectPropertyBase>(Function->GetReturnProperty());
				if(ObjectPropertyBase != nullptr && Function->NumParms == 1)
				{
					bValidObjectFunction = true;
				}
			}

			if(bValidObjectFunction || Args.OnCanBindFunction.Execute(Function))
			{
				Pred(FFunctionInfo(Function));
			}
		}
	}
}

template <typename Predicate>
void SPropertyBinding::ForEachBindableProperty(UStruct* InStruct, Predicate Pred) const
{
	if(Args.OnCanBindProperty.IsBound())
	{
		UBlueprintGeneratedClass* SkeletonClass = Cast<UBlueprintGeneratedClass>(Blueprint->SkeletonGeneratedClass);

		for ( TFieldIterator<FProperty> PropIt(InStruct, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt )
		{
			FProperty* Property = *PropIt;

			// Stop processing properties after reaching the stopped base class
			if ( IsFieldFromBlackListedClass(Property) )
			{
				break;
			}

			if ( !UEdGraphSchema_K2::CanUserKismetAccessVariable(Property, SkeletonClass, UEdGraphSchema_K2::CannotBeDelegate) )
			{
				continue;
			}

			// Also ignore advanced properties
			if ( Property->HasAnyPropertyFlags(CPF_AdvancedDisplay | CPF_EditorOnly) )
			{
				continue;
			}

			Pred(Property);
		}
	}
}

TSharedRef<SWidget> SPropertyBinding::OnGenerateDelegateMenu()
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr, Args.MenuExtender);

	MenuBuilder.BeginSection("BindingActions", LOCTEXT("Bindings", "Bindings"));

	if ( CanRemoveBinding() )
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("RemoveBinding", "Remove Binding"),
			LOCTEXT("RemoveBindingToolTip", "Removes the current binding"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Cross"),
			FUIAction(FExecuteAction::CreateSP(this, &SPropertyBinding::HandleRemoveBinding))
			);
	}

	if(Args.bAllowNewBindings && Args.BindableSignature != nullptr)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateBinding", "Create Binding"),
			LOCTEXT("CreateBindingToolTip", "Creates a new function on the widget blueprint that will return the binding data for this property."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Plus"),
			FUIAction(FExecuteAction::CreateSP(this, &SPropertyBinding::HandleCreateAndAddBinding))
			);
	}

	MenuBuilder.EndSection(); //CreateBinding

	// Properties
	{
		// Get the current skeleton class, think header for the blueprint.
		UBlueprintGeneratedClass* SkeletonClass = Cast<UBlueprintGeneratedClass>(Blueprint->SkeletonGeneratedClass);

		TArray<TSharedPtr<FBindingChainElement>> BindingChain;
		FillPropertyMenu(MenuBuilder, SkeletonClass, BindingChain);
	}

	FDisplayMetrics DisplayMetrics;
	FSlateApplication::Get().GetCachedDisplayMetrics(DisplayMetrics);

	return
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.MaxHeight(DisplayMetrics.PrimaryDisplayHeight * 0.5)
		[
			MenuBuilder.MakeWidget()
		];
}

void SPropertyBinding::FillPropertyMenu(FMenuBuilder& MenuBuilder, UStruct* InOwnerStruct, TArray<TSharedPtr<FBindingChainElement>> InBindingChain)
{
	auto MakeArrayElementPropertyWidget = [this](FProperty* InProperty, const TArray<TSharedPtr<FBindingChainElement>>& InNewBindingChain)
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		FEdGraphPinType PinType;
		Schema->ConvertPropertyToPinType(InProperty, PinType);
		
		TSharedPtr<FBindingChainElement> LeafElement = InNewBindingChain.Last();

		return SNew(SHorizontalBox)
			.ToolTipText(InProperty->GetToolTipText())
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(18.0f, 0.0f))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(1.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FBlueprintEditorUtils::GetIconFromPin(PinType, true))
				.ColorAndOpacity(Schema->GetPinTypeColor(PinType))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromName(InProperty->GetFName()))
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.Padding(2.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(24.0f)
				[
					SNew(SNumericEntryBox<int32>)
					.ToolTipText(LOCTEXT("ArrayIndex", "Array Index"))
					.Value_Lambda([LeafElement](){ return LeafElement->ArrayIndex == INDEX_NONE ? TOptional<int32>() : TOptional<int32>(LeafElement->ArrayIndex); })
					.OnValueCommitted(this, &SPropertyBinding::HandleSetBindingArrayIndex, InProperty, InNewBindingChain)
				]
			];
	};

	auto MakeArrayElementEntry = [this, &MenuBuilder, &MakeArrayElementPropertyWidget](FProperty* InProperty, const TArray<TSharedPtr<FBindingChainElement>>& InNewBindingChain)
	{
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateSP(this, &SPropertyBinding::HandleAddBinding, InNewBindingChain)),
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeArrayElementPropertyWidget(InProperty, InNewBindingChain)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(15.0f, 0.0f))
			]);
	};

	auto MakePropertyWidget = [this](FProperty* InProperty)
	{
		static FName PropertyIcon(TEXT("Kismet.VariableList.TypeIcon"));

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		FEdGraphPinType PinType;
		Schema->ConvertPropertyToPinType(InProperty, PinType);

		return SNew(SHorizontalBox)
			.ToolTipText(InProperty->GetToolTipText())
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(18.0f, 0.0f))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(1.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FBlueprintEditorUtils::GetIconFromPin(PinType, true))
				.ColorAndOpacity(Schema->GetPinTypeColor(PinType))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromName(InProperty->GetFName()))
			];
	};

	auto MakePropertyEntry = [this, &MenuBuilder, &MakePropertyWidget](FProperty* InProperty, const TArray<TSharedPtr<FBindingChainElement>>& InNewBindingChain)
	{
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateSP(this, &SPropertyBinding::HandleAddBinding, InNewBindingChain)),
			MakePropertyWidget(InProperty));
	};

	auto MakeFunctionWidget = [this](const FFunctionInfo& Info)
	{
		static FName FunctionIcon(TEXT("GraphEditor.Function_16x"));

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

		FEdGraphPinType PinType;
		if(FProperty* ReturnProperty = Info.Function->GetReturnProperty())
		{
			Schema->ConvertPropertyToPinType(ReturnProperty, PinType);
		}

		return SNew(SHorizontalBox)
			.ToolTipText(FText::FromString(Info.Tooltip))
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(18.0f, 0.0f))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(1.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::Get().GetBrush(FunctionIcon))
				.ColorAndOpacity(Schema->GetPinTypeColor(PinType))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Info.DisplayName)
			];
	};

	//---------------------------------------
	// Function Bindings

	if ( UClass* OwnerClass = Cast<UClass>(InOwnerStruct) )
	{
		static FName FunctionIcon(TEXT("GraphEditor.Function_16x"));

		MenuBuilder.BeginSection("Functions", LOCTEXT("Functions", "Functions"));
		{
			ForEachBindableFunction(OwnerClass, [this, &InBindingChain, &MenuBuilder, &MakeFunctionWidget] (const FFunctionInfo& Info) 
			{
				TArray<TSharedPtr<FBindingChainElement>> NewBindingChain(InBindingChain);
				NewBindingChain.Emplace(MakeShared<FBindingChainElement>(Info.Function));

				FProperty* ReturnProperty = Info.Function->GetReturnProperty();
				// We can get here if we accept non-leaf UObject functions, so if so we need to check the return value for compatibility
				if(!Args.bAllowUObjectFunctions || Args.OnCanBindProperty.Execute(ReturnProperty))
				{
					MenuBuilder.AddMenuEntry(
						FUIAction(FExecuteAction::CreateSP(this, &SPropertyBinding::HandleAddBinding, NewBindingChain)),
						MakeFunctionWidget(Info));
				}

				// Only show bindable subobjects and variables if we're generating pure bindings.
				if(Args.bGeneratePureBindings)
				{
					if(FObjectPropertyBase* ObjectPropertyBase = CastField<FObjectPropertyBase>(ReturnProperty))
					{
						MenuBuilder.AddSubMenu(
							MakeFunctionWidget(Info),
							FNewMenuDelegate::CreateSP(this, &SPropertyBinding::FillPropertyMenu, static_cast<UStruct*>(ObjectPropertyBase->PropertyClass), NewBindingChain));
					}
				}
			});
		}
		MenuBuilder.EndSection(); //Functions
	}

	//---------------------------------------
	// Property Bindings

	// Get the current skeleton class, think header for the blueprint.
	UBlueprintGeneratedClass* SkeletonClass = Cast<UBlueprintGeneratedClass>(Blueprint->SkeletonGeneratedClass);

	// Only show bindable subobjects and variables if we're generating pure bindings.
	if ( Args.bGeneratePureBindings )
	{
		FProperty* BindingProperty = nullptr;
		if(Args.BindableSignature != nullptr)
		{
			BindingProperty = Args.BindableSignature->GetReturnProperty();
		}
		else
		{
			BindingProperty = Args.Property;
		}

		// Find the binder that can handle the delegate return type, don't bother allowing people 
		// to look for bindings that we don't support
		if ( Args.OnCanBindProperty.IsBound() && Args.OnCanBindProperty.Execute(BindingProperty) )
		{
			MenuBuilder.BeginSection("Properties", LOCTEXT("Properties", "Properties"));
			{
				ForEachBindableProperty(InOwnerStruct, [this, &InBindingChain, &MenuBuilder, &MakeArrayElementPropertyWidget, &MakePropertyWidget, &MakePropertyEntry, &MakeArrayElementEntry] (FProperty* Property) 
				{
					TArray<TSharedPtr<FBindingChainElement>> NewBindingChain(InBindingChain);
					NewBindingChain.Emplace(MakeShared<FBindingChainElement>(Property));

					if(Args.OnCanBindProperty.Execute(Property))
					{
						MakePropertyEntry(Property, NewBindingChain);
					}

					FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);

					if(Args.bAllowArrayElementBindings && ArrayProperty != nullptr && Args.OnCanBindProperty.Execute(ArrayProperty->Inner))
					{
						TArray<TSharedPtr<FBindingChainElement>> NewArrayElementBindingChain(InBindingChain);
						NewArrayElementBindingChain.Emplace(MakeShared<FBindingChainElement>(Property));
						NewArrayElementBindingChain.Last()->ArrayIndex = 0;

						MakeArrayElementEntry(ArrayProperty->Inner, NewArrayElementBindingChain);
					}

					FProperty* InnerProperty = Property;
					if(Args.bAllowArrayElementBindings && ArrayProperty != nullptr)
					{
						InnerProperty = ArrayProperty->Inner;
					}

					FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InnerProperty);
					FStructProperty* StructProperty = CastField<FStructProperty>(InnerProperty);

					UStruct* Struct = nullptr;
					UClass* Class = nullptr;

					if ( ObjectProperty )
					{
						Struct = Class = ObjectProperty->PropertyClass;
					}
					else if ( StructProperty )
					{
						Struct = StructProperty->Struct;
					}

					if ( Struct )
					{
						if ( Class )
						{
							// Ignore any subobject properties that are not bindable.
							// Also ignore any class that is explicitly on the black list.
							if ( IsClassBlackListed(Class) || (Args.OnCanBindToSubObjectClass.IsBound() && Args.OnCanBindToSubObjectClass.Execute(Class)))
							{
								return;
							}
						}

						if(Args.bAllowArrayElementBindings && ArrayProperty != nullptr)
						{
							TArray<TSharedPtr<FBindingChainElement>> NewArrayElementBindingChain(InBindingChain);
							NewArrayElementBindingChain.Emplace(MakeShared<FBindingChainElement>(Property));
							NewArrayElementBindingChain.Last()->ArrayIndex = 0;

							MenuBuilder.AddSubMenu(
								MakeArrayElementPropertyWidget(ArrayProperty->Inner, NewArrayElementBindingChain),
								FNewMenuDelegate::CreateSP(this, &SPropertyBinding::FillPropertyMenu, Struct, NewArrayElementBindingChain)
								);
						}
						else
						{
							MenuBuilder.AddSubMenu(
								MakePropertyWidget(Property),
								FNewMenuDelegate::CreateSP(this, &SPropertyBinding::FillPropertyMenu, Struct, NewBindingChain));
						}
					}
				});
			}
			MenuBuilder.EndSection(); //Properties
		}
	}

	// Add 'none' entry only if we just have the search block in the builder
	if ( MenuBuilder.GetMultiBox()->GetBlocks().Num() == 1 )
	{
		MenuBuilder.BeginSection("None", InOwnerStruct->GetDisplayNameText());
		MenuBuilder.AddWidget(SNew(STextBlock).Text(LOCTEXT("None", "None")), FText::GetEmpty());
		MenuBuilder.EndSection(); //None
	}
}

const FSlateBrush* SPropertyBinding::GetCurrentBindingImage() const
{
	if(Args.CurrentBindingImage.IsSet())
	{
		return Args.CurrentBindingImage.Get();
	}

	return nullptr;
}

FText SPropertyBinding::GetCurrentBindingText() const
{
	if(Args.CurrentBindingText.IsSet())
	{
		return Args.CurrentBindingText.Get();
	}

	return LOCTEXT("Bind", "Bind");
}

FSlateColor SPropertyBinding::GetCurrentBindingColor() const
{
	if(Args.CurrentBindingColor.IsSet())
	{
		return Args.CurrentBindingColor.Get();
	}

	return FLinearColor(0.25f, 0.25f, 0.25f);
}

bool SPropertyBinding::CanRemoveBinding()
{
	if(Args.OnCanRemoveBinding.IsBound())
	{
		return Args.OnCanRemoveBinding.Execute(PropertyName);
	}

	return false;
}

void SPropertyBinding::HandleRemoveBinding()
{
	if(Args.OnRemoveBinding.IsBound())
	{
		const FScopedTransaction Transaction(LOCTEXT("UnbindDelegate", "Remove Binding"));

		Args.OnRemoveBinding.Execute(PropertyName);
	}
}

void SPropertyBinding::HandleAddBinding(TArray<TSharedPtr<FBindingChainElement>> InBindingChain)
{
	if(Args.OnAddBinding.IsBound())
	{
		const FScopedTransaction Transaction(LOCTEXT("BindDelegate", "Set Binding"));

		TArray<FBindingChainElement> BindingChain;
		Algo::Transform(InBindingChain, BindingChain, [](TSharedPtr<FBindingChainElement> InElement)
		{
			return *InElement.Get();
		});
		Args.OnAddBinding.Execute(PropertyName, BindingChain);
	}
}

void SPropertyBinding::HandleSetBindingArrayIndex(int32 InArrayIndex, ETextCommit::Type InCommitType, FProperty* InProperty, TArray<TSharedPtr<FBindingChainElement>> InBindingChain)
{
	InBindingChain.Last()->ArrayIndex = InArrayIndex;

	// If the user hit enter on a compatible property, assume they want to accept
	if(Args.OnCanBindProperty.Execute(InProperty) && InCommitType == ETextCommit::OnEnter)
	{
		HandleAddBinding(InBindingChain);
	}
}

void SPropertyBinding::HandleCreateAndAddBinding()
{
	const FScopedTransaction Transaction(LOCTEXT("CreateDelegate", "Create Binding"));

	Blueprint->Modify();

	FString Pre = Args.bGeneratePureBindings ? FString(TEXT("Get")) : FString(TEXT("On"));

	FString WidgetName;
	if(Args.OnGenerateBindingName.IsBound())
	{
		WidgetName = Args.OnGenerateBindingName.Execute();
	}

	FString Post = PropertyName != NAME_None ? PropertyName.ToString() : TEXT("");
	Post.RemoveFromStart(TEXT("On"));
	Post.RemoveFromEnd(TEXT("Event"));

	// Create the function graph.
	FString FunctionName = Pre + WidgetName + Post;
	UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, 
		FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());
	
	const bool bUserCreated = true;
	FBlueprintEditorUtils::AddFunctionGraph(Blueprint, FunctionGraph, bUserCreated, Args.BindableSignature);

	UFunction* Function = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName());
	check(Function);

	// Add the binding to the blueprint
	TArray<TSharedPtr<FBindingChainElement>> BindingPath;
	BindingPath.Emplace(MakeShared<FBindingChainElement>(Function));

	HandleAddBinding(BindingPath);

	// Only mark bindings as pure that need to be.
	if ( Args.bGeneratePureBindings )
	{
		const UEdGraphSchema_K2* Schema_K2 = Cast<UEdGraphSchema_K2>(FunctionGraph->GetSchema());
		Schema_K2->AddExtraFunctionFlags(FunctionGraph, FUNC_BlueprintPure);
	}

	HandleGotoBindingClicked();
}

EVisibility SPropertyBinding::GetGotoBindingVisibility() const
{
	if(Args.OnCanGotoBinding.IsBound())
	{
		if(Args.OnCanGotoBinding.Execute(PropertyName))
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FReply SPropertyBinding::HandleGotoBindingClicked()
{
	if(Args.OnGotoBinding.IsBound())
	{
		if(Args.OnGotoBinding.Execute(PropertyName))
		{
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
