// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "ILevelSnapshotsEditorView.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"
#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "AssetRegistryModule.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "LevelSnapshot.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboButton.h"

#include "LevelSnapshotsLog.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

class SLevelSnapshotsEditorContextPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectWorldContext, FSoftObjectPath);

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorContextPicker) {}

	/** Attribute for retrieving the current context */
	SLATE_ATTRIBUTE(FSoftObjectPath, SelectWorldPath)
	
	/** Called when the user explicitly chooses a new context world. */
	SLATE_EVENT(FOnSelectWorldContext, OnSelectWorldContext)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	UWorld* GetSelectedWorld() const
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		
		return Cast<UWorld>(AssetRegistryModule.Get().GetAssetByObjectPath(FName(SelectedWorldPath.GetAssetPathString())).GetAsset());
	}

	FSoftObjectPath GetSelectedWorldSoftPath()
	{
		return SelectedWorldPath;
	}

private:

	TSharedRef<SWidget> BuildWorldPickerMenu();

	static FText GetWorldDescription(UWorld* World);

	FText GetWorldPickerMenuButtonText(const FSoftObjectPath& AssetPath, const FName& AssetName) const
	{
		const bool bDoesAssetPointToEditorWorld = AssetPath == FSoftObjectPath(SLevelSnapshotsEditorInput::GetEditorWorld());

		if (bDoesAssetPointToEditorWorld)
		{
			const FText EditorLabel = LOCTEXT("EditorLabel", "Editor");
			const FText FormattedEditorLabel = FText::Format(INVTEXT(" ({0})"), EditorLabel);

			return FText::Format(INVTEXT("{0}{1}"),
				FText::FromName(AssetName), (bDoesAssetPointToEditorWorld ? FormattedEditorLabel : FText::GetEmpty()));
		}
		else
		{
			return FText::Format(INVTEXT("{0}"), FText::FromName(AssetName));
		}
	}

	FText GetCurrentContextText() const
	{
		UObject* WorldObject = SelectedWorldPath.ResolveObject();
		UWorld* CurrentWorld = Cast<UWorld>(WorldObject);
		check(CurrentWorld);
		return GetWorldDescription(CurrentWorld);
	}

	const FSlateBrush* GetBorderBrush(FSoftObjectPath WorldPath) const
	{
		UObject* WorldObject = WorldPath.ResolveObject();
		UWorld* CurrentWorld = Cast<UWorld>(WorldObject);
		check(CurrentWorld);

		if (CurrentWorld->WorldType == EWorldType::PIE)
		{
			return GEditor->bIsSimulatingInEditor ? FEditorStyle::GetBrush("LevelViewport.StartingSimulateBorder") : FEditorStyle::GetBrush("LevelViewport.StartingPlayInEditorBorder");
		}
		else
		{
			return FEditorStyle::GetBrush("LevelViewport.NoViewportBorder");
		}
	}

	void ToggleAutoPIE() const
	{
		OnSelectWorldContextEvent.ExecuteIfBound(nullptr);
	}

	void ToggleAutoSimulate() const
	{
		OnSelectWorldContextEvent.ExecuteIfBound(nullptr);
	}

	void OnSetWorldContextSelection(const FAssetData Asset)
	{ 
		check(Asset.IsValid());

		// Set this so we can compare selected radio button name against each radio button to see if it should be checked
		SelectedWorldPath = Asset.ToSoftObjectPath();
		
		// Set picker button text to reflect new world
		if (PickerButtonTextBlock.IsValid())
		{
			PickerButtonTextBlock.Get()->SetText(GetWorldPickerMenuButtonText(SelectedWorldPath, Asset.AssetName));
		}

		// Callback
		OnSelectWorldContextEvent.ExecuteIfBound(Asset.ToSoftObjectPath());
	}

	bool ShouldRadioButtonBeChecked(const FSoftObjectPath InWorldSoftPath) const
	{
		return SelectedWorldPath.IsAsset() && !SelectedWorldPath.IsNull() && SelectedWorldPath == InWorldSoftPath;
	}

private:
	FOnSelectWorldContext OnSelectWorldContextEvent;

	TSharedPtr<STextBlock> PickerButtonTextBlock;

	// The selected radio button's world ref's soft path (for comparison)
	FSoftObjectPath SelectedWorldPath;
};

TSharedRef<SWidget> SLevelSnapshotsEditorContextPicker::BuildWorldPickerMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// Get all worlds
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> WorldAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UWorld::StaticClass()->GetFName(), WorldAssets);

	MenuBuilder.BeginSection("Other Worlds", LOCTEXT("OtherWorldsHeader", "Other Worlds"));
	for (const FAssetData& Asset : WorldAssets)
	{
		{
			if (Asset.IsValid())
			{
				MenuBuilder.AddMenuEntry(
					GetWorldPickerMenuButtonText(Asset.ToSoftObjectPath(), Asset.AssetName),
					LOCTEXT("World", "World"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateRaw(
							this, &SLevelSnapshotsEditorContextPicker::OnSetWorldContextSelection, Asset),
						FCanExecuteAction(),
						FIsActionChecked::CreateRaw(
							this, &SLevelSnapshotsEditorContextPicker::ShouldRadioButtonBeChecked, Asset.ToSoftObjectPath())
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FText SLevelSnapshotsEditorContextPicker::GetWorldDescription(UWorld* World)
{
	FText PostFix;
	if (World->WorldType == EWorldType::PIE)
	{
		switch(World->GetNetMode())
		{
		case NM_Client:
			PostFix = FText::Format(LOCTEXT("ClientPostfixFormat", " (Client {0})"), FText::AsNumber(World->GetOutermost()->PIEInstanceID - 1));
			break;
		case NM_DedicatedServer:
		case NM_ListenServer:
			PostFix = LOCTEXT("ServerPostfix", " (Server)");
			break;
		case NM_Standalone:
			PostFix = GEditor->bIsSimulatingInEditor ? LOCTEXT("SimulateInEditorPostfix", " (Simulate)") : LOCTEXT("PlayInEditorPostfix", " (PIE)");
			break;
		}
	}
	else if (World->WorldType == EWorldType::Editor)
	{
		PostFix = LOCTEXT("EditorPostfix", " (Editor)");
	}

	return FText::Format(LOCTEXT("WorldFormat", "{0}{1}"), FText::FromString(World->GetFName().GetPlainNameString()), PostFix);
}

void SLevelSnapshotsEditorContextPicker::Construct(const FArguments& InArgs)
{
	SelectedWorldPath = InArgs._SelectWorldPath.Get();
	OnSelectWorldContextEvent = InArgs._OnSelectWorldContext;

	check(SelectedWorldPath.IsAsset());
	check(OnSelectWorldContextEvent.IsBound());

	// This is a callback lambda to update the button text when a map is changed in editor
	// If the chosen map is not the editor map, the "Editor" text will need to be removed
	FEditorDelegates::OnMapOpened.AddLambda([this](const FString& FileName, bool bAsTemplate)
	{
		if (PickerButtonTextBlock.IsValid())
		{
			PickerButtonTextBlock.Get()->SetText(GetWorldPickerMenuButtonText(
				SelectedWorldPath, FName(SelectedWorldPath.GetAssetName())));
		}
	});
	
	ChildSlot
	.Padding(0.0f)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("LevelViewport.NoViewportBorder"))
		.Padding(0.0f)
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.OnGetMenuContent(this, &SLevelSnapshotsEditorContextPicker::BuildWorldPickerMenu)
			.ToolTipText(LOCTEXT("WorldPickerButtonTooltip", "The world context whose Level Snapshots you want to view"))
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("SceneOutliner.World"))
				]

				+ SHorizontalBox::Slot()
				.Padding(3.f, 0.f)
				[
					SAssignNew(PickerButtonTextBlock, STextBlock)
					.Text(GetCurrentContextText())
				]
			]
		]
	];
}

UWorld* SLevelSnapshotsEditorInput::GetEditorWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();

		bool IsWorldNotEditorOrPIE = (World == nullptr || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Editor));

		if (IsWorldNotEditorOrPIE)
		{
			continue;
		}

		return World;
	}

	return nullptr;
}

void SLevelSnapshotsEditorInput::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	EditorInputPtr = InEditorInput;
	BuilderPtr = InBuilder;

	// This is a callback lambda to update the snapshot picker when an level snapshot asset is saved, renamed or deleted.
	// Creating an asset should also trigger this, but "Take Snapshot" won't. The snapshot will need to be saved.
	FCoreUObjectDelegates::OnObjectModified.AddLambda([this](UObject* ObjectModified)
	{
		if (ObjectModified->IsA(ULevelSnapshot::StaticClass()) && EditorContextPickerPtr.IsValid())
		{
			OverrideWorld(EditorContextPickerPtr.Get()->GetSelectedWorldSoftPath());
		}
	});

	FSoftObjectPath EditorWorldPath = FSoftObjectPath(SLevelSnapshotsEditorInput::GetEditorWorld());

	ChildSlot
		[
			SAssignNew(EditorInputOuterVerticalBox, SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(FMargin(0.f, 1.0f))
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SAssignNew(EditorContextPickerPtr, SLevelSnapshotsEditorContextPicker)
					.SelectWorldPath(EditorWorldPath)
					.OnSelectWorldContext(this, &SLevelSnapshotsEditorInput::OverrideWorld)
				]
			]

			+ SVerticalBox::Slot()
			[
				
				SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, InBuilder)
				.OwningWorldPath(EditorWorldPath)
			]
		];
}

void SLevelSnapshotsEditorInput::OverrideWorld(FSoftObjectPath InNewContextPath)
{
	// Replace the Browser widget with new world context if world and builder pointer valid
	if (!ensure(InNewContextPath.IsValid()) || !ensure(BuilderPtr.IsValid()))
	{
		UE_LOG(LogLevelSnapshots, Error,
			TEXT("SLevelSnapshotsEditorInput::OverrideWorld: Unable to rebuild Snapshot Browser; InNewContext or BuilderPtr are invalid."));
		return;
	}
	
	if (ensure(EditorInputOuterVerticalBox))
	{
		// Remove the Browser widget then add a new one into the same slot
		EditorInputOuterVerticalBox->RemoveSlot(EditorBrowserWidgetPtr.ToSharedRef());
		
		EditorInputOuterVerticalBox->AddSlot()
		[
			SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, BuilderPtr.Pin().ToSharedRef())
			.OwningWorldPath(InNewContextPath)
		];
	}
}

#undef LOCTEXT_NAMESPACE
