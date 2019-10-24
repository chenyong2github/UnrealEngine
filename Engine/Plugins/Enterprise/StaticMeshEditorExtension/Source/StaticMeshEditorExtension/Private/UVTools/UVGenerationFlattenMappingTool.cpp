// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UVGenerationFlattenMappingTool.h"

#include "UVTools/UVGenerationFlattenMapping.h"
#include "UVTools/UVGenerationUtils.h"

#include "AssetData.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Interfaces/IPluginManager.h"
#include "IStaticMeshEditor.h"
#include "MeshEditorUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "UVGenerationFlattenMappingTool"


#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FUVGenerationFlattenMappingToolStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )

class FUVGenerationFlattenMappingToolStyle
{
public:

	static void Initialize()
	{
		if (StyleSet.IsValid())
		{
			return;
		}

		StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

		StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
		StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);

		// Icons for the mode panel tabs
		StyleSet->Set("UVGenerationFlattenMapping.UnwrapUV", new IMAGE_PLUGIN_BRUSH("Icons/UnwrapUV", Icon40x40));
		StyleSet->Set("UVGenerationFlattenMapping.UnwrapUV.Small", new IMAGE_PLUGIN_BRUSH("Icons/UnwrapUV", Icon20x20));
		StyleSet->Set("UVGenerationFlattenMapping.UnwrapUV.Selected", new IMAGE_PLUGIN_BRUSH("Icons/UnwrapUV", Icon40x40));
		StyleSet->Set("UVGenerationFlattenMapping.UnwrapUV.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/UnwrapUV", Icon20x20));

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
	}

	static void Shutdown()
	{
		if (StyleSet.IsValid())
		{
			FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
			ensure(StyleSet.IsUnique());
			StyleSet.Reset();
		}
	}

	static TSharedPtr<ISlateStyle> Get() { return StyleSet; }

	static FName GetStyleSetName()
	{
		static FName StyleName("UVGenerationFlattenMappingToolStyle");
		return StyleName;
	}

	static FString InContent(const FString& RelativePath, const ANSICHAR* Extension)
	{
		static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("StaticMeshEditorExtension"))->GetContentDir();
		return (ContentDir / RelativePath) + Extension;
	}


private:

	static TSharedPtr<FSlateStyleSet> StyleSet;
};

#undef IMAGE_PLUGIN_BRUSH

TSharedPtr<FSlateStyleSet> FUVGenerationFlattenMappingToolStyle::StyleSet = nullptr;


class SUVGenerationFlattenMappingWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUVGenerationFlattenMappingWindow)
	{}
		SLATE_ARGUMENT(TArray<UStaticMesh*>*, StaticMeshes)
		SLATE_ARGUMENT(UUVFlattenMappingSettings*, MappingSettings)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

public:
	static bool DisplayDialog(TArray<UStaticMesh*>* StaticMeshes, UUVFlattenMappingSettings* OutFlattenMappingSettings);

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			return OnCancel();
		}

		return FReply::Unhandled();
	}

	bool GetCanProceed() const	{ return bCanProceed; }

private:	
	FReply OnProceed()
	{
		if (Window.IsValid())
		{
			Window.Pin()->RequestDestroyWindow();
		}
		bCanProceed = true;
		return FReply::Handled();
	}

	FReply OnCancel()
	{
		if (Window.IsValid())
		{
			Window.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

private:
	TArray<UStaticMesh*>* StaticMeshes;
	UUVFlattenMappingSettings* MappingSettings;
	TWeakPtr< SWindow > Window;
	bool bCanProceed;
};



TSharedRef<FExtender> FUVGenerationFlattenMappingTool::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	TArray<UStaticMesh*> StaticMeshes;

	for (const FAssetData& Asset : SelectedAssets)
	{
		if (Asset.AssetClass == UStaticMesh::StaticClass()->GetFName())
		{
			if (UStaticMesh* SelectedStaticMesh = Cast<UStaticMesh>(Asset.GetAsset()))
			{
				StaticMeshes.Add(SelectedStaticMesh);
			}
		}
	}

	FUVGenerationFlattenMappingToolStyle::Initialize();
	if (StaticMeshes.Num() > 0)
	{
		// Add the Datasmith actions sub-menu extender
		Extender->AddMenuExtension("GetAssetActions", EExtensionHook::First, nullptr, FMenuExtensionDelegate::CreateLambda(
			[StaticMeshes](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ObjectContext_UnwrapUV", "Unwrap UV"),
				LOCTEXT("ObjectContext_UnwrapUVTooltip", "Opens Unwrap UV option window"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic( &FUVGenerationFlattenMappingTool::OpenUnwrapUVWindow, StaticMeshes), FCanExecuteAction()));
		}));
	}

	return Extender;
}

void FUVGenerationFlattenMappingTool::OpenUnwrapUVWindow(TArray<UStaticMesh*> StaticMeshes)
{
	TStrongObjectPtr<UUVFlattenMappingSettings> MappingSettings = TStrongObjectPtr<UUVFlattenMappingSettings>(NewObject<UUVFlattenMappingSettings>(GetTransientPackage(), TEXT("Flatten Mapping UV Generation Settings")));
	bool bCancelled = false;

	if (SUVGenerationFlattenMappingWindow::DisplayDialog(&StaticMeshes, MappingSettings.Get()))
	{
		// Save parameters to config file
		MappingSettings->SaveConfig(CPF_Config, *MappingSettings->GetDefaultConfigFilename());

		FScopedTransaction Transaction(LOCTEXT("GenerateUnwrappedUVsTransation", "Generate Unwrapped UVs"));
		FText SlowTaskProgressText(LOCTEXT("UnwrappingUVsSlowTask", "Unwrapping UVs ({0}/{1})"));
		FScopedSlowTask SlowTask(StaticMeshes.Num(), FText::Format(SlowTaskProgressText, 0, StaticMeshes.Num()));
		SlowTask.MakeDialog(true);


		for (UStaticMesh* CurrentStaticMesh : StaticMeshes)
		{
			TMap<FString, FStringFormatArg> ProgressArgs = {
				{TEXT("StaticMeshName"), CurrentStaticMesh->GetName()},
				{TEXT("CurrentProgress"), FString::FromInt(SlowTask.CompletedWork + 1)},
				{TEXT("TotalProgression"), FString::FromInt(SlowTask.TotalAmountOfWork)},
			};

			SlowTask.EnterProgressFrame(1, FText::Format(SlowTaskProgressText, SlowTask.CompletedWork + 1, SlowTask.TotalAmountOfWork));
			UUVGenerationFlattenMapping::GenerateFlattenMappingUVs(CurrentStaticMesh, MappingSettings->UVChannel, MappingSettings->AngleThreshold, MappingSettings->AreaWeight);

			if (SlowTask.ShouldCancel())
			{
				bCancelled = true;
				break;
			}
		}
	}

	if (bCancelled)
	{
		//The operation was aborted, we revert the transaction with bCanRedo = false.
		GEditor->UndoTransaction(false);
	}
}

bool SUVGenerationFlattenMappingWindow::DisplayDialog(TArray<UStaticMesh*>* StaticMeshes, UUVFlattenMappingSettings* OutFlattenMappingSettings)
{
	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SUVGenerationFlattenMappingWindow_Title", "Generate Unwrapped UV"))
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SUVGenerationFlattenMappingWindow> ParameterWindow;
	Window->SetContent
	(
		SAssignNew(ParameterWindow, SUVGenerationFlattenMappingWindow)
		.StaticMeshes(StaticMeshes)
		.MappingSettings(OutFlattenMappingSettings)
		.WidgetWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return ParameterWindow->GetCanProceed();
}

void SUVGenerationFlattenMappingWindow::Construct(const FArguments& InArgs)
{
	StaticMeshes = InArgs._StaticMeshes;
	MappingSettings = InArgs._MappingSettings;
	Window = InArgs._WidgetWindow;
	bCanProceed = false;

	TSharedPtr<SBox> DetailsViewBox;
	
	ChildSlot
	[
		SNew(SVerticalBox)
		
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(DetailsViewBox, SBox)
			//.MaxDesiredHeight(320.0f)
			.MaxDesiredWidth(450.0f)
		]

		+ SVerticalBox::Slot()
		.HAlign(HAlign_Right)
		.AutoHeight()
		.Padding(2)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(8.0f, 0.0f, 8.0f, 0.0f))
			
			+ SUniformGridPanel::Slot(0, 0)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("SUVGenerationFlattenMappingWindow_Proceed", "Proceed"))
				.OnClicked(this, &SUVGenerationFlattenMappingWindow::OnProceed)
			]

			+ SUniformGridPanel::Slot(1, 0)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("SUVGenerationFlattenMappingWindow_Cancel", "Cancel"))
				.OnClicked(this, &SUVGenerationFlattenMappingWindow::OnCancel)
			]
		]
	];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	DetailsView->SetObject(MappingSettings);
	DetailsViewBox->SetContent(DetailsView.ToSharedRef());
}

void FUVGenerationFlattenMappingToolbar::CreateMenu(FMenuBuilder& ParentMenuBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* InStaticMesh)
{
	FUIAction GenerateUnwrappedUVMenuAction;
	GenerateUnwrappedUVMenuAction.ExecuteAction.BindLambda([InStaticMesh]()
	{
		IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(InStaticMesh, false);
		if (!EditorInstance || !EditorInstance->GetEditorName().ToString().Contains(TEXT("StaticMeshEditor")))
		{
			return;
		}
		FUVGenerationFlattenMappingTool::OpenUnwrapUVWindow({ InStaticMesh });
		static_cast<IStaticMeshEditor*>(EditorInstance)->RefreshTool();
	});
	ParentMenuBuilder.AddMenuEntry(
		LOCTEXT("UnwrapUV", "Unwrap UV"),
		LOCTEXT("UnwrapUVTooltip", "Opens the UV unwrapping window"),
		FSlateIcon(),
		GenerateUnwrappedUVMenuAction
	);
}

#undef LOCTEXT_NAMESPACE
