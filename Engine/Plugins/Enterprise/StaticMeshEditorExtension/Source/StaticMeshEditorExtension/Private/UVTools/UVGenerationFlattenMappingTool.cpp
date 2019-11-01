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
#include "MeshDescriptionOperations.h"
#include "MeshEditorUtils.h"
#include "MeshUtilitiesCommon.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "OverlappingCorners.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogUVUnwrapping, Log, All);

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
		SLATE_ARGUMENT(UUVUnwrapSettings*, UnwrapSettings)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

public:
	static bool DisplayDialog(TArray<UStaticMesh*>* StaticMeshes, UUVUnwrapSettings* OutUVUnwrapSettings);

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
	UUVUnwrapSettings* UnwrapSettings;
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
	TStrongObjectPtr<UUVUnwrapSettings> UVUnwrapSettings = TStrongObjectPtr<UUVUnwrapSettings>(NewObject<UUVUnwrapSettings>(GetTransientPackage(), TEXT("Flatten Mapping UV Generation Settings")));
	bool bCancelled = false;

	if (SUVGenerationFlattenMappingWindow::DisplayDialog(&StaticMeshes, UVUnwrapSettings.Get()))
	{
		// Save parameters to config file
		UVUnwrapSettings->SaveConfig(CPF_Config, *UVUnwrapSettings->GetDefaultConfigFilename());

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
			CurrentStaticMesh->Modify();

			for (int32 LODIndex = 0; LODIndex < CurrentStaticMesh->GetNumSourceModels(); ++LODIndex)
			{
				int32 UVChannel = -1;

				if (SetupMeshForUVGeneration(CurrentStaticMesh, UVUnwrapSettings.Get(), LODIndex, UVChannel))
				{
					UUVGenerationFlattenMapping::GenerateFlattenMappingUVs(CurrentStaticMesh, UVChannel, UVUnwrapSettings->AngleThreshold);

					if (UVUnwrapSettings->ChannelSelection == EUnwrappedUVChannelSelection::AutomaticLightmapSetup)
					{
						UVGenerationUtils::SetupGeneratedLightmapUVResolution(CurrentStaticMesh, LODIndex);
					}
					
					UStaticMesh::FCommitMeshDescriptionParams CommitMeshDescriptionParam;
					CommitMeshDescriptionParam.bUseHashAsGuid = true;
					CurrentStaticMesh->CommitMeshDescription(LODIndex, CommitMeshDescriptionParam);
				}
				else
				{
					UE_LOG(LogUVUnwrapping, Error, TEXT("Could not generate unwrapped UV at the specified channel for static mesh %s"), *CurrentStaticMesh->GetName());
					break;
				}
			}

			CurrentStaticMesh->PostEditChange();

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

bool FUVGenerationFlattenMappingTool::SetupMeshForUVGeneration(UStaticMesh* StaticMesh, const UUVUnwrapSettings* UVUnwrapSettings, int32 LODIndex, int32& OutChannelIndex)
{
	OutChannelIndex = -1;

	if (UVUnwrapSettings->ChannelSelection == EUnwrappedUVChannelSelection::SpecifyChannel)
	{
		if (UVUnwrapSettings->UVChannel >= 0 && UVUnwrapSettings->UVChannel < MAX_MESH_TEXTURE_COORDS_MD)
		{
			OutChannelIndex = UVUnwrapSettings->UVChannel;
		}
	}
	else if (UVUnwrapSettings->ChannelSelection == EUnwrappedUVChannelSelection::AutomaticLightmapSetup)
	{
		FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;

		if (!BuildSettings.bGenerateLightmapUVs)
		{
			//If the lightmap generation was deactivated we change source and destination indexes to the first empty slot.
			int32 FirstOpenChannel = UVGenerationUtils::GetNextOpenUVChannel(StaticMesh, LODIndex);

			if (FirstOpenChannel >= 0)
			{
				BuildSettings.SrcLightmapIndex = FirstOpenChannel;
				BuildSettings.DstLightmapIndex = FirstOpenChannel;
				OutChannelIndex = FirstOpenChannel;

				if (LODIndex == 0)
				{
					//If we are setting up the first LOD make sure the mesh lightmap coordinate points to the generated ones.
					StaticMesh->LightMapCoordinateIndex = FirstOpenChannel;
				}

				BuildSettings.bGenerateLightmapUVs = true;
			}
		}
		else
		{
			OutChannelIndex = BuildSettings.SrcLightmapIndex;
		}
	}
	else if ( UVUnwrapSettings->ChannelSelection == EUnwrappedUVChannelSelection::FirstEmptyChannel )
	{
		int32 FirstOpenChannel = UVGenerationUtils::GetNextOpenUVChannel(StaticMesh, LODIndex);

		if (FirstOpenChannel >= 0)
		{
			OutChannelIndex = FirstOpenChannel;
		}
	}

	return OutChannelIndex >= 0;
}

bool SUVGenerationFlattenMappingWindow::DisplayDialog(TArray<UStaticMesh*>* StaticMeshes, UUVUnwrapSettings* OutUVUnwrapSettings)
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
		.UnwrapSettings(OutUVUnwrapSettings)
		.WidgetWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return ParameterWindow->GetCanProceed();
}

void SUVGenerationFlattenMappingWindow::Construct(const FArguments& InArgs)
{
	StaticMeshes = InArgs._StaticMeshes;
	UnwrapSettings = InArgs._UnwrapSettings;
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

	DetailsView->SetObject(UnwrapSettings);
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
