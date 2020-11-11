// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "ILevelSnapshotsEditorView.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"
#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

class SLevelSnapshotsEditorContextPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSetValue, UWorld*);

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorContextPicker) {}

		/** Attribute for retrieving the current context */
		SLATE_ATTRIBUTE(UWorld*, Value)

		/** Called when the user explicitly chooses a new context world. */
		SLATE_EVENT(FOnSetValue, OnSetValue)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> BuildWorldPickerMenu();

	static FText GetWorldDescription(UWorld* World);

	FText GetCurrentContextText() const
	{
		UWorld* CurrentWorld = ValueAttribute.Get();
		check(CurrentWorld);
		return GetWorldDescription(CurrentWorld);
	}

	const FSlateBrush* GetBorderBrush() const
	{
		UWorld* CurrentWorld = ValueAttribute.Get();
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
		OnSetValueEvent.ExecuteIfBound(nullptr);
	}

	void ToggleAutoSimulate() const
	{
		OnSetValueEvent.ExecuteIfBound(nullptr);
	}

	void OnSetValue(TWeakObjectPtr<UWorld> InWorld)
	{
		if (UWorld* NewContext = InWorld.Get())
		{
			OnSetValueEvent.ExecuteIfBound(NewContext);
		}
	}

	bool IsWorldCurrentValue(TWeakObjectPtr<UWorld> InWorld)
	{
		return InWorld == ValueAttribute.Get();
	}

private:
	TAttribute<UWorld*> ValueAttribute;
	FOnSetValue OnSetValueEvent;
};

TSharedRef<SWidget> SLevelSnapshotsEditorContextPicker::BuildWorldPickerMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("WorldsHeader", "Worlds"));
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World == nullptr || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Editor))
			{
				continue;
			}

			MenuBuilder.AddMenuEntry(
				GetWorldDescription(World),
				FText(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SLevelSnapshotsEditorContextPicker::OnSetValue, MakeWeakObjectPtr(World)),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SLevelSnapshotsEditorContextPicker::IsWorldCurrentValue, MakeWeakObjectPtr(World))
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}
	}
	MenuBuilder.EndSection();

	// Get all worlds
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> WorldAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UWorld::StaticClass()->GetFName(), WorldAssets);

	MenuBuilder.BeginSection("Other Worlds");
	for (const FAssetData& Asset : WorldAssets)
	{
		{
			MenuBuilder.AddMenuEntry(
				FText::FromName(Asset.AssetName),
				LOCTEXT("World", "World"),
				FSlateIcon(),
				FUIAction()
				);
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
	ValueAttribute = InArgs._Value;
	OnSetValueEvent = InArgs._OnSetValue;
	
	check(ValueAttribute.IsSet());
	check(OnSetValueEvent.IsBound());

	UWorld* World = GEditor->GetEditorWorldContext().World();

	ChildSlot
	.Padding(0.0f)
	[
		SNew(SBorder)
		.BorderImage(this, &SLevelSnapshotsEditorContextPicker::GetBorderBrush)
		.Padding(0.0f)
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.OnGetMenuContent(this, &SLevelSnapshotsEditorContextPicker::BuildWorldPickerMenu)
			.ToolTipText(FText::Format(LOCTEXT("WorldPickerTextFomrat", "'{0}': The world context that sequencer should be bound to, and playback within."), GetCurrentContextText()))
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
					SNew(STextBlock)
					.Text(GetWorldDescription(World))
				]
			]
		]
	];
}

SLevelSnapshotsEditorInput::~SLevelSnapshotsEditorInput()
{
}

void SLevelSnapshotsEditorInput::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	EditorInputPtr = InEditorInput;
	BuilderPtr = InBuilder;

	auto GetWorld = [this]()
	{
		TSharedPtr<FLevelSnapshotsEditorInput> EditorInput = EditorInputPtr.Pin();
		check(EditorInput.IsValid());
		TSharedPtr<FLevelSnapshotsEditorViewBuilder> Builder = BuilderPtr.Pin();
		TSharedPtr<ILevelSnapshotsEditorContext> EditorContext = Builder->EditorContextPtr.Pin();
		check(EditorContext.IsValid());

		return EditorContext->Get();
	};

	ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(FMargin(0.f, 1.0f))
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(SLevelSnapshotsEditorContextPicker)
					.Value_Lambda(GetWorld)
					.OnSetValue(this, &SLevelSnapshotsEditorInput::OverrideWith)
				]
			]

			+ SVerticalBox::Slot()
			[
				SNew(SLevelSnapshotsEditorBrowser, InBuilder)
					.Value_Lambda(GetWorld)
			]
		];
}

void SLevelSnapshotsEditorInput::OverrideWith(UWorld* InNewContext)
{
}

#undef LOCTEXT_NAMESPACE
