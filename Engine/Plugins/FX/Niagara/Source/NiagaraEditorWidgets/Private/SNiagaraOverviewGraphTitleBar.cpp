// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewGraphTitleBar.h"

#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraSettings.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "SNiagaraScalabilityPreviewSettings.h"
#include "SNiagaraSystemEffectTypeBar.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewGraphTitleBar"

SNiagaraOverviewGraphTitleBar::~SNiagaraOverviewGraphTitleBar()
{
	ClearListeners();
}

void SNiagaraOverviewGraphTitleBar::Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel, const FAssetData& InEditedAsset)
{
	SystemViewModel = InSystemViewModel;
	EditedAsset = InEditedAsset;

	bScalabilityModeActive = SystemViewModel.Pin()->GetScalabilityViewModel()->IsActive();
	SystemViewModel.Pin()->GetScalabilityViewModel()->OnScalabilityModeChanged().AddSP(this, &SNiagaraOverviewGraphTitleBar::OnUpdateScalabilityMode);

	AddAssetListeners();
	RebuildWidget();
}

void SNiagaraOverviewGraphTitleBar::RebuildWidget()
{
	ContainerWidget = SNew(SVerticalBox);

	// we only allow the Niagara system effect type bar on system assets
	if (bScalabilityModeActive && SystemViewModel.IsValid() && SystemViewModel.Pin()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		ContainerWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.TitleBackground")))
			.HAlign(HAlign_Fill)
			[
				SNew(SNiagaraSystemEffectTypeBar, SystemViewModel.Pin()->GetSystem())
			]
		];
	}
	
	ContainerWidget->AddSlot()
	.HAlign(HAlign_Fill)
	.AutoHeight()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Graph.TitleBackground")))
		.HAlign(HAlign_Fill)
		[
			SNew(STextBlock)
			.Text(SystemViewModel.Pin()->GetOverviewGraphViewModel().Get(), &FNiagaraOverviewGraphViewModel::GetDisplayName)
			.TextStyle(FAppStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
			.Justification(ETextJustify::Center)
		]
	];

	if (SystemViewModel.IsValid() && SystemViewModel.Pin()->GetEditMode() == ENiagaraSystemViewModelEditMode::EmitterAsset)
	{
		ContainerWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.TitleBackground")))
			.ColorAndOpacity(this, &SNiagaraOverviewGraphTitleBar::GetEmitterSubheaderColor)
			.Visibility(this, &SNiagaraOverviewGraphTitleBar::GetEmitterSubheaderVisibility)
			.HAlign(HAlign_Fill)
			[
				SNew(STextBlock)
				.Text(this, &SNiagaraOverviewGraphTitleBar::GetEmitterSubheaderText)
				.TextStyle(FAppStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
				.Justification(ETextJustify::Center)
			]
		];
	}
	
	if(bScalabilityModeActive)
	{
		ContainerWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.Padding(5.f)
		.AutoHeight()
		[			
			SNew(SNiagaraScalabilityPreviewSettings, *SystemViewModel.Pin()->GetScalabilityViewModel()).Visibility(EVisibility::SelfHitTestInvisible)			
		];
	}

	ChildSlot
	[
		ContainerWidget.ToSharedRef()
	];
}

void SNiagaraOverviewGraphTitleBar::OnUpdateScalabilityMode(bool bActive)
{
	bScalabilityModeActive = bActive;
	RebuildWidget();
}

FText SNiagaraOverviewGraphTitleBar::GetEmitterSubheaderText() const
{
	return FText::Format(LOCTEXT("EmitterSubheaderText", "Note: editing this emitter will affect {0} child emitters and systems (across all versions)!"), GetEmitterAffectedAssets());
}

EVisibility SNiagaraOverviewGraphTitleBar::GetEmitterSubheaderVisibility() const
{
	return GetEmitterAffectedAssets() > 1 ? EVisibility::Visible : EVisibility::Collapsed;
}

FLinearColor SNiagaraOverviewGraphTitleBar::GetEmitterSubheaderColor() const
{
	return GetEmitterAffectedAssets() >= 5 ? FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.SystemOverview.AffectedAssetsWarningColor") : FLinearColor::White;
}

void SNiagaraOverviewGraphTitleBar::ResetAssetCount(const FAssetData&)
{
	EmitterAffectedAssets.Reset();
}

void SNiagaraOverviewGraphTitleBar::AddAssetListeners()
{
	if (EditedAsset.IsValid())
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.OnAssetUpdated().AddRaw(this, &SNiagaraOverviewGraphTitleBar::ResetAssetCount);
		AssetRegistry.OnAssetAdded().AddRaw(this, &SNiagaraOverviewGraphTitleBar::ResetAssetCount);
		AssetRegistry.OnAssetRemoved().AddRaw(this, &SNiagaraOverviewGraphTitleBar::ResetAssetCount);
	}
}

void SNiagaraOverviewGraphTitleBar::ClearListeners()
{
	if (EditedAsset.IsValid())
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.OnAssetUpdated().RemoveAll(this);
		AssetRegistry.OnAssetAdded().RemoveAll(this);
		AssetRegistry.OnAssetRemoved().RemoveAll(this);
	}
}

int32 SNiagaraOverviewGraphTitleBar::GetEmitterAffectedAssets() const
{
	if (!EditedAsset.IsValid())
	{
		return 0;
	}
	
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	if (Settings->bDisplayAffectedAssetStatsInEditor == false)
	{
		return 0;
	}
	
	if (!EmitterAffectedAssets.IsSet())
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			// We are still discovering assets, wait a bit
			return 0;
		}

		int32 Count = 0;
		if (EditedAsset.IsValid())
		{
			TSet<FName> SeenObjects;
			TArray<FAssetData> AssetsToCheck;
			AssetsToCheck.Add(EditedAsset);

			// search for assets that reference this emitter
			while (AssetsToCheck.Num() > 0)
			{
				FAssetData AssetToCheck = AssetsToCheck[0];
				AssetsToCheck.RemoveAtSwap(0);
				if (SeenObjects.Contains(AssetToCheck.ObjectPath))
				{
					continue;
				}
				SeenObjects.Add(AssetToCheck.ObjectPath);

				if (AssetToCheck.GetClass() == UNiagaraEmitter::StaticClass())
				{
					Count++;
					TArray<FName> Referencers;
					AssetRegistry.GetReferencers(AssetToCheck.PackageName, Referencers);
					for (FName& Referencer : Referencers)
					{
						AssetRegistry.GetAssetsByPackageName(Referencer, AssetsToCheck);
					}
				}
				else if (AssetToCheck.GetClass() == UNiagaraSystem::StaticClass())
				{
					Count++;
				}
			}
			Count--; // remove one for our own asset
		}
		EmitterAffectedAssets = Count;
	}
	return EmitterAffectedAssets.Get(0);
}

#undef LOCTEXT_NAMESPACE
