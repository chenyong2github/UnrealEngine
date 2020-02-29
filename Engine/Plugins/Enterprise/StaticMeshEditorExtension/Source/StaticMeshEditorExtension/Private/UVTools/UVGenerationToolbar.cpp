// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVGenerationToolbar.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "SUVGenerationTool.h"
#include "IStaticMeshEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "UVGenerationToolbar"

static const FName UVGenerationToolTabId(TEXT("UVGenerationTool"));

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FUVGenerationToolStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
class FUVGenerationToolStyle
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

		const FVector2D Icon16x16(16.0f, 16.0f);

		// Icons for the toolbar and panel tab
		StyleSet->Set("UVGenerationTool.Tabs.GenerateUV", new IMAGE_PLUGIN_BRUSH("Icons/icon_UVGenerationTool_Generate_UV_16x", Icon16x16));

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
		static FName StyleName("UVGenerationToolStyle");
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

TSharedPtr<FSlateStyleSet> FUVGenerationToolStyle::StyleSet = nullptr;

FUVGenerationToolbar::~FUVGenerationToolbar()
{
	if (StaticMeshEditorPtr.IsValid())
	{
		TSharedPtr<IStaticMeshEditor> StaticMeshEditorSharedPtr = StaticMeshEditorPtr.Pin();
		StaticMeshEditorSharedPtr->OnStaticMeshEditorClosed().Remove(OnStaticMeshEditorClosedHandle);
	}
}

void FUVGenerationToolbar::CreateUVMenu(FMenuBuilder& ParentMenuBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* StaticMesh)
{
	FUIAction GenerateUVMenuAction;
	GenerateUVMenuAction.ExecuteAction.BindLambda([StaticMesh]()
	{
		IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(StaticMesh, false);
		if (!EditorInstance || !EditorInstance->GetEditorName().ToString().Contains(TEXT("StaticMeshEditor")))
		{
			return;
		}
		EditorInstance->GetAssociatedTabManager()->TryInvokeTab(UVGenerationToolTabId);
	});
	ParentMenuBuilder.AddMenuEntry(
		LOCTEXT("GenerateUVs", "Generate UVs"),
		LOCTEXT("GenerateUVsTooltip", "Open the UVs generation window"),
		FSlateIcon(),
		GenerateUVMenuAction,
		NAME_None,
		EUserInterfaceActionType::Button
	);
}

void FUVGenerationToolbar::CreateTool(TWeakPtr<IStaticMeshEditor> InStaticMeshEditor)
{
	TSharedPtr<FUVGenerationToolbar> UVGenerationTool = MakeShared<FUVGenerationToolbar>();
	UVGenerationTool->Initialize(InStaticMeshEditor);
}

bool FUVGenerationToolbar::Initialize(const TWeakPtr<IStaticMeshEditor>& InStaticMeshEditor)
{
	StaticMeshEditorPtr = InStaticMeshEditor;

	if (StaticMeshEditorPtr.IsValid())
	{
		TSharedPtr<IStaticMeshEditor> StaticMeshEditorSharedPtr = StaticMeshEditorPtr.Pin();
		TSharedRef<FWorkspaceItem> LocalWorkspace = StaticMeshEditorSharedPtr->GetWorkspaceMenuCategory();
		FUVGenerationToolStyle::Initialize();

		SAssignNew(UVGenerationTab, SGenerateUV)
			.UVGenerationTool(SharedThis(this))
			.StaticMeshEditorPtr(StaticMeshEditorPtr);

		OnStaticMeshEditorClosedHandle = StaticMeshEditorSharedPtr->OnStaticMeshEditorClosed().AddSP(this, &FUVGenerationToolbar::OnCloseStaticMeshEditor);
		StaticMeshEditorSharedPtr->OnStaticMeshEditorDockingExtentionTabs().AddSP(this, &FUVGenerationToolbar::DockStaticMeshEditorExtensionTabs);
		StaticMeshEditorSharedPtr->OnRegisterTabSpawners().AddSP(this, &FUVGenerationToolbar::RegisterStaticMeshEditorTabs);
		StaticMeshEditorSharedPtr->OnUnregisterTabSpawners().AddSP(this, &FUVGenerationToolbar::UnregisterStaticMeshEditorTabs);
		return true;
	}

	return false;
}

void FUVGenerationToolbar::OnCloseStaticMeshEditor()
{
	//Break the self referencing loop, everything will be deleted.
	UVGenerationTab.Reset();
}

void FUVGenerationToolbar::DockStaticMeshEditorExtensionTabs(const TSharedRef<FTabManager::FStack>& TabStack)
{
	TabStack->AddTab(UVGenerationToolTabId, ETabState::ClosedTab);
}

void FUVGenerationToolbar::RegisterStaticMeshEditorTabs(const TSharedRef<FTabManager>& TabManager)
{
	if (StaticMeshEditorPtr.IsValid())
	{
		TSharedPtr<IStaticMeshEditor> StaticMeshEditorSharedPtr = StaticMeshEditorPtr.Pin();
		TabManager->RegisterTabSpawner(UVGenerationToolTabId, FOnSpawnTab::CreateSP(this, &FUVGenerationToolbar::Spawn_UVGenerationToolTab))
			.SetDisplayName(LOCTEXT("GenerateUVTab", "Generate UV"))
			.SetGroup(StaticMeshEditorSharedPtr->GetWorkspaceMenuCategory())
			.SetIcon(FSlateIcon(FUVGenerationToolStyle::GetStyleSetName(), "UVGenerationTool.Tabs.GenerateUV"));
	}
}


void FUVGenerationToolbar::UnregisterStaticMeshEditorTabs(const TSharedRef<FTabManager>& TabManager)
{
	TabManager->UnregisterTabSpawner(UVGenerationToolTabId);
}


TSharedRef<SDockTab> FUVGenerationToolbar::Spawn_UVGenerationToolTab(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == UVGenerationToolTabId);

	UVGenerationTab->SetNextValidTargetChannel();
	return SNew(SDockTab)
		.Label(LOCTEXT("StaticMeshGenerateUV_TabTitle", "Generate UV"))
		[
			UVGenerationTab.ToSharedRef()
		];
}

#undef IMAGE_PLUGIN_BRUSH
#undef LOCTEXT_NAMESPACE