// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMVVMCachedViewBindingPropertyPath.h"

#include "Editor.h"
#include "MVVMBlueprintView.h"
#include "MVVMEditorSubsystem.h"
#include "Styling/MVVMEditorStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/SMVVMPropertyPath.h"

#define LOCTEXT_NAMESPACE "SCachedViewBindingPropertyPath"

namespace UE::MVVM
{

void SCachedViewBindingPropertyPath::Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint)
{
	SetCanTick(true);

	WidgetBlueprint = InWidgetBlueprint;
	OnGetPropertyPath = InArgs._OnGetPropertyPath;

	const FMVVMBlueprintPropertyPath PropertyPath = OnGetPropertyPath.Execute();
	CachedPropertyPath = PropertyPath.GetFields(InWidgetBlueprint->SkeletonGeneratedClass);

	ChildSlot
	[
		SAssignNew(PropertyPathWidget, SPropertyPath, InWidgetBlueprint)
		.TextStyle(InArgs._TextStyle)
		.PropertyPath(PropertyPath)
		.ShowContext(InArgs._ShowContext)
		.ShowOnlyLastPath(InArgs._ShowOnlyLastPath)
	];
}

void SCachedViewBindingPropertyPath::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const FMVVMBlueprintPropertyPath PropertyPath = OnGetPropertyPath.Execute();
	TArray<FMVVMConstFieldVariant> NewCachedPropertyPath = PropertyPath.GetFields(WidgetBlueprint.Get()->SkeletonGeneratedClass);
	if (NewCachedPropertyPath != CachedPropertyPath)
	{
		CachedPropertyPath = MoveTemp(NewCachedPropertyPath);
		PropertyPathWidget->SetPropertyPath(PropertyPath);
	}
}


} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
