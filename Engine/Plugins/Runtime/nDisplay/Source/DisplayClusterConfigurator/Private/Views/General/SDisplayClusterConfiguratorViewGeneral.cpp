// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/General/SDisplayClusterConfiguratorViewGeneral.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewGeneral"


SDisplayClusterConfiguratorViewGeneral::SDisplayClusterConfiguratorViewGeneral()
	: bRefreshOnTick(false)
{
}

SDisplayClusterConfiguratorViewGeneral::~SDisplayClusterConfiguratorViewGeneral()
{
}

void SDisplayClusterConfiguratorViewGeneral::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	ToolkitPtr = InToolkit;

	// Delegates
	InToolkit->RegisterOnConfigReloaded(IDisplayClusterConfiguratorToolkit::FOnConfigReloadedDelegate::CreateSP(this, &SDisplayClusterConfiguratorViewGeneral::OnConfigReloaded));

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;

	PropertyView = EditModule.CreateDetailView(DetailsViewArgs);

	SDisplayClusterConfiguratorViewBase::Construct(
		SDisplayClusterConfiguratorViewBase::FArguments()
		.Padding(0.0f)
		.Content()
		[
			PropertyView.ToSharedRef()
		],
		InToolkit);
}

void SDisplayClusterConfiguratorViewGeneral::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshOnTick)
	{
		UpdateFromObjects(RefreshPropertyObjects);
		RefreshPropertyObjects.Empty();
		bRefreshOnTick = false;
	}
}

void SDisplayClusterConfiguratorViewGeneral::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
}

void SDisplayClusterConfiguratorViewGeneral::ShowDetailsObjects(const TArray<UObject*>& Objects)
{
	bRefreshOnTick = true;
	RefreshPropertyObjects.Empty();

	RefreshPropertyObjects.Append(Objects);
}

const TArray<UObject*>& SDisplayClusterConfiguratorViewGeneral::GetSelectedObjects() const
{
	return ToolkitPtr.Pin()->GetSelectedObjects();
}

void SDisplayClusterConfiguratorViewGeneral::UpdateFromObjects(const TArray<UObject*>& PropertyObjects)
{
	PropertyView->SetObjects(PropertyObjects);
}

void SDisplayClusterConfiguratorViewGeneral::OnConfigReloaded()
{
	TArray<UObject*> SelectedObjects;
	SelectedObjects.Add(ToolkitPtr.Pin()->GetConfig());

	UpdateFromObjects(SelectedObjects);
}

#undef LOCTEXT_NAMESPACE
