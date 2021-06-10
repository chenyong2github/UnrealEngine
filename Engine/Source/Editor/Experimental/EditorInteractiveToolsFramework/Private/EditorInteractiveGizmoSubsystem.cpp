// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorInteractiveGizmoSubsystem.h"
#include "EditorGizmos/EditorTransformGizmo.h"


#define LOCTEXT_NAMESPACE "UEditorInteractiveGizmoSubsystem"

DEFINE_LOG_CATEGORY_STATIC(LogEditorInteractiveGizmoSubsystem, Log, All);


UEditorInteractiveGizmoSubsystem::UEditorInteractiveGizmoSubsystem()
	: UEditorSubsystem()
{

}

void UEditorInteractiveGizmoSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	if (GEngine && GEngine->IsInitialized())
	{
		RegisterBuiltinGizmoSelectionTypes();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddUObject(this, &UEditorInteractiveGizmoSubsystem::RegisterBuiltinGizmoSelectionTypes);
	}
}

void UEditorInteractiveGizmoSubsystem::Deinitialize()
{
	DeregisterBuiltinGizmoSelectionTypes();
}

void UEditorInteractiveGizmoSubsystem::RegisterBuiltinGizmoSelectionTypes()
{
#if 0
	// Register built-in gizmo types here
	TObjectPtr<UEditorTransformGizmoBuilder> EditorTransformBuilder = NewObject<UEditorTransformGizmoBuilder>();
	RegisterGizmoSelectionType(EditorTransformBuilder);
#endif

	RegisterEditorGizmoSelectionTypesDelegate.Broadcast();
}

void UEditorInteractiveGizmoSubsystem::DeregisterBuiltinGizmoSelectionTypes()
{
	DeregisterEditorGizmoSelectionTypesDelegate.Broadcast();
	ClearGizmoSelectionTypeRegistry();
}

void UEditorInteractiveGizmoSubsystem::RegisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder)
{
	if (ensure(InGizmoSelectionBuilder != nullptr) == false)
	{
		return;
	}

	if (GizmoSelectionBuilders.Contains(InGizmoSelectionBuilder))
	{
		UE_LOG(LogEditorInteractiveGizmoSubsystem, Warning,
			TEXT("UInteractiveGizmoSubsystem::RegisterGizmoSelectionType: type has already been registered %s"), *InGizmoSelectionBuilder->GetName());
		return;
	}

	GizmoSelectionBuilders.Add(InGizmoSelectionBuilder);
	GizmoSelectionBuilders.StableSort(
		[](UEditorInteractiveGizmoSelectionBuilder& A, UEditorInteractiveGizmoSelectionBuilder& B) {
			return (A).GetPriority() > (B).GetPriority();
		});
}

TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> UEditorInteractiveGizmoSubsystem::GetQualifiedGizmoSelectionBuilders(const FToolBuilderState& InToolBuilderState)
{
	TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> FoundBuilders;
	FEditorGizmoTypePriority FoundPriority = 0;

	for (TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> Builder : GizmoSelectionBuilders)
	{
		if (Builder->GetPriority() < FoundPriority)
		{
			break;
		}

		if (Builder->SatisfiesCondition(InToolBuilderState))
		{
			FoundBuilders.Add(Builder);
			FoundPriority = Builder->GetPriority();
		}
	}

	return FoundBuilders;
}

bool UEditorInteractiveGizmoSubsystem::DeregisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder)
{
	if (ensure(InGizmoSelectionBuilder != nullptr) == false)
	{
		return false;
	}

	if (GizmoSelectionBuilders.Contains(InGizmoSelectionBuilder) == false)
	{
		UE_LOG(LogEditorInteractiveGizmoSubsystem, Warning,
			TEXT("UInteractiveGizmoSubsystem::DeregisterGizmoSelectionType: type has already been registered %s"), *InGizmoSelectionBuilder->GetName());
		return false;
	}
	GizmoSelectionBuilders.Remove(InGizmoSelectionBuilder);
	return true;
}

void UEditorInteractiveGizmoSubsystem::ClearGizmoSelectionTypeRegistry()
{
	GizmoSelectionBuilders.Reset();
}

#undef LOCTEXT_NAMESPACE

