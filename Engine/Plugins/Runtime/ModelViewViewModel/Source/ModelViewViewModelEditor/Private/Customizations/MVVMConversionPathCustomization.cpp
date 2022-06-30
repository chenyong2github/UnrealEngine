// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMConversionPathCustomization.h"

#include "Algo/Transform.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Deque.h"
#include "DetailWidgetRow.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "NodeFactory.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SGraphPin.h"
#include "Styling/MVVMEditorStyle.h"
#include "UObject/StructOnScope.h"
#include "WidgetBlueprint.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SMVVMConversionPath.h"
#include "Widgets/SMVVMFieldSelector.h"
#include "Widgets/SMVVMSourceSelector.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MVVMConversionPath"

namespace UE::MVVM
{

	FConversionPathCustomization::FConversionPathCustomization(UWidgetBlueprint* InWidgetBlueprint)
	{
		check(InWidgetBlueprint);
		WidgetBlueprint = InWidgetBlueprint;
	}

	void FConversionPathCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();

		InPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FConversionPathCustomization::RefreshDetailsView));
		InPropertyHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FConversionPathCustomization::RefreshDetailsView));

		ParentHandle = InPropertyHandle->GetParentHandle();
		BindingModeHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, BindingType));

		HeaderRow
			.ShouldAutoExpand()
			.NameContent()
			[
				InPropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				InPropertyHandle->CreatePropertyValueWidget()
			];
	}

	void FConversionPathCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		ArgumentPinWidgets.Reset();

		AddRowForProperty(ChildBuilder, InPropertyHandle, true);
		AddRowForProperty(ChildBuilder, InPropertyHandle, false);
	}

	void FConversionPathCustomization::RefreshDetailsView() const
	{
		if (TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
		{
			PropertyUtilities->RequestRefresh();
		}
	}

	FText FConversionPathCustomization::GetFunctionPathText(TSharedRef<IPropertyHandle> Property) const
	{
		void* Value;
		FPropertyAccess::Result Result = Property->GetValueData(Value);

		if (Result == FPropertyAccess::Success)
		{
			FMemberReference* MemberReference = reinterpret_cast<FMemberReference*>(Value);
			UFunction* Function = MemberReference->ResolveMember<UFunction>(WidgetBlueprint);

			return FText::FromString(Function->GetPathName());
		}

		if (Result == FPropertyAccess::MultipleValues)
		{
			return LOCTEXT("MultipleValues", "Multiple Values");
		}

		return FText::GetEmpty();
	}

	void FConversionPathCustomization::OnTextCommitted(const FText& NewValue, ETextCommit::Type CommitType, TSharedRef<IPropertyHandle> Property, bool bSourceToDestination)
	{
		UFunction* FoundFunction = FindObject<UFunction>(nullptr, *NewValue.ToString(), true);
		OnFunctionPathChanged(FoundFunction, Property, bSourceToDestination);
	}

	void FConversionPathCustomization::OnFunctionPathChanged(const UFunction* NewFunction, TSharedRef<IPropertyHandle> InPropertyHandle, bool bSourceToDestination)
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		
		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* ViewBinding = static_cast<FMVVMBlueprintViewBinding*>(RawBinding);
			if (bSourceToDestination)
			{
				EditorSubsystem->SetSourceToDestinationConversionFunction(WidgetBlueprint, *ViewBinding, NewFunction);
			}
			else
			{
				EditorSubsystem->SetDestinationToSourceConversionFunction(WidgetBlueprint, *ViewBinding, NewFunction);
			}
		}

		RefreshDetailsView();
	}

	void FConversionPathCustomization::AddRowForProperty(IDetailChildrenBuilder& ChildBuilder, const TSharedPtr<IPropertyHandle>& InPropertyHandle, bool bSourceToDestination)
	{
		TArray<FMVVMBlueprintViewBinding*> ViewBindings;
		{
			TArray<void*> RawData;
			ParentHandle->AccessRawData(RawData);

			for (void* Data : RawData)
			{
				ViewBindings.Add((FMVVMBlueprintViewBinding*) Data);
			}
		}

		const FName FunctionPropertyName = bSourceToDestination ? GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewConversionPath, SourceToDestinationFunction) : GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewConversionPath, DestinationToSourceFunction);
		TSharedRef<IPropertyHandle> FunctionProperty = InPropertyHandle->GetChildHandle(FunctionPropertyName).ToSharedRef();

		ChildBuilder.AddProperty(FunctionProperty)
			.IsEnabled(
				TAttribute<bool>::CreateLambda([this, bSourceToDestination]()
					{
						uint8 EnumValue;
						if (BindingModeHandle->GetValue(EnumValue) == FPropertyAccess::Success)
						{
							return bSourceToDestination ? IsForwardBinding((EMVVMBindingMode) EnumValue) : IsBackwardBinding((EMVVMBindingMode) EnumValue);
						}
						return true;
					}))
			.CustomWidget()
			.NameContent()
			[
				FunctionProperty->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SEditableTextBox)
					.Text(this, &FConversionPathCustomization::GetFunctionPathText, FunctionProperty)
					.OnTextCommitted(this, &FConversionPathCustomization::OnTextCommitted, FunctionProperty, bSourceToDestination)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SMVVMConversionPath, WidgetBlueprint, bSourceToDestination)
					.Bindings(ViewBindings)
					.OnFunctionChanged(this, &FConversionPathCustomization::OnFunctionPathChanged, FunctionProperty, bSourceToDestination)
				]
			];

		if (ViewBindings.Num() != 1)
		{
			// don't show arguments for multi-selection
			return;
		}

		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

		UEdGraph* Graph = EditorSubsystem->GetConversionFunctionGraph(WidgetBlueprint, *ViewBindings[0], bSourceToDestination);
		if (Graph == nullptr)
		{
			return;
		}

		TArray<UK2Node_CallFunction*> FunctionNodes;
		Graph->GetNodesOfClass<UK2Node_CallFunction>(FunctionNodes);

		if (FunctionNodes.Num() != 1)
		{
			// ambiguous result, no idea what our function node is
			return;
		}

		UK2Node_CallFunction* CallFunctionNode = FunctionNodes[0];
		for (UEdGraphPin* Pin : CallFunctionNode->Pins)
		{
			// skip output and exec pins, the rest should be arguments
			if (Pin->Direction != EGPD_Input || Pin->PinName == UEdGraphSchema_K2::PN_Execute || Pin->PinName == UEdGraphSchema_K2::PN_Self)
			{
				continue;
			}

			const FName ArgumentName = Pin->GetFName();

			// create a new pin widget so that we can get the default value widget out of it
			TSharedPtr<SGraphPin> PinWidget = FNodeFactory::CreateK2PinWidget(Pin);

			TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;
			if (PinWidget.IsValid())
			{
				ArgumentPinWidgets.Add(PinWidget);
				ValueWidget = PinWidget->GetDefaultValueWidget();
			}

			if (ValueWidget == SNullWidget::NullWidget)
			{
				ValueWidget = SNew(STextBlock)
					.Text(LOCTEXT("DefaultValue", "Default Value"))
					.TextStyle(FAppStyle::Get(), "HintText");
			}

			ValueWidget->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &FConversionPathCustomization::GetArgumentWidgetVisibility, ArgumentName, bSourceToDestination, true));

			TSharedPtr<SMVVMFieldSelector> FieldSelector;

			ChildBuilder.AddCustomRow(Pin->GetDisplayName())
			.NameContent()
			[
				SNew(SBox)
				.Padding(FMargin(16, 0, 0, 0))
				[
					SNew(STextBlock)
					.Text(Pin->GetDisplayName())
				]
			]
			.ValueContent()
			[
				// TODO (sebastiann): This would be easier to accomplish if this entire thing was a widget
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						ValueWidget
					]
					+ SOverlay::Slot()
					[
						SNew(SHorizontalBox)
						.Visibility(this, &FConversionPathCustomization::GetArgumentWidgetVisibility, ArgumentName, bSourceToDestination, false)
						+ SHorizontalBox::Slot()
						[
							SNew(SMVVMSourceSelector)
							.SelectedSource(this, &FConversionPathCustomization::OnGetSelectedSource, ArgumentName, bSourceToDestination)
							.AvailableSources(this, &FConversionPathCustomization::OnGetAvailableSources, bSourceToDestination)
							.OnSelectionChanged(this, &FConversionPathCustomization::OnSetSource, ArgumentName, bSourceToDestination)
						]
						+ SHorizontalBox::Slot()
						[
							SAssignNew(FieldSelector, SMVVMFieldSelector)
							.IsSource(bSourceToDestination)
							.SelectedSource(this, &FConversionPathCustomization::OnGetSelectedSource, ArgumentName, bSourceToDestination)
							.AvailableFields(this, &FConversionPathCustomization::OnGetAvailableFields, ArgumentName, bSourceToDestination)
							.SelectedField(this, &FConversionPathCustomization::OnGetSelectedField, ArgumentName, bSourceToDestination)
							.OnSelectionChanged(this, &FConversionPathCustomization::OnSetProperty, ArgumentName, bSourceToDestination)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(5, 0, 0, 0)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("BindArgument", "Bind this argument to a property."))
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked(this, &FConversionPathCustomization::OnGetIsArgumentBound, ArgumentName, bSourceToDestination)
					.OnCheckStateChanged(this, &FConversionPathCustomization::OnCheckedBindArgument, ArgumentName, bSourceToDestination)
					[
						SNew(SBox)
						.WidthOverride(16)
						.HeightOverride(16)
						[
							SNew(SImage)
							.Image_Lambda([this, ArgumentName, bSourceToDestination]()
								{
									ECheckBoxState CheckState = OnGetIsArgumentBound(ArgumentName, bSourceToDestination);
									return CheckState == ECheckBoxState::Checked ? FAppStyle::GetBrush("Icons.Link") : FAppStyle::GetBrush("Icons.Unlink");
								})
						]
					]
				]
			];

			ArgumentFieldSelectors.Add(FieldSelector);
		}
	}

	TArray<FBindingSource> FConversionPathCustomization::OnGetAvailableSources(bool bSourceToDestination) const
	{
		UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		if (bSourceToDestination)
		{
			return Subsystem->GetAllViewModels(WidgetBlueprint);
		}
		else
		{
			return Subsystem->GetBindableWidgets(WidgetBlueprint);
		}
	}

	FBindingSource FConversionPathCustomization::OnGetSelectedSource(FName ArgumentName, bool bSourceToDestination) const
	{
		UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		FMVVMBlueprintPropertyPath Path;
		bool bFirst = true;

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* Binding = reinterpret_cast<FMVVMBlueprintViewBinding*>(RawBinding);
			FMVVMBlueprintPropertyPath ThisPath = Subsystem->GetPathForConversionFunctionArgument(WidgetBlueprint, *Binding, ArgumentName, bSourceToDestination);

			if (bFirst)
			{
				Path = ThisPath;
				bFirst = false;
			}
			else if (Path != ThisPath)
			{
				Path = FMVVMBlueprintPropertyPath();
			}
		}
		
		if (Path.IsFromViewModel())
		{
			return FBindingSource::CreateForViewModel(WidgetBlueprint, Path.GetViewModelId());
		}
		else if (Path.IsFromWidget())
		{
			return FBindingSource::CreateForWidget(WidgetBlueprint, Path.GetWidgetName());
		}

		return FBindingSource();
	}

	void FConversionPathCustomization::OnSetSource(FBindingSource Source, FName ArgumentName, bool bSourceToDestination)
	{
		FMVVMBlueprintPropertyPath Path;
		if (Source.ViewModelId.IsValid())
		{
			Path.SetViewModelId(Source.ViewModelId);
		}
		else
		{
			Path.SetWidgetName(Source.Name);
		}

		UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* Binding = reinterpret_cast<FMVVMBlueprintViewBinding*>(RawBinding);
			Subsystem->SetPathForConversionFunctionArgument(WidgetBlueprint, *Binding, ArgumentName, Path, bSourceToDestination);
		}

		for (const TSharedPtr<SMVVMFieldSelector>& FieldSelector : ArgumentFieldSelectors)
		{
			FieldSelector->Refresh();
		}
	}

	void FConversionPathCustomization::OnSetProperty(FMVVMBlueprintPropertyPath NewSelection, FName ArgumentName, bool bSourceToDestination)
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* Binding = reinterpret_cast<FMVVMBlueprintViewBinding*>(RawBinding);
			EditorSubsystem->SetPathForConversionFunctionArgument(WidgetBlueprint, *Binding, ArgumentName, NewSelection, bSourceToDestination);
		}
	}

	FMVVMBlueprintPropertyPath FConversionPathCustomization::OnGetSelectedField(FName ArgumentName, bool bSourceToDestination) const
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		
		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		FMVVMBlueprintPropertyPath Path;
		bool bFirst = true;

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* Binding = reinterpret_cast<FMVVMBlueprintViewBinding*>(RawBinding);
			
			FMVVMBlueprintPropertyPath CurrentPath = EditorSubsystem->GetPathForConversionFunctionArgument(WidgetBlueprint, *Binding, ArgumentName, bSourceToDestination);
			if (bFirst)
			{
				Path = CurrentPath;
				bFirst = false;
			}
			else if (Path != CurrentPath)
			{
				return FMVVMBlueprintPropertyPath();
			}
		}

		return Path;
	}

	TArray<FMVVMBlueprintPropertyPath> FConversionPathCustomization::OnGetAvailableFields(FName ArgumentName, bool bSourceToDestination) const
	{
		TArray<FMVVMBlueprintPropertyPath> AvailablePaths;

		FBindingSource Source = OnGetSelectedSource(ArgumentName, bSourceToDestination);
		UClass* SourceClass = nullptr;
		if (!Source.Name.IsNone())
		{
			// widget
			if (Source.Name.IsEqual(WidgetBlueprint->GetFName()))
			{
				SourceClass = WidgetBlueprint->GeneratedClass;
			}
			else if (const UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(Source.Name))
			{
				SourceClass = Widget->GetClass();
			}
		}
		else if (Source.ViewModelId.IsValid())
		{
			// viewmodel
			UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
			if (UMVVMBlueprintView* View = EditorSubsystem->GetView(WidgetBlueprint))
			{
				if (const FMVVMBlueprintViewModelContext* ViewModel = View->FindViewModel(Source.ViewModelId))
				{
					SourceClass = ViewModel->GetViewModelClass();
				}
			}
		}

		if (SourceClass == nullptr)
		{
			return AvailablePaths;
		}

		UMVVMSubsystem* Subsystem = GEngine->GetEngineSubsystem<UMVVMSubsystem>();
		TArray<FMVVMAvailableBinding> AvailableBindings = Subsystem->GetAvailableBindings(SourceClass, WidgetBlueprint->GeneratedClass);

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		FEdGraphPinType ArgumentType = GetArgumentPinType(ArgumentName, bSourceToDestination);

		Algo::TransformIf(AvailableBindings, AvailablePaths, 
			// filter handler
			[ArgumentType, Schema, SourceClass](const FMVVMAvailableBinding& Binding) -> bool
			{
				FName BindingName = Binding.GetBindingName().ToName();
				
				if (const FProperty* Property = SourceClass->FindPropertyByName(BindingName))
				{
					return true;
				}

				if (const UFunction* Function = SourceClass->FindFunctionByName(BindingName))
				{
					const FProperty* ReturnProperty = UE::MVVM::BindingHelper::GetReturnProperty(Function);
					if (ReturnProperty != nullptr)
					{
						FEdGraphPinType ReturnType;
						if (Schema->ConvertPropertyToPinType(ReturnProperty, ReturnType))
						{
							if (Schema->ArePinTypesCompatible(ReturnType, ArgumentType, SourceClass))
							{
								return true;
							}
						}
					}
				}

				return false;
			},
			[SourceClass, Source](const FMVVMAvailableBinding& Binding) -> FMVVMBlueprintPropertyPath
			{
				FName BindingName = Binding.GetBindingName().ToName();

				UE::MVVM::FMVVMConstFieldVariant Variant;
				if (const UFunction* Function = SourceClass->FindFunctionByName(BindingName))
				{
					Variant = UE::MVVM::FMVVMConstFieldVariant(Function);
				}
				else if (const FProperty* Property = SourceClass->FindPropertyByName(BindingName))
				{
					Variant = UE::MVVM::FMVVMConstFieldVariant(Property);
				}

				FMVVMBlueprintPropertyPath Path;
				if (!Source.Name.IsNone())
				{
					Path.SetWidgetName(Source.Name);
				}
				else
				{
					Path.SetViewModelId(Source.ViewModelId);
				}
				Path.SetBasePropertyPath(Variant);

				return Path;
			});

		return AvailablePaths;
	}

	ECheckBoxState FConversionPathCustomization::OnGetIsArgumentBound(FName ArgumentName, bool bSourceToDestination) const
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		FMVVMBlueprintPropertyPath Path;
		bool bFirst = true;

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* Binding = reinterpret_cast<FMVVMBlueprintViewBinding*>(RawBinding);

			FMVVMBlueprintPropertyPath CurrentPath = EditorSubsystem->GetPathForConversionFunctionArgument(WidgetBlueprint, *Binding, ArgumentName, bSourceToDestination);
			if (bFirst)
			{
				Path = CurrentPath;
				bFirst = false;
			}
			else if (Path != CurrentPath)
			{
				return ECheckBoxState::Undetermined;
			}
		}

		return Path.IsEmpty() ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
	}

	void FConversionPathCustomization::OnCheckedBindArgument(ECheckBoxState CheckState, FName ArgumentName, bool bSourceToDestination)
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();

		FMVVMBlueprintPropertyPath Path;

		if (CheckState == ECheckBoxState::Checked)
		{
			// HACK: Just set a placeholder viewmodel reference
			if (const UMVVMBlueprintView* View = EditorSubsystem->GetView(WidgetBlueprint))
			{
				const TArrayView<const FMVVMBlueprintViewModelContext> ViewModels = View->GetViewModels();
				if (ViewModels.Num() > 0)
				{
					Path.SetViewModelId(ViewModels[0].GetViewModelId());
				}
			}
		}

		TArray<void*> RawBindings;
		ParentHandle->AccessRawData(RawBindings);

		for (void* RawBinding : RawBindings)
		{
			FMVVMBlueprintViewBinding* Binding = reinterpret_cast<FMVVMBlueprintViewBinding*>(RawBinding);
			EditorSubsystem->SetPathForConversionFunctionArgument(WidgetBlueprint, *Binding, ArgumentName, Path, bSourceToDestination);
		}
	}

	EVisibility FConversionPathCustomization::GetArgumentWidgetVisibility(FName ArgumentName, bool bSourceToDestination, bool bDefaultValue) const
	{
		FMVVMBlueprintPropertyPath Path = OnGetSelectedField(ArgumentName, bSourceToDestination);
		if (Path.IsEmpty())
		{
			return bDefaultValue ? EVisibility::Visible : EVisibility::Collapsed;
		}
		else
		{
			return bDefaultValue ? EVisibility::Collapsed : EVisibility::Visible;
		}
	}

	FEdGraphPinType FConversionPathCustomization::GetArgumentPinType(FName ArgumentName, bool bSourceToDestination) const 
	{
		TArray<void*> RawData;
		ParentHandle->AccessRawData(RawData);
		if (RawData.Num() != 1)
		{
			return FEdGraphPinType();
		}

		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		UEdGraph* Graph = EditorSubsystem->GetConversionFunctionGraph(WidgetBlueprint, *reinterpret_cast<const FMVVMBlueprintViewBinding*>(RawData[0]), bSourceToDestination);
		if (Graph == nullptr)
		{
			return FEdGraphPinType();
		}

		TArray<UK2Node_CallFunction*> FunctionNodes;
		Graph->GetNodesOfClass<UK2Node_CallFunction>(FunctionNodes);
		if (FunctionNodes.Num() != 1)
		{
			// ambiguous result, no idea what our function node is
			return FEdGraphPinType();
		}

		UK2Node_CallFunction* CallFunctionNode = FunctionNodes[0];
		UEdGraphPin* Pin = CallFunctionNode->FindPin(ArgumentName);
		return Pin != nullptr ? Pin->PinType : FEdGraphPinType();
	}
}

#undef LOCTEXT_NAMESPACE 
