// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomCreateBindingOptionsWindow.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "GroomCreateBindingOptions.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "GroomCreateBindingOptionsWindow"

void SGroomCreateBindingOptionsWindow::Construct(const FArguments& InArgs)
{
	BindingOptions = InArgs._BindingOptions;
	WidgetWindow = InArgs._WidgetWindow;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(BindingOptions);

	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("CurrentFile", "Current File: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(InArgs._FullPath)
				]
			]
		]

		+ SVerticalBox::Slot()
		.Padding(2)
		.MaxHeight(500.0f)
		[
			DetailsView->AsShared()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(2)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(2)
			+ SUniformGridPanel::Slot(0, 0)
			[
				SAssignNew(ImportButton, SButton)
				.HAlign(HAlign_Center)
				.Text(InArgs._ButtonLabel)
				.IsEnabled(this, &SGroomCreateBindingOptionsWindow::CanCreateBinding)
				.OnClicked(this, &SGroomCreateBindingOptionsWindow::OnCreateBinding)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SGroomCreateBindingOptionsWindow::OnCancel)
			]
		]
	];
}

bool SGroomCreateBindingOptionsWindow::CanCreateBinding()  const
{
	return true;
}

enum class EGroomBindingOptionsVisibility : uint8
{
	None = 0x00,
	ConversionOptions = 0x01,
	BuildOptions = 0x02,
	All = ConversionOptions | BuildOptions
};

ENUM_CLASS_FLAGS(EGroomBindingOptionsVisibility);

TSharedPtr<SGroomCreateBindingOptionsWindow> DisplayOptions(UGroomCreateBindingOptions* BindingOptions, EGroomBindingOptionsVisibility VisibilityFlag, FText WindowTitle, FText InButtonLabel)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SGroomCreateBindingOptionsWindow> OptionsWindow;

	Window->SetContent
	(
		SAssignNew(OptionsWindow, SGroomCreateBindingOptionsWindow)
		.BindingOptions(BindingOptions)
		.WidgetWindow(Window)
//		.FullPath(FText::FromString(FileName))
		.ButtonLabel(InButtonLabel)
	);

	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return OptionsWindow;
}

TSharedPtr<SGroomCreateBindingOptionsWindow> SGroomCreateBindingOptionsWindow::DisplayCreateBindingOptions(UGroomCreateBindingOptions* BindingOptions)
{
	return DisplayOptions(BindingOptions, EGroomBindingOptionsVisibility::BuildOptions, LOCTEXT("GroomBindingRebuildWindowTitle", "Groom Binding Options"), LOCTEXT("Build", "Create"));
}

static UObject* InternalCreateNewBindAsset(FName InAssetName, UObject* InParent, UGroomAsset* GroomAsset, USkeletalMesh* SourceSkelMesh, USkeletalMesh* TargetSkelMesh, const int32 NumInterpolationPoints)
{
	if (!TargetSkelMesh)
	{
		return nullptr;
	}

	FString Name = InAssetName.ToString();
	FString PackageName;

	if (InAssetName == NAME_None)
	{
		FString Suffix;
		if (SourceSkelMesh)
		{
			Suffix += TEXT("_") + SourceSkelMesh->GetName();
		}
		if (TargetSkelMesh)
		{
			Suffix += TEXT("_") + TargetSkelMesh->GetName();
		}
		Suffix += TEXT("_Binding");

		// Get a unique package and asset name
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(GroomAsset->GetOutermost()->GetName(), Suffix, PackageName, Name);
	}

	UPackage* Package = Cast<UPackage>(InParent);
	if (InParent == nullptr && !PackageName.IsEmpty())
	{
		// Then find/create it.
		Package = CreatePackage(nullptr, *PackageName);
		if (!ensure(Package))
		{
			// There was a problem creating the package
			return nullptr;
		}
	}
	  
	if (UGroomBindingAsset* NewAsset = NewObject<UGroomBindingAsset>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional))
	{
		NewAsset->Groom = GroomAsset;
		NewAsset->SourceSkeletalMesh = SourceSkelMesh;
		NewAsset->TargetSkeletalMesh = TargetSkelMesh;
		NewAsset->HairGroupDatas.Reserve(GroomAsset->HairGroupsData.Num());
		NewAsset->NumInterpolationPoints = NumInterpolationPoints;
		NewAsset->MarkPackageDirty();

		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(NewAsset);
		return NewAsset;
	}

	return nullptr;
}

UGroomBindingAsset* CreateGroomBindinAsset(UGroomAsset* GroomAsset, USkeletalMesh* SourceSkelMesh, USkeletalMesh* TargetSkelMesh, const int32 NumInterpolationPoints)
{
	if (!GroomAsset || !TargetSkelMesh)
	{
		return nullptr;
	}

	UObject* BindingAsset = InternalCreateNewBindAsset(NAME_None, nullptr, GroomAsset, SourceSkelMesh, TargetSkelMesh, NumInterpolationPoints);

	if (BindingAsset)
	{
		TArray<UObject*> CreatedObjects;
		CreatedObjects.Add(BindingAsset);

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(CreatedObjects);
	#if WITH_EDITOR
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(CreatedObjects);
	#endif
	}

	return (UGroomBindingAsset*) BindingAsset;
}

#undef LOCTEXT_NAMESPACE
