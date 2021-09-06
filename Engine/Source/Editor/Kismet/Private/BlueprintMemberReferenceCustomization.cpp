// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintMemberReferenceCustomization.h"
#include "IPropertyAccessEditor.h"
#include "SMyBlueprint.h"
#include "Engine/MemberReference.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "DetailWidgetRow.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "BlueprintMemberReferenceCustomization"

void FBlueprintMemberReferenceDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	if(IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		UBlueprint* Blueprint = MyBlueprint.IsValid() ? MyBlueprint.Pin()->GetBlueprintObj() : nullptr;
		if(Blueprint == nullptr)
		{
			// Try to get the BP from a node that we are member of
			TArray<UObject*> OuterObjects;
			InStructPropertyHandle->GetOuterObjects(OuterObjects);
			if(OuterObjects.Num() > 0)
			{
				if(UK2Node* Node = Cast<UK2Node>(OuterObjects[0]))
				{
					Blueprint = Node->HasValidBlueprint() ? Node->GetBlueprint() : nullptr;
				}
			}
		}

		if(Blueprint == nullptr)
		{
			return;
		}
		
		const bool bFunctionReference = InStructPropertyHandle->HasMetaData("FunctionReference");
		const bool bPropertyReference = InStructPropertyHandle->HasMetaData("PropertyReference");
		if(bPropertyReference || !bFunctionReference)
		{
			// Only function references are supported right now
			return;
		}

		const bool bAllowFunctionLibraryReferences = InStructPropertyHandle->HasMetaData("AllowFunctionLibraries");
		
		// Try to find the prototype function
		const FString& PrototypeFunctionName = InStructPropertyHandle->GetMetaData("PrototypeFunction");
		UFunction* PrototypeFunction = PrototypeFunctionName.IsEmpty() ? nullptr : FindObject<UFunction>(nullptr, *PrototypeFunctionName);

		auto OnGenerateBindingName = []() -> FString
		{
			return TEXT("NewFunction");
		};
		
		auto OnCanBindProperty = [](FProperty* InProperty)
		{
			return true;
		};

		auto OnGoToBinding = [InStructPropertyHandle, Blueprint](FName InPropertyName)
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				UFunction* Function = MemberReference->ResolveMember<UFunction>(Blueprint->SkeletonGeneratedClass);
				if(Function)
				{
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
					
					if(IBlueprintEditor* BlueprintEditor = static_cast<IBlueprintEditor*>(GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, true)))
					{
						BlueprintEditor->JumpToHyperlink(Function, false);
						return true;
					}
				}
			}

			return false;
		};
		
		auto OnCanGotoBinding = [InStructPropertyHandle](FName InPropertyName)
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				return MemberReference->GetMemberName() != NAME_None;
			}
			
			return false;
		};
		
		auto OnCanBindFunction = [PrototypeFunction](UFunction* InFunction)
		{
			if(PrototypeFunction != nullptr)
			{
				return PrototypeFunction->IsSignatureCompatibleWith(InFunction)
					&& FBlueprintEditorUtils::HasFunctionBlueprintThreadSafeMetaData(PrototypeFunction) == FBlueprintEditorUtils::HasFunctionBlueprintThreadSafeMetaData(InFunction);
			}
			
			return false;
		};

		auto OnAddBinding = [InStructPropertyHandle, Blueprint](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				InStructPropertyHandle->NotifyPreChange();
				
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				UFunction* Function = InBindingChain[0].Field.Get<UFunction>();
				UClass* OwnerClass = Function ? Function->GetOwnerClass() : nullptr;
				bool bSelfContext = false;
				if(OwnerClass != nullptr)
				{
					bSelfContext = (Blueprint->GeneratedClass != nullptr && Blueprint->GeneratedClass->IsChildOf(OwnerClass)) ||
									(Blueprint->SkeletonGeneratedClass != nullptr && Blueprint->SkeletonGeneratedClass->IsChildOf(OwnerClass));
				}
				MemberReference->SetFromField<UFunction>(Function, bSelfContext);

				InStructPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}
		};

		auto OnRemoveBinding = [InStructPropertyHandle](FName InPropertyName)
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				InStructPropertyHandle->NotifyPreChange();
				
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				*MemberReference = FMemberReference();

				InStructPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			}
		};

		auto CanRemoveBinding = [InStructPropertyHandle](FName InPropertyName)
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				return MemberReference->GetMemberName() != NAME_None;
			}
			
			return false;
		};

		auto OnNewFunctionBindingCreated = [PrototypeFunction](UEdGraph* InFunctionGraph, UFunction* InFunction)
		{
			// Ensure newly created function is thread safe 
			if(PrototypeFunction && FBlueprintEditorUtils::HasFunctionBlueprintThreadSafeMetaData(PrototypeFunction))
			{
				TArray<UK2Node_FunctionEntry*> EntryNodes;
				InFunctionGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
				if(EntryNodes.Num() > 0)
				{
					EntryNodes[0]->MetaData.bThreadSafe = true;
				}
			}
		};

		auto CurrentBindingText = [bFunctionReference, Blueprint, InStructPropertyHandle]()
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				if(bFunctionReference)
				{
					UFunction* Function = MemberReference->ResolveMember<UFunction>(Blueprint->SkeletonGeneratedClass);
					if(Function)
					{
						return FText::FromName(Function->GetFName());
					}
					else
					{
						return FText::FromName(MemberReference->GetMemberName());
					}
				}
			}
			else if(Result == FPropertyAccess::MultipleValues)
			{
				return LOCTEXT("MultipleValues", "Multiple Values");
			}

			return FText::GetEmpty();
		};

		auto CurrentBindingToolTipText = [InStructPropertyHandle]()
		{
			void* StructData = nullptr;
			const FPropertyAccess::Result Result = InStructPropertyHandle->GetValueData(StructData);
			if(Result == FPropertyAccess::Success)
			{
				check(StructData);
				FMemberReference* MemberReference = static_cast<FMemberReference*>(StructData);
				return FText::FromName(MemberReference->GetMemberName());
			}
			else if(Result == FPropertyAccess::MultipleValues)
			{
				return LOCTEXT("MultipleValues", "Multiple Values");
			}

			return FText::GetEmpty();
		};
	
		FPropertyBindingWidgetArgs Args;
		Args.BindableSignature = PrototypeFunction;
		Args.OnGenerateBindingName = FOnGenerateBindingName::CreateLambda(OnGenerateBindingName);
		Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda(OnCanBindProperty);
		Args.OnGotoBinding = FOnGotoBinding::CreateLambda(OnGoToBinding);
		Args.OnCanGotoBinding = FOnCanGotoBinding::CreateLambda(OnCanGotoBinding);
		Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda(OnCanBindFunction);
		Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass){ return true; });
		Args.OnAddBinding = FOnAddBinding::CreateLambda(OnAddBinding);
		Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda(OnRemoveBinding);
		Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda(CanRemoveBinding);
		Args.OnNewFunctionBindingCreated = FOnNewFunctionBindingCreated::CreateLambda(OnNewFunctionBindingCreated);
		Args.CurrentBindingText = MakeAttributeLambda(CurrentBindingText);
		Args.CurrentBindingToolTipText = MakeAttributeLambda(CurrentBindingToolTipText);
		Args.CurrentBindingImage = FEditorStyle::GetBrush("GraphEditor.Function_16x");
		Args.CurrentBindingColor = FEditorStyle::GetSlateColor("Colors.Foreground").GetSpecifiedColor();
		Args.bGeneratePureBindings = false;
		Args.bAllowFunctionBindings = bFunctionReference;
		Args.bAllowFunctionLibraryBindings = bAllowFunctionLibraryReferences;
		Args.bAllowPropertyBindings = false;
		Args.bAllowNewBindings = true;
		Args.bAllowArrayElementBindings = false;
		Args.bAllowUObjectFunctions = false;
		Args.bAllowStructFunctions = false;
		Args.bAllowStructMemberBindings = false;

		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
		InHeaderRow
		.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SBox)
			.MaxDesiredWidth(200.0f)
			[
				PropertyAccessEditor.MakePropertyBindingWidget(Blueprint, Args)
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE
