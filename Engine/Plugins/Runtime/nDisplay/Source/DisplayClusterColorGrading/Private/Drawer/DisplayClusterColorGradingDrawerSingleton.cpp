// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingDrawerSingleton.h"

#include "DisplayClusterColorGradingStyle.h"

#include "DisplayClusterRootActor.h"

#include "IDisplayClusterOperator.h"
#include "IDisplayClusterOperatorViewModel.h"
#include "DisplayClusterOperatorStatusBarExtender.h"
#include "Drawer/SDisplayClusterColorGradingDrawer.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

const FName FDisplayClusterColorGradingDrawerSingleton::ColorGradingDrawerId = TEXT("ColorGradingDrawer");
const FName FDisplayClusterColorGradingDrawerSingleton::ColorGradingDrawerTab = TEXT("ColorGradingDrawerTab");

FDisplayClusterColorGradingDrawerSingleton::FDisplayClusterColorGradingDrawerSingleton()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ColorGradingDrawerTab, FOnSpawnTab::CreateRaw(this, &FDisplayClusterColorGradingDrawerSingleton::SpawnColorGradingDrawerTab))
		.SetDisplayName(LOCTEXT("ColorGradingDarwerTab_DisplayName", "In-Camera VFX"))
		.SetTooltipText(LOCTEXT("ColorGradingDarwerTab_Tooltip", "Editing tools for in-camera VFX."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	IDisplayClusterOperator::Get().OnRegisterLayoutExtensions().AddRaw(this, &FDisplayClusterColorGradingDrawerSingleton::ExtendOperatorTabLayout);
	IDisplayClusterOperator::Get().OnRegisterStatusBarExtensions().AddRaw(this, &FDisplayClusterColorGradingDrawerSingleton::ExtendOperatorStatusBar);
}

FDisplayClusterColorGradingDrawerSingleton::~FDisplayClusterColorGradingDrawerSingleton()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ColorGradingDrawerTab);
	}
}

void FDisplayClusterColorGradingDrawerSingleton::DockColorGradingDrawer()
{
	if (TSharedPtr<FTabManager> OperatorPanelTabManager = IDisplayClusterOperator::Get().GetOperatorViewModel()->GetTabManager())
	{
		if (TSharedPtr<SDockTab> ExistingTab = OperatorPanelTabManager->FindExistingLiveTab(ColorGradingDrawerTab))
		{
			IDisplayClusterOperator::Get().ForceDismissDrawers();
			ExistingTab->ActivateInParent(ETabActivationCause::SetDirectly);
		}
		else
		{
			OperatorPanelTabManager->TryInvokeTab(ColorGradingDrawerTab);
		}
	}
}

void FDisplayClusterColorGradingDrawerSingleton::RefreshColorGradingDrawers(bool bPreserveDrawerState)
{
	if (ColorGradingDrawer.IsValid())
	{
		ColorGradingDrawer.Pin()->Refresh(bPreserveDrawerState);
	}

	if (TSharedPtr<FTabManager> OperatorPanelTabManager = IDisplayClusterOperator::Get().GetOperatorViewModel()->GetTabManager())
	{
		if (TSharedPtr<SDockTab> ExistingTab = OperatorPanelTabManager->FindExistingLiveTab(ColorGradingDrawerTab))
		{
			TSharedRef<SDisplayClusterColorGradingDrawer> DockedDrawer = StaticCastSharedRef<SDisplayClusterColorGradingDrawer>(ExistingTab->GetContent());
			DockedDrawer->Refresh(bPreserveDrawerState);
		}
	}
}

TSharedRef<SWidget> FDisplayClusterColorGradingDrawerSingleton::CreateDrawerContent(bool bIsInDrawer, bool bCopyStateFromActiveDrawer)
{
	if (bIsInDrawer)
	{
		TSharedPtr<SDisplayClusterColorGradingDrawer> Drawer = ColorGradingDrawer.IsValid() ? ColorGradingDrawer.Pin() : nullptr;

		if (!Drawer.IsValid())
		{
			Drawer = SNew(SDisplayClusterColorGradingDrawer, true);
			ColorGradingDrawer = Drawer;
		}

		if (PreviousDrawerState.IsSet())
		{
			ColorGradingDrawer.Pin()->SetDrawerState(PreviousDrawerState.GetValue());
			PreviousDrawerState.Reset();
		}

		return Drawer.ToSharedRef();
	}
	else
	{
		TSharedRef<SDisplayClusterColorGradingDrawer> NewDrawer = SNew(SDisplayClusterColorGradingDrawer, false);

		if (bCopyStateFromActiveDrawer && ColorGradingDrawer.IsValid())
		{
			NewDrawer->SetDrawerState(ColorGradingDrawer.Pin()->GetDrawerState());
		}

		return NewDrawer;
	}
}

TSharedRef<SDockTab> FDisplayClusterColorGradingDrawerSingleton::SpawnColorGradingDrawerTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	MajorTab->SetContent(CreateDrawerContent(false, true));

	return MajorTab;
}

void FDisplayClusterColorGradingDrawerSingleton::ExtendOperatorTabLayout(FLayoutExtender& InExtender)
{
	FTabManager::FTab NewTab(FTabId(ColorGradingDrawerTab, ETabIdFlags::SaveLayout), ETabState::ClosedTab);
	InExtender.ExtendStack(IDisplayClusterOperator::Get().GetAuxilliaryOperatorExtensionId(), ELayoutExtensionPosition::After, NewTab);
}

void FDisplayClusterColorGradingDrawerSingleton::ExtendOperatorStatusBar(FDisplayClusterOperatorStatusBarExtender& StatusBarExtender)
{
	FWidgetDrawerConfig ColorGradingDrawerConfig(ColorGradingDrawerId);

	ColorGradingDrawerConfig.GetDrawerContentDelegate.BindRaw(this, &FDisplayClusterColorGradingDrawerSingleton::CreateDrawerContent, true, false);
	ColorGradingDrawerConfig.OnDrawerDismissedDelegate.BindRaw(this, &FDisplayClusterColorGradingDrawerSingleton::SaveDrawerState);
	ColorGradingDrawerConfig.ButtonText = LOCTEXT("ColorGradingDrawer_ButtonText", "In-Camera VFX");
	ColorGradingDrawerConfig.Icon = FDisplayClusterColorGradingStyle::Get().GetBrush("ColorGradingDrawer.Icon");

	StatusBarExtender.AddWidgetDrawer(ColorGradingDrawerConfig);
}

void FDisplayClusterColorGradingDrawerSingleton::SaveDrawerState(const TSharedPtr<SWidget>& DrawerContent)
{
	if (ColorGradingDrawer.IsValid())
	{
		PreviousDrawerState = ColorGradingDrawer.Pin()->GetDrawerState();
	}
	else
	{
		PreviousDrawerState.Reset();
	}
}

#undef LOCTEXT_NAMESPACE