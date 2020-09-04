// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraTab.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "RemoteSession.h"
#include "SlateOptMacros.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSplitter.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IDetailsView.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "VirtualCameraActor.h"
#include "VirtualCameraEditorStyle.h"
#include "VirtualCameraSubsystem.h"

#define LOCTEXT_NAMESPACE "VirtualCameraTab"

namespace VirtualCamera
{
	static const FName VirtualCameraApp = "SVirtualCameraApp";
	static const FName LevelEditorModuleName = "LevelEditor";
	static const FVector2D DefaultResolution{ 1536, 1152 };
	static FDelegateHandle LevelEditorTabManagerChangedHandle;

	TSharedRef<SDockTab> CreateVirtualCameraViewportTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SVirtualCameraTab)
			];
	}
}


UVirtualCameraTabUserData::UVirtualCameraTabUserData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TargetDeviceResolution(VirtualCamera::DefaultResolution)
	, Port(IRemoteSessionModule::kDefaultPort)
{
}

void SVirtualCameraTab::RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceGroup)
{
	auto RegisterTabSpawner = [InWorkspaceGroup]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(VirtualCamera::LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		LevelEditorTabManager->RegisterTabSpawner(VirtualCamera::VirtualCameraApp, FOnSpawnTab::CreateStatic(&VirtualCamera::CreateVirtualCameraViewportTab))
			.SetDisplayName(LOCTEXT("TabTitle", "VirtualCamera"))
			.SetTooltipText(LOCTEXT("TooltipText", "Set up the Virtual Camera."))
			.SetGroup(InWorkspaceGroup)
			.SetIcon(FSlateIcon(FVirtualCameraEditorStyle::GetStyleSetName(), "TabIcons.VirtualCamera.Small"));
	};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	if (LevelEditorModule.GetLevelEditorTabManager())
	{
		RegisterTabSpawner();
	}
	else
	{
		VirtualCamera::LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda(RegisterTabSpawner);
	}
}

void SVirtualCameraTab::UnregisterNomadTabSpawner()
{
	if (FSlateApplication::IsInitialized() && FModuleManager::Get().IsModuleLoaded(VirtualCamera::LevelEditorModuleName))
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VirtualCamera::LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager;
		if (LevelEditorModule)
		{
			LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
			LevelEditorModule->OnTabManagerChanged().Remove(VirtualCamera::LevelEditorTabManagerChangedHandle);
		}

		if (LevelEditorTabManager.IsValid())
		{
			LevelEditorTabManager->UnregisterTabSpawner(VirtualCamera::LevelEditorModuleName);
		}
	}
}

void SVirtualCameraTab::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(WidgetUserData);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SVirtualCameraTab::Construct(const FArguments& InArgs)
{
	SetCanTick(false);

	WidgetUserData = NewObject<UVirtualCameraTabUserData>();

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bAllowFavoriteSystem = false;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.ViewIdentifier = "VirtualCamera";
	DetailView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailView->SetObject(WidgetUserData);
	DetailView->OnFinishedChangingProperties().AddSP(this, &SVirtualCameraTab::OnPropertyChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(2.f))
		[
			MakeToolBar()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(FMargin(2.f))
		[
			SAssignNew(Splitter, SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			[
				SNew(SBorder)
				.Padding(FMargin(3.f))
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.IsEnabled_Lambda([this]() { return !IsStreaming(); })
				[
					DetailView.ToSharedRef()
				]
			]
		]
	];

	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->OnMapChanged().AddSP(this, &SVirtualCameraTab::OnMapChanged);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SVirtualCameraTab::~SVirtualCameraTab()
{
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->OnMapChanged().RemoveAll(this);
	}
}

TSharedRef<class SWidget> SVirtualCameraTab::MakeToolBar()
{
	FToolBarBuilder ToolBarBuilder(TSharedPtr<FUICommandList>(), FMultiBoxCustomization::None);
	ToolBarBuilder.BeginSection(TEXT("Stream"));
	{
		ToolBarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda([this]
				{
					StartStreaming();
				}),
				FCanExecuteAction::CreateLambda([this]
				{
					return CanStream() && !IsStreaming();
				})),
			NAME_None,
			LOCTEXT("Stream_Label", "Stream"),
			LOCTEXT("Stream_ToolTip", "Start streaming the target to the VirtualCamera application."),
			FSlateIcon(FVirtualCameraEditorStyle::GetStyleSetName(), "VirtualCamera.Stream")
			);
		ToolBarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda([this]
				{
					StopStreaming();
				}),
				FCanExecuteAction::CreateLambda([this]
				{
					return IsStreaming();
				})
			),
			NAME_None,
			LOCTEXT("Stop_Label", "Stop"),
			LOCTEXT("Stop_ToolTip", "Stop streaming."),
			FSlateIcon(FVirtualCameraEditorStyle::GetStyleSetName(), "VirtualCamera.Stop")
			);
	}
	ToolBarBuilder.EndSection();

	return ToolBarBuilder.MakeWidget();
}

void SVirtualCameraTab::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	if (!WidgetUserData->VirtualCameraActor.IsValid())
	{
		return;
	}

	if (ChangeType == EMapChangeType::TearDownWorld)
	{
		if (UVirtualCameraSubsystem* SubSystem = GEngine->GetEngineSubsystem<UVirtualCameraSubsystem>())
		{
			SubSystem->SetVirtualCameraController(nullptr);
		}
	}
	else if (ChangeType == EMapChangeType::LoadMap)
	{
		if (UVirtualCameraSubsystem* SubSystem = GEngine->GetEngineSubsystem<UVirtualCameraSubsystem>())
		{
			SubSystem->SetVirtualCameraController(WidgetUserData->VirtualCameraActor.Get());
		}
	}
}

void SVirtualCameraTab::OnPropertyChanged(const FPropertyChangedEvent& InEvent)
{
	if (InEvent.Property != nullptr)
	{
		const FName PropertyName(InEvent.Property->GetFName());

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UVirtualCameraTabUserData, VirtualCameraActor))
		{
			if (UVirtualCameraSubsystem* SubSystem = GEngine->GetEngineSubsystem<UVirtualCameraSubsystem>())
			{
				SubSystem->SetVirtualCameraController(WidgetUserData->VirtualCameraActor.Get());
			}
		}
	}
}

bool SVirtualCameraTab::IsStreaming() const
{
	if (!WidgetUserData->VirtualCameraActor.IsValid())
	{
		return false;
	}

	{
		FEditorScriptExecutionGuard ScriptGuard;
		UObject* VirtualCameraControllerBase = WidgetUserData->VirtualCameraActor.Get();
		return WidgetUserData->VirtualCameraActor.IsValid() && WidgetUserData->VirtualCameraActor->Execute_IsStreaming(VirtualCameraControllerBase);
	}
}

bool SVirtualCameraTab::CanStream() const
{
	return WidgetUserData && WidgetUserData->VirtualCameraActor.IsValid() && WidgetUserData->TargetDeviceResolution.X > 1 && WidgetUserData->TargetDeviceResolution.Y > 1;
}

bool SVirtualCameraTab::StartStreaming()
{
	if (!WidgetUserData->VirtualCameraActor.IsValid())
	{
		return false;
	}

	// override the actor's settings
	WidgetUserData->VirtualCameraActor->RemoteSessionPort = WidgetUserData->Port;
	WidgetUserData->VirtualCameraActor->TargetDeviceResolution = WidgetUserData->TargetDeviceResolution;

	if (UVirtualCameraSubsystem* SubSystem = GEngine->GetEngineSubsystem<UVirtualCameraSubsystem>())
	{
		return SubSystem->StartStreaming();
	}

	return false;
}

bool SVirtualCameraTab::StopStreaming()
{
	if (!WidgetUserData->VirtualCameraActor.IsValid())
	{
		return false;
	}

	if (UVirtualCameraSubsystem* SubSystem = GEngine->GetEngineSubsystem<UVirtualCameraSubsystem>())
	{
		return SubSystem->StopStreaming();
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
