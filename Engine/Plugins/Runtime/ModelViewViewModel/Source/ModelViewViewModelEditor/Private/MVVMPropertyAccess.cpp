// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMPropertyAccess.h"
//
//#include "WidgetBlueprint.h"
//#include "WidgetBlueprintExtension.h"
//#include "MVVMViewModel.h"
//
//#include "Features/IModularFeatures.h"
//#include "Framework/MultiBox/MultiBoxBuilder.h"
//#include "Kismet2/BlueprintEditorUtils.h"
//#include "IPropertyAccessEditor.h"
//#include "PropertyHandle.h"
//
//#include "Widgets/SBoxPanel.h"
//#include "Widgets/Images/SImage.h"
//#include "Widgets/Layout/SSpacer.h"
//#include "Widgets/Text\STextBlock.h"
//
//#define LOCTEXT_NAMESPACE "MVVMPropertyAccess"
//
//FMVVMPropertyAccess::FMVVMPropertyAccess(UWidgetBlueprint* InWidgetBlueprint, const TSharedPtr<IPropertyHandle>& InPropertyHandle)
//	: WidgetBlueprint(InWidgetBlueprint)
//	, PropertyHandle(InPropertyHandle)
//{
//
//}
//
//
//TSharedPtr<SWidget> FMVVMPropertyAccess::MakeViewModelBindingMenu() const
//{
//	const bool bInShouldCloseWindowAfterMenuSelection = false;
//	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);
//
//	{
//		MenuBuilder.BeginSection("BindingActions", LOCTEXT("Bindings", "Bindings"));
//		MenuBuilder.AddMenuEntry(
//			LOCTEXT("RemoveBinding", "Remove Binding"),
//			LOCTEXT("RemoveBindingToolTip", "Removes the current binding"),
//			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Cross"),
//			FUIAction()
//		);
//		MenuBuilder.AddMenuEntry(
//			LOCTEXT("CreateBinding", "Create Binding"),
//			LOCTEXT("CreateBindingToolTip", "Creates a new function on the widget blueprint that will return the binding data for this property."),
//			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Plus"),
//			FUIAction()
//		);
//		MenuBuilder.EndSection();
//	}
//
//	{
//		UClass* PlayerPawnViewModel = FindObject<UClass>(nullptr, TEXT("/Game/PlayerPawnViewModel.PlayerPawnViewModel_C"));
//		TSubclassOf<UMVVMViewModelBase> ViewModelDefinition = PlayerPawnViewModel;
//
//		MenuBuilder.BeginSection("ViewModels", LOCTEXT("ViewModels", "ViewModels"));
//		// for all ViewModel
//		MenuBuilder.AddSubMenu(
//			MakeViewModelSubMenuWidget(ViewModelDefinition),
//			FNewMenuDelegate::CreateRaw(this, &FMVVMPropertyAccess::FillViewModelSubMenu, ViewModelDefinition));
//		MenuBuilder.EndSection();
//	}
//
//	return MenuBuilder.MakeWidget();
//}
//
//TSharedRef<SWidget> FMVVMPropertyAccess::MakeViewModelSubMenuWidget(TSubclassOf<UMVVMViewModelBase> ViewModelDefinition) const
//{
//	UMVVMViewModelBase* ViewModel_CDO = ViewModelDefinition.GetDefaultObject();
//	if (ViewModel_CDO == nullptr)
//	{
//		return SNullWidget::NullWidget;
//	}
//
//	return SNew(SHorizontalBox)
//		.ToolTipText(ViewModelDefinition->GetToolTipText())
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		[
//			SNew(SSpacer)
//			.Size(FVector2D(18.0f, 0.0f))
//		]
//		//+ SHorizontalBox::Slot()
//		//.AutoWidth()
//		//.VAlign(VAlign_Center)
//		//.Padding(1.0f, 0.0f)
//		//[
//		//	SNew(SImage)
//		//	.Image(ContextStruct.Icon)
//		//]
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		.VAlign(VAlign_Center)
//		.Padding(4.0f, 0.0f)
//		[
//			SNew(STextBlock)
//			.Text(ViewModelDefinition->GetDisplayNameText())
//		];
//}
//
//
//TSharedRef<SWidget> FMVVMPropertyAccess::MakeFunctionWidget(UFunction* Function)
//{
//	static const FName FunctionIcon(TEXT("GraphEditor.Function_16x"));
//
//	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
//
//	FEdGraphPinType PinType;
//	if (FProperty* ReturnProperty = Function->GetReturnProperty())
//	{
//		Schema->ConvertPropertyToPinType(ReturnProperty, PinType);
//	}
//
//	return SNew(SHorizontalBox)
//		.ToolTipText(Function->GetToolTipText())
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		[
//			SNew(SSpacer)
//			.Size(FVector2D(18.0f, 0.0f))
//		]
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		.VAlign(VAlign_Center)
//		.Padding(1.0f, 0.0f)
//		[
//			SNew(SImage)
//			.Image(FAppStyle::Get().GetBrush(FunctionIcon))
//			.ColorAndOpacity(Schema->GetPinTypeColor(PinType))
//		]
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		.VAlign(VAlign_Center)
//		.Padding(4.0f, 0.0f)
//		[
//			SNew(STextBlock)
//			.Text(Function->GetDisplayNameText())
//		];
//}
//
//
//TSharedRef<SWidget> FMVVMPropertyAccess::MakePropertyWidget(FProperty* Property)
//{
//	static FName PropertyIcon(TEXT("Kismet.VariableList.TypeIcon"));
//
//	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
//
//	FEdGraphPinType PinType;
//	Schema->ConvertPropertyToPinType(Property, PinType);
//
//	return SNew(SHorizontalBox)
//		.ToolTipText(Property->GetToolTipText())
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		[
//			SNew(SSpacer)
//			.Size(FVector2D(18.0f, 0.0f))
//		]
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		.VAlign(VAlign_Center)
//		.Padding(1.0f, 0.0f)
//		[
//			SNew(SImage)
//			.Image(FBlueprintEditorUtils::GetIconFromPin(PinType, true))
//			.ColorAndOpacity(Schema->GetPinTypeColor(PinType))
//		]
//		+ SHorizontalBox::Slot()
//		.AutoWidth()
//		.VAlign(VAlign_Center)
//		.Padding(4.0f, 0.0f)
//		[
//			SNew(STextBlock)
//			.Text(FText::FromName(Property->GetFName()))
//		];
//}
//
//void FMVVMPropertyAccess::FillViewModelSubMenu(FMenuBuilder& MenuBuilder, TSubclassOf<UMVVMViewModelBase> ViewModelDefinition) const
//{
//	//for (const FMVVMViewModelOutput& Output : ViewModelDefinition.GetDefaultObject()->Outputs)
//	//{
//	//	UFunction* Function = nullptr;
//	//	FProperty* Property = nullptr;
//	//	if (!Output.ConversionFunction.IsNone())
//	//	{
//	//		Function = ViewModelDefinition->FindFunctionByName(Output.ConversionFunction);
//	//	}
//	//	else
//	//	{
//	//		if (Output.InputIds.Num() == 1)
//	//		{
//	//			if (const FMVVMViewModelInput* Input = ViewModelDefinition.GetDefaultObject()->FindInput(Output.InputIds[0]))
//	//			{
//	//				FFieldVariant FieldVariant = ViewModelDefinition.GetDefaultObject()->GetInputField(*Input);
//	//				Function = Cast<UFunction>(FieldVariant.ToUObject());
//	//				Property = CastField<FProperty>(FieldVariant.ToField());
//	//			}
//	//		}
//	//	}
//
//	//	if (Function)
//	//	{
//	//		MenuBuilder.AddMenuEntry(
//	//			FUIAction(FExecuteAction::CreateRaw(this, &FMVVMPropertyAccess::HandleAddBinding, Output.OutputId)),
//	//			MakeFunctionWidget(Function));
//	//	}
//	//	else if (Property)
//	//	{
//	//		MenuBuilder.AddMenuEntry(
//	//			FUIAction(FExecuteAction::CreateRaw(this, &FMVVMPropertyAccess::HandleAddBinding, Output.OutputId)),
//	//			MakePropertyWidget(Property));
//	//	}
//	//}
//}
//
//void FMVVMPropertyAccess::HandleAddBinding(FGuid OutputId) const
//{
//
//}
//
//
//bool FMVVMPropertyAccess::IsBindableFunction(UFunction* InFunction)
//{
//	int32 ParamCount = 0;
//	FProperty* ReturnProperty = nullptr;
//	for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
//	{
//		if (It->PropertyFlags & CPF_ReturnParm)
//		{
//			ReturnProperty = *It;
//		}
//		if (It->PropertyFlags & CPF_Parm)
//		{
//			++ParamCount;
//		}
//	}
//
//	return ReturnProperty != nullptr
//		&& ReturnProperty->IsA(FFloatProperty::StaticClass())
//		&& ParamCount == 0
//		&& InFunction->HasAllFunctionFlags(FUNC_Public | FUNC_BlueprintPure)
//		&& !InFunction->HasAnyFunctionFlags(FUNC_EditorOnly);
//}
//
//
//bool FMVVMPropertyAccess::IsBindableProperty(FProperty* InProperty)
//{
//	return InProperty->IsA(FFloatProperty::StaticClass())
//		&& InProperty->HasAllPropertyFlags(CPF_BlueprintVisible)
//		&& !InProperty->HasAnyPropertyFlags(CPF_EditorOnly | CPF_Deprecated);
//}
//
////TSharedPtr<SWidget> FMVVMPropertyAccess::MakeViewModelBindingMenu(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<IPropertyHandle>& PropertyHandle)
////{
////	static const FName NAME_PropertyAccessEditor = "PropertyAccessEditor";
////	if (!IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
////	{
////		return TSharedPtr<SWidget>();
////	}
////
////	//if (UMVVMViewModelBlueprintExtension* Extension = UWidgetBlueprintExtension::RequestExtension<UMVVMViewModelBlueprintExtension>(WidgetBlueprint))
////	//{
////
////	FProperty* Property = PropertyHandle->GetProperty();
////	const FName PropertyName = Property->GetFName();
////
////	TArray<TWeakObjectPtr<UObject>> Objects;
////	{
////		TArray<UObject*> RawObjects;
////		PropertyHandle->GetOuterObjects(RawObjects);
////
////		Objects.Reserve(RawObjects.Num());
////		for (UObject* RawObject : RawObjects)
////		{
////			Objects.Add(RawObject);
////		}
////	}
////
////	UWidget* Widget = Objects.Num() ? Cast<UWidget>(Objects[0]) : nullptr;
////
////	FPropertyBindingMenuWidgetArgs Args;
////	Args.Property = Property;
////	Args.bAllowNewBindings = false;
////	Args.bGeneratePureBindings = true;
////	//Args.BindableSignature = InSetterSignature;
////	//Args.OnGenerateBindingName = FOnGenerateBindingName::CreateLambda([WidgetName]()
////	//	{
////	//		return WidgetName;
////	//	});
////
////	//Args.OnGotoBinding = FOnGotoBinding::CreateLambda([InEditor, Objects](FName InPropertyName)
////	//	{
////	//		UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();
////
////	//		//TODO UMG O(N) Isn't good for this, needs to be map, but map isn't serialized, need cached runtime map for fast lookups.
////
////	//		for (const TWeakObjectPtr<UObject>& ObjectPtr : Objects)
////	//		{
////	//			UObject* Object = ObjectPtr.Get();
////
////	//			// Ignore null outer objects
////	//			if (Object == nullptr)
////	//			{
////	//				continue;
////	//			}
////
////	//			for (const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings)
////	//			{
////	//				if (Binding.ObjectName == Object->GetName() && Binding.PropertyName == InPropertyName)
////	//				{
////	//					if (Binding.Kind == EBindingKind::Function)
////	//					{
////	//						TArray<UEdGraph*> AllGraphs;
////	//						ThisBlueprint->GetAllGraphs(AllGraphs);
////
////	//						FGuid SearchForGuid = Binding.MemberGuid;
////	//						if (!Binding.SourcePath.IsEmpty())
////	//						{
////	//							SearchForGuid = Binding.SourcePath.Segments.Last().GetMemberGuid();
////	//						}
////
////	//						for (UEdGraph* Graph : AllGraphs)
////	//						{
////	//							if (Graph->GraphGuid == SearchForGuid)
////	//							{
////	//								InEditor.Pin()->SetCurrentMode(FWidgetBlueprintApplicationModes::GraphMode);
////	//								InEditor.Pin()->OpenDocument(Graph, FDocumentTracker::OpenNewDocument);
////	//							}
////	//						}
////
////	//						// Either way return
////	//						return true;
////	//					}
////	//				}
////	//			}
////	//		}
////
////	//		return false;
////	//	});
////
////		//Args.OnCanGotoBinding = FOnCanGotoBinding::CreateLambda([InEditor, Objects](FName InPropertyName)
////		//	{
////		//		//UWidgetBlueprint* ThisBlueprint = InEditor.Pin()->GetWidgetBlueprintObj();
////
////		//		//for (const TWeakObjectPtr<UObject>& ObjectPtr : Objects)
////		//		//{
////		//		//	UObject* Object = ObjectPtr.Get();
////
////		//		//	// Ignore null outer objects
////		//		//	if (Object == nullptr)
////		//		//	{
////		//		//		continue;
////		//		//	}
////
////		//		//	for (const FDelegateEditorBinding& Binding : ThisBlueprint->Bindings)
////		//		//	{
////		//		//		if (Binding.ObjectName == Object->GetName() && Binding.PropertyName == InPropertyName)
////		//		//		{
////		//		//			if (Binding.Kind == EBindingKind::Function)
////		//		//			{
////		//		//				return true;
////		//		//			}
////		//		//		}
////		//		//	}
////		//		//}
////
////		//		return false;
////		//	});
////
////		Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([](FProperty* InProperty)
////			{
////				//static const FName NAME_OnChangedEvent = "OnChangedEvent";
////				//if (InProperty->HasMetaData(NAME_OnChangedEvent))
////				//{
////				//	const FName EventName = FName(InProperty->GetMetaData(NAME_OnChangedEvent));
////				//	if (FMulticastDelegateProperty* Event = FindFProperty<FMulticastDelegateProperty>(InProperty->GetOwnerStruct(), EventName))
////				//	{
////				//		check(Event->SignatureFunction);
////				//		const bool bHasValidReturnValue = Event->SignatureFunction->GetReturnProperty() == nullptr;
////				//		bool bHasValidParams = true;
////				//		for (TFieldIterator<FProperty> ParamIt(Event->SignatureFunction); ParamIt; ++ParamIt)
////				//		{
////				//			if ((*ParamIt)->HasAnyPropertyFlags(CPF_Parm))
////				//			{
////				//				bHasValidParams = false;
////				//			}
////				//		}
////
////				//		return bHasValidReturnValue && bHasValidParams;
////				//	}
////				//}
////
////				//return false;
////				return InProperty->IsA(FFloatProperty::StaticClass())
////					&& InProperty->HasAllPropertyFlags(CPF_BlueprintVisible)
////					&& !InProperty->HasAnyPropertyFlags(CPF_EditorOnly| CPF_Deprecated);
////			});
////
////		Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda([](UFunction* InFunction)
////			{
////				int32 ParamCount = 0;
////				FProperty* ReturnProperty = nullptr;
////				for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
////				{
////					if (It->PropertyFlags & CPF_ReturnParm)
////					{
////						ReturnProperty = *It;
////					}
////					if (It->PropertyFlags & CPF_Parm)
////					{
////						++ParamCount;
////					}
////				}
////
////				return ReturnProperty != nullptr
////					&& ReturnProperty->IsA(FFloatProperty::StaticClass())
////					&& ParamCount == 0
////					&& InFunction->HasAllFunctionFlags(FUNC_Public| FUNC_BlueprintPure)
////					&& !InFunction->HasAnyFunctionFlags(FUNC_EditorOnly);
////			});
////
////		//Args.OnCanBindFunction = FOnCanBindFunction::CreateLambda([InDelegateProperty](UFunction* InFunction)
////		//	{
////		//		auto HasFunctionBinder = [InFunction](UFunction* InBindableSignature)
////		//		{
////		//			if (InFunction->NumParms == 1 && InBindableSignature->NumParms == 1)
////		//			{
////		//				if (FProperty* FunctionReturn = InFunction->GetReturnProperty())
////		//				{
////		//					if (FProperty* DelegateReturn = InBindableSignature->GetReturnProperty())
////		//					{
////		//						// Find the binder that can handle the delegate return type.
////		//						TSubclassOf<UPropertyBinding> Binder = UWidget::FindBinderClassForDestination(DelegateReturn);
////		//						if (Binder != nullptr)
////		//						{
////		//							// Ensure that the binder also can handle binding from the property we care about.
////		//							if (Binder->GetDefaultObject<UPropertyBinding>()->IsSupportedSource(FunctionReturn))
////		//							{
////		//								return true;
////		//							}
////		//						}
////		//					}
////		//				}
////		//			}
////
////		//			return false;
////		//		};
////
////		//		// We ignore CPF_ReturnParm because all that matters for binding to script functions is that the number of out parameters match.
////		//		return (InFunction->IsSignatureCompatibleWith(InDelegateProperty->SignatureFunction, UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_ReturnParm) ||
////		//			HasFunctionBinder(InDelegateProperty->SignatureFunction));
////		//	});
////
////		Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
////			{
////				return true;
////			});
////
////		Args.OnAddBinding = FOnAddBinding::CreateLambda([WidgetBlueprint, Objects](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
////			{
////				WidgetBlueprint->Modify();
////
////				UMVVMViewModelBlueprintExtension* Extension = UWidgetBlueprintExtension::GetExtension<UMVVMViewModelBlueprintExtension>(WidgetBlueprint);
////				if (Extension == nullptr)
////				{
////					Extension = UWidgetBlueprintExtension::RequestExtension<UMVVMViewModelBlueprintExtension>(WidgetBlueprint);
////				}
////				UMVVMViewModelEditorData* ViewModel = Extension->GetViewModelEditorData();
////				if (ViewModel == nullptr)
////				{
////					Extension->CreateViewModelInstance();
////					ViewModel = Extension->GetViewModelEditorData();
////				}
////
////
////				FGuid ViewModelBindingId;
////				UClass* PlayerPawnViewModel = FindObject<UClass>(nullptr, TEXT("/Game/PlayerPawnViewModel.PlayerPawnViewModel_C"));
////				if (UMVVMViewModelBase* Base = PlayerPawnViewModel->GetDefaultObject<UMVVMViewModelBase>())
////				{
////					const FMVVMViewModelBinding* ViewModelBinding = Base->FindBindingByViewModelPropertyName(InBindingChain.Last().Field.GetFName());
////					if (ViewModelBinding)
////					{
////						ViewModelBindingId = ViewModelBinding->BindingId;
////					}
////				}
////
////				// Find the ViewModel binding or add it in the ViewModel
////				//FGuid ViewModelBindingId;
////				//{
////				//	FMVVMViewModelBindingEditorData ViewModelEditorData;
////				//	FMVVMModelContext Context;
////				//	Context.ContextClass = FindObject<UClass>(nullptr, TEXT("/Script/MVVMProto.ProtoCharacter"));
////				//	Context.ContextName = TEXT("PlayerCharacter");
////				//	ViewModel->AddUniqueViewModelBinding(Context, InBindingChain.Last().Field.GetFName().ToString());
////				//}
////
////				// Add or modify the current Model binding
////				{
////					for (TWeakObjectPtr<UObject> WeakObject : Objects)
////					{
////						if (UObject* Object = WeakObject.Get())
////						{
////							FMVVMViewBindingEditorData ViewData;
////							ViewData.DestinationObject = Object;
////							ViewData.DestinationPropertyPath = InPropertyName.ToString();
////							ViewData.ViewModelBindingId = ViewModelBindingId;
////							if (ViewData.Resolve())
////							{
////								Extension->AddUniqueViewBinding(MoveTemp(ViewData));
////							}
////						}
////					}
////				}
////			});
////
////	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
////	TArray<FBindingContextStruct> BindingContextStructs;
////
////	//if (UMVVMViewModelBlueprintExtension* Extension = UWidgetBlueprintExtension::RequestExtension<UMVVMViewModelBlueprintExtension>(WidgetBlueprint))
////	//{
////		UClass* PlayerPawnViewModel = FindObject<UClass>(nullptr, TEXT("/Game/PlayerPawnViewModel.PlayerPawnViewModel_C"));
////		FText DisplayName = PlayerPawnViewModel->GetDisplayNameText();
////		FText Tooltip = PlayerPawnViewModel->GetToolTipText();
////		UObject* CDO = PlayerPawnViewModel->GetDefaultObject();
////		BindingContextStructs.Emplace(PlayerPawnViewModel, nullptr, DisplayName, Tooltip);
////	//}
////
////	//UStruct* FoundStruct = FindObject<UStruct>(nullptr, TEXT("/Script/MVVMProto.ProtoCharacter"));
////	//BindingContextStructs.Emplace(FoundStruct, nullptr, LOCTEXT("cONTEXT", "fOUNDdOUNCONT"), LOCTEXT("cONTSSEXT", "fOUNDdOUSSNCONT"));
////
////	return PropertyAccessEditor.MakePropertyBindingMenuWidget(BindingContextStructs, Args);
////}
//
//#undef LOCTEXT_NAMESPACE