// Copyright Epic Games, Inc. All Rights Reserved.


#include "EditorInteractiveGizmoManager.h"
#include "EditorInteractiveGizmoSelectionBuilder.h"
#include "EditorInteractiveGizmoSubsystem.h"
#include "EdModeInteractiveToolsContext.h"
#include "EditorModeManager.h"
#include "HAL/IConsoleManager.h"
#include "InputRouter.h"
#include "InteractiveGizmo.h"
#include "InteractiveToolChange.h"
#include "InteractiveToolsContext.h"
#include "ToolContextInterfaces.h"


#define LOCTEXT_NAMESPACE "UEditorInteractiveGizmoManager"

#if 0
static TAutoConsoleVariable<int32> CVarUseLegacyWidget(
	TEXT("Gizmos.UseLegacyWidget"),
	1,
	TEXT("Specify whether to use selection-based gizmos or legacy widget\n")
	TEXT("0 = enable UE5 transform and other selection-based gizmos.\n")
	TEXT("1 = enable legacy UE4 transform widget."),
	ECVF_RenderThreadSafe);
#endif


UEditorInteractiveGizmoManager::UEditorInteractiveGizmoManager() :
	UInteractiveGizmoManager()
{

}


void UEditorInteractiveGizmoManager::InitializeWithEditorModeManager(IToolsContextQueriesAPI* QueriesAPIIn, IToolsContextTransactionsAPI* TransactionsAPIIn, UInputRouter* InputRouterIn, FEditorModeTools* InEditorModeManager)
{
	Super::Initialize(QueriesAPIIn, TransactionsAPIIn, InputRouterIn);
	EditorModeManager = InEditorModeManager;

}


void UEditorInteractiveGizmoManager::Shutdown()
{
	DestroyAllSelectionGizmos();
	Super::Shutdown();
}

void UEditorInteractiveGizmoManager::RegisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder)
{
	if (ensure(InGizmoSelectionBuilder))
	{
		if (GizmoSelectionBuilders.Contains(InGizmoSelectionBuilder))
		{
			DisplayMessage(
				FText::Format(LOCTEXT("DeregisterFailedMessage", "UInteractiveGizmoSubsystem::DeregisterGizmoSelectionType: type has already been registered {0}"), FText::FromName(InGizmoSelectionBuilder->GetFName())),
				EToolMessageLevel::Internal);
			return;
		}

		GizmoSelectionBuilders.Add(InGizmoSelectionBuilder);
		GizmoSelectionBuilders.StableSort(
			[](UEditorInteractiveGizmoSelectionBuilder& A, UEditorInteractiveGizmoSelectionBuilder& B) {
				return (A).GetPriority() > (B).GetPriority();
			});
	}
}

bool UEditorInteractiveGizmoManager::DeregisterGizmoSelectionType(const TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> InGizmoSelectionBuilder)
{
	if (ensure(InGizmoSelectionBuilder))
	{
		if (GizmoSelectionBuilders.Contains(InGizmoSelectionBuilder) == false)
		{
			DisplayMessage(
				FText::Format(LOCTEXT("DeregisterFailedMessage", "UInteractiveGizmoSubsystem::DeregisterGizmoSelectionType: could not find requested type {0}"), FText::FromName(InGizmoSelectionBuilder->GetFName())),
				EToolMessageLevel::Internal);
			return false;
		}
		GizmoSelectionBuilders.Remove(InGizmoSelectionBuilder);

		return true;
	}

	return false;

}

TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> UEditorInteractiveGizmoManager::GetQualifiedGizmoSelectionBuilders(const FToolBuilderState& InToolBuilderState)
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

	if (!bSearchLocalBuildersOnly)
	{
		UEditorInteractiveGizmoSubsystem* GizmoSubsystem = GEditor->GetEditorSubsystem<UEditorInteractiveGizmoSubsystem>();
		if (ensure(GizmoSubsystem))
		{
			TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> FoundSubsystemBuilders = GizmoSubsystem->GetQualifiedGizmoSelectionBuilders(InToolBuilderState);

			FEditorGizmoTypePriority FoundPriority0 = FoundBuilders.Num() > 0 ? FoundBuilders[0]->GetPriority() : 0;
			FEditorGizmoTypePriority FoundPriority1 = FoundSubsystemBuilders.Num() > 0 ? FoundSubsystemBuilders[0]->GetPriority() : 0;

			if (FoundPriority0 == FoundPriority1)
			{
				FoundBuilders.Append(FoundSubsystemBuilders);
			}
			else if (FoundPriority0 < FoundPriority1)
			{
				FoundBuilders = FoundSubsystemBuilders;
			}
		}
	}

	return FoundBuilders;
}

TArray<UInteractiveGizmo*> UEditorInteractiveGizmoManager::CreateSelectionGizmos(void* Owner)
{
	// always destroy the previous active auto gizmo
	DestroyAllSelectionGizmos();

	if (bShowSelectionGizmos)
	{
		FToolBuilderState CurrentSceneState;
		QueriesAPI->GetCurrentSelectionState(CurrentSceneState);

		if (UTypedElementSelectionSet* SelectionSet = CurrentSceneState.TypedElementSelectionSet.Get())
		{
			if (SelectionSet->HasSelectedElements())
			{
				TArray<UInteractiveGizmo*> NewGizmos;

				TArray<TObjectPtr<UEditorInteractiveGizmoSelectionBuilder>> FoundBuilders = GetQualifiedGizmoSelectionBuilders(CurrentSceneState);

				for (TObjectPtr<UEditorInteractiveGizmoSelectionBuilder> FoundBuilder : FoundBuilders)
				{
					UInteractiveGizmo* NewGizmo = FoundBuilder->BuildGizmo(CurrentSceneState);
					if (NewGizmo == nullptr)
					{
						DisplayMessage(LOCTEXT("CreateGizmoReturnNullMessage", "UEditorInteractiveGizmoManager::CreateGizmo: BuildGizmo() returned null"), EToolMessageLevel::Internal);
						return NewGizmos;
					}

					// register new active input behaviors
					InputRouter->RegisterSource(NewGizmo);

					NewGizmos.Add(NewGizmo);
				}

				PostInvalidation();

				for (UInteractiveGizmo* NewGizmo : NewGizmos)
				{
					FActiveSelectionGizmo ActiveGizmo = { NewGizmo, Owner };
					ActiveSelectionGizmos.Add(ActiveGizmo);
				}

				return NewGizmos;
			}
		}
	}

	return TArray<UInteractiveGizmo*>();
}


bool UEditorInteractiveGizmoManager::DestroySelectionGizmo(UInteractiveGizmo* Gizmo)
{
	auto Pred = [Gizmo](const FActiveSelectionGizmo& ActiveSelectionGizmo) {return ActiveSelectionGizmo.Gizmo == Gizmo; };
	if (!ensure(ActiveSelectionGizmos.FindByPredicate(Pred)))
	{
		return false;
	}

	InputRouter->ForceTerminateSource(Gizmo);

	Gizmo->Shutdown();

	InputRouter->DeregisterSource(Gizmo);

	ActiveSelectionGizmos.RemoveAll(Pred);

	PostInvalidation();

	return true;
}

void UEditorInteractiveGizmoManager::DestroyAllSelectionGizmos()
{
	for (int i = 0; i < ActiveSelectionGizmos.Num(); i++)
	{
		UInteractiveGizmo* Gizmo = ActiveSelectionGizmos[i].Gizmo;
		if (ensure(Gizmo))
		{
			DestroySelectionGizmo(Gizmo);
		}
	}

	ActiveSelectionGizmos.Reset();

	PostInvalidation();
}


void UEditorInteractiveGizmoManager::OnEditorSelectionChanged()
{
	CreateSelectionGizmos();
}

void UEditorInteractiveGizmoManager::OnEditorSelectNone()
{
	DestroyAllSelectionGizmos();
}

// @todo move this to a gizmo context object
bool UEditorInteractiveGizmoManager::GetShowSelectionGizmos()
{
	return bShowSelectionGizmos;
}

bool UEditorInteractiveGizmoManager::GetShowSelectionGizmosForView(IToolsContextRenderAPI* RenderAPI)
{
	const bool bEngineShowFlagsModeWidget = (RenderAPI && RenderAPI->GetSceneView() && 
											 RenderAPI->GetSceneView()->Family &&
											 RenderAPI->GetSceneView()->Family->EngineShowFlags.ModeWidgets);
	return bShowSelectionGizmos && bEngineShowFlagsModeWidget;
}

void UEditorInteractiveGizmoManager::UpdateActiveSelectionGizmos()
{
#if 0
	const bool bEditorModeToolsSupportsWidgetDrawing = EditorModeManager ? EditorModeManager->GetShowWidget() : true;
	const bool bEnableSelectionGizmos = (CVarUseLegacyWidget.GetValueOnGameThread() == 0);
	const bool bNewShowSelectionGizmos = bEditorModeToolsSupportsWidgetDrawing && bEnableSelectionGizmos;
#else
	const bool bNewShowSelectionGizmos = EditorModeManager ? EditorModeManager->GetShowWidget() : true;
#endif

	if (bShowSelectionGizmos != bNewShowSelectionGizmos)
	{
		bShowSelectionGizmos = bNewShowSelectionGizmos;
		if (bShowSelectionGizmos)
		{
			CreateSelectionGizmos();
		}
		else
		{
			DestroyAllSelectionGizmos();
		}
	}
}

void UEditorInteractiveGizmoManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateActiveSelectionGizmos();

	for (FActiveSelectionGizmo& ActiveSelectionGizmo : ActiveSelectionGizmos)
	{
		ActiveSelectionGizmo.Gizmo->Tick(DeltaTime);
	}
}


void UEditorInteractiveGizmoManager::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);

	if (GetShowSelectionGizmosForView(RenderAPI))
	{
		for (FActiveSelectionGizmo& ActiveSelectionGizmo : ActiveSelectionGizmos)
		{
			ActiveSelectionGizmo.Gizmo->Render(RenderAPI);
		}
	}
}

void UEditorInteractiveGizmoManager::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	Super::DrawHUD(Canvas, RenderAPI);

	if (GetShowSelectionGizmosForView(RenderAPI))
	{
		for (FActiveSelectionGizmo& ActiveSelectionGizmo : ActiveSelectionGizmos)
		{
			ActiveSelectionGizmo.Gizmo->DrawHUD(Canvas, RenderAPI);
		}
	}
}

#undef LOCTEXT_NAMESPACE
