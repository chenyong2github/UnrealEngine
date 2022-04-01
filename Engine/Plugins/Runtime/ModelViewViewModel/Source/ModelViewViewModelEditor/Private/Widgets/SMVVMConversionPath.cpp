// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMConversionPath.h"

#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMPropertyPathHelpers.h"
#include "MVVMSubsystem.h"
#include "Styling/StyleColors.h"
#include "Types/MVVMFieldVariant.h"
#include "UMGStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "MVVMConversionPath"

void SMVVMConversionPath::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint, bool bInIsGetter)
{
	bIsGetter = bInIsGetter;
	WidgetBlueprint = InWidgetBlueprint;
	OnFunctionChanged = InArgs._OnFunctionChanged;
	Bindings = InArgs._Bindings;
	check(Bindings.IsSet());

	ChildSlot
	[
		SAssignNew(Anchor, SMenuAnchor)
		.ToolTipText(this, &SMVVMConversionPath::GetFunctionToolTip)
		.OnGetMenuContent(this, &SMVVMConversionPath::GetFunctionMenuContent)
		.Visibility(this, &SMVVMConversionPath::IsFunctionVisible)
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(3, 0, 3, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &SMVVMConversionPath::OnButtonClicked)
				[
					SNew(SImage)
					.Image(FUMGStyle::Get().GetBrush(bIsGetter ? "MVVM.GetterFunction" : "MVVM.SetterFunction"))
					.ColorAndOpacity(this, &SMVVMConversionPath::GetFunctionColor)
				]
			]
		]
	];
}

EVisibility SMVVMConversionPath::IsFunctionVisible() const
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	if (ViewBindings.Num() == 0)
	{
		return EVisibility::Collapsed;
	}

	for (const FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		bool bShouldBeVisible =
			Binding->BindingType == EMVVMBindingMode::OneTimeToDestination ||
			Binding->BindingType == EMVVMBindingMode::OneWayToDestination ||
			Binding->BindingType == EMVVMBindingMode::TwoWay;

		if (bShouldBeVisible)
		{
			return EVisibility::Visible;
		}
	}
	
	return EVisibility::Hidden;
}

FString SMVVMConversionPath::GetFunctionPath() const
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	for (const FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		FString FunctionPath = bIsGetter ? Binding->Conversion.GetConversionFunctionPath : Binding->Conversion.SetConversionFunctionPath;
		return FunctionPath;
	}
	return FString();
}

FText SMVVMConversionPath::GetFunctionToolTip() const
{
	FString FunctionPath = GetFunctionPath();
	if (!FunctionPath.IsEmpty())
	{
		return FText::FromString(FunctionPath);
	}

	return bIsGetter ?
		LOCTEXT("AddGetterConversionFunction", "Add conversion function to be used when getting the value from the viewmodel.") :
		LOCTEXT("AddSetterConversionFunction", "Add conversion function to be used when setting the value in the viewmodel.");
}

FSlateColor SMVVMConversionPath::GetFunctionColor() const
{
	FString FunctionPath = GetFunctionPath();
	if (FunctionPath.IsEmpty())
	{
		return FStyleColors::Foreground;
	}

	return FStyleColors::AccentGreen;
}

FReply SMVVMConversionPath::OnButtonClicked() const
{
	Anchor->SetIsOpen(!Anchor->IsOpen());
	return FReply::Handled();
}

void SMVVMConversionPath::SetConversionFunction(const UFunction* Function)
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	if (ViewBindings.Num() == 0)
	{
		return;
	}

	for (FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		if (bIsGetter)
		{
			Binding->Conversion.GetConversionFunctionPath = Function->GetPathName();
		}
		else
		{
			Binding->Conversion.SetConversionFunctionPath = Function->GetPathName();
		}
	}

	if (OnFunctionChanged.IsBound())
	{
		OnFunctionChanged.Execute(Function->GetPathName());
	}
}

TSharedRef<SWidget> SMVVMConversionPath::GetFunctionMenuContent()
{
	TArray<FMVVMBlueprintViewBinding*> ViewBindings = Bindings.Get(TArray<FMVVMBlueprintViewBinding*>());
	if (ViewBindings.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TSet<const UFunction*> ConversionFunctions;

	for (FMVVMBlueprintViewBinding* Binding : ViewBindings)
	{
		UE::MVVM::FViewModelFieldPathHelper ViewModelHelper(&Binding->ViewModelPath, WidgetBlueprint);
		UE::MVVM::FMVVMConstFieldVariant ViewModelField = ViewModelHelper.GetSelectedField();

		UE::MVVM::FWidgetFieldPathHelper WidgetHelper(&Binding->WidgetPath, WidgetBlueprint);
		UE::MVVM::FMVVMConstFieldVariant WidgetField = WidgetHelper.GetSelectedField();

		UMVVMSubsystem::FConstDirectionalBindingArgs Args;
		Args.SourceBinding = bIsGetter ? ViewModelField : WidgetField;
		Args.DestinationBinding = bIsGetter ? WidgetField : ViewModelField;

		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		TArray<const UFunction*> FunctionsForThis = EditorSubsystem->GetAvailableConversionFunctions(Args.SourceBinding, Args.DestinationBinding);

		if (ConversionFunctions.Num() > 0)
		{
			ConversionFunctions = ConversionFunctions.Intersect(TSet<const UFunction*>(FunctionsForThis));
		}
		else
		{
			ConversionFunctions = TSet<const UFunction*>(FunctionsForThis);
		}
	}

	FMenuBuilder MenuBuilder(true, TSharedPtr<const FUICommandList>());

	if (ConversionFunctions.Num() == 0)
	{
		MenuBuilder.AddWidget(
			SNew(SBox)
			.Padding(10,0)
			[
				SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), "HintText")
					.Text(LOCTEXT("NoCompatibleFunctions", "No compatible functions found."))
			],
			FText::GetEmpty(),
			true, // no indent
			false // searchable
		);
	}

	for (const UFunction* Function : ConversionFunctions)
	{
		FUIAction Action(FExecuteAction::CreateSP(this, &SMVVMConversionPath::SetConversionFunction, Function));
		MenuBuilder.AddMenuEntry(
			Function->GetDisplayNameText(),
			Function->GetToolTipText(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.FunctionIcon"),
			Action);
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE