// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshProcessingLibraryModule.h"
#include "MeshProcessingLibrary.h"


#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Internationalization/Text.h"
#include "ISettingsModule.h"
#include "Layers/LayersSubsystem.h"
#include "LevelEditor.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "MeshProcessingLibraryModule"

namespace MeshProcessingLibraryUtils
{

	void GetStaticMeshActors(AActor* RootActor, TArray<AActor*>& MeshActors);

	enum class EJacketingAction : uint8
	{
		None = 0,
		/** Tag the invisible actors/static mesh components with "Jacketing Hidden". */
		Tag,
		/** Hide the invisible actors/static mesh components. */
		Hide,
		/** Move the invisible actors/static mesh components to the "Jacketing Hidden" layer. */
		Layer,
		/** Delete the invisible actors/static mesh components or the triangles/vertices in case of Mesh target. */
		Delete
	};

	class FJacketingDetailsCustomization;

	class SObjectEditingWindow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SObjectEditingWindow)
		{}

			SLATE_ARGUMENT(UObject*, Parameters)
			SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
		SLATE_END_ARGS()

	public:
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

		MeshProcessingLibraryUtils::EJacketingAction GetAction() const
		{
			return bCanProceed ? Action : MeshProcessingLibraryUtils::EJacketingAction::None;
		}

		static MeshProcessingLibraryUtils::EJacketingAction DisplayDialog(UObject* Object);

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

		void SetAction(EJacketingAction InAction)
		{
			Action = InAction;
		}

	private:
		UObject* Parameters;
		TArray< TSharedPtr<FString> >	ActionOptions;
		TWeakPtr< SWindow > Window;
		EJacketingAction Action;
		bool bCanProceed;

		friend FJacketingDetailsCustomization;
	};

	class FJacketingDetailsCustomization : public IDetailCustomization
	{
	public:
		/** Makes a new instance of this detail layout class for a specific detail view requesting it */
		static TSharedRef<IDetailCustomization> MakeInstance(SObjectEditingWindow *Window)
		{
			return MakeShareable(new FJacketingDetailsCustomization(Window));
		}

		// IDetailCustomization interface
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override
		{
			SelectedAction = MakeShareable(new FString(TEXT("Tag")));
			ActionOptions.Add(SelectedAction);
			ActionOptions.Add(MakeShareable(new FString(TEXT("Hide"))));
			ActionOptions.Add(MakeShareable(new FString(TEXT("Layer"))));
			ActionOptions.Add(MakeShareable(new FString(TEXT("Delete"))));

			IDetailCategoryBuilder& JacketingCategory = DetailLayout.EditCategory("Jacketing", FText::GetEmpty(), ECategoryPriority::Default);

			// Add all UJacketingOptions class's properties before the custom row
			JacketingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UJacketingOptions, Accuracy));
			JacketingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UJacketingOptions, MergeDistance));
			IDetailPropertyRow& DetailPropertyRow = JacketingCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UJacketingOptions, Target));

			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			DetailPropertyRow.GetDefaultWidgets(NameWidget, ValueWidget);
			volatile FVector2D DesiredSize = ValueWidget->GetDesiredSize();

			// Add callback to update action widget according to target
			JacketingTargetProp = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UJacketingOptions, Target));
			JacketingTargetProp->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FJacketingDetailsCustomization::OnTargetChanged));

			// Create action widget
			FDetailWidgetRow& WidgetRow = JacketingCategory.AddCustomRow(LOCTEXT("ActionType", "Action"), false)
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FJacketingDetailsCustomization_ActionLabel", "Action Type"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				[
					SNew(STextComboBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.OptionsSource(&ActionOptions)
					.InitiallySelectedItem(SelectedAction)
					.OnSelectionChanged(this, &FJacketingDetailsCustomization::OnActionChanged)
					.ToolTipText(LOCTEXT("FJacketingDetailsCustomization_ActionTooltip", "Type of action to apply when 'Action Level' is set to 'Level'."))
				];

			// Cache action widget for future update according to target
			ActionValueWidget = StaticCastSharedPtr<STextComboBox>(TSharedPtr<SWidget>(WidgetRow.ValueContent().Widget));

			// Update Action widget according to target
			OnTargetChanged();
		}
		// End of IDetailCustomization interface

	protected:
		FJacketingDetailsCustomization(SObjectEditingWindow* InWindow)
			: Action(EJacketingAction::Tag)
		{
			TSharedPtr<SWidget> WindowPtr(InWindow->AsShared());
			Window = TWeakPtr< SObjectEditingWindow >(StaticCastSharedPtr< SObjectEditingWindow >(WindowPtr));
		}
		
		void OnActionChanged(TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo)
		{
			int32 NewSelection;
			bool bFound = ActionOptions.Find(ItemSelected, NewSelection);
			check(bFound && NewSelection != INDEX_NONE);

			using namespace MeshProcessingLibraryUtils;

			switch (NewSelection)
			{
			case 0:
			{
				Action = EJacketingAction::Tag;
				break;
			}
			case 1:
			{
				Action = EJacketingAction::Hide;
				break;
			}
			case 2:
			{
				Action = EJacketingAction::Layer;
				break;
			}
			case 3:
			{
				Action = EJacketingAction::Delete;
				break;
			}
			default:
			{
				check(false);
				break;
			}
			}

			Window.Pin()->SetAction(Action);
		}

		void OnTargetChanged()
		{
			if (JacketingTargetProp.IsValid())
			{
				uint8 EnumVal;
				JacketingTargetProp->GetValue(EnumVal);
				if (EnumVal == 0)
				{
					ActionValueWidget->SetSelectedItem(SelectedAction);
					ActionValueWidget->SetEnabled(true);
				}
				else if (EnumVal == 1)
				{
					SelectedAction = ActionValueWidget->GetSelectedItem();
					ActionValueWidget->SetSelectedItem(ActionOptions[3]);
					ActionValueWidget->SetEnabled(false);
				}
				else
				{
					check(false);
				}
			}
		}

	private:
		EJacketingAction Action;
		TArray< TSharedPtr<FString> >		ActionOptions;
		TWeakPtr< SObjectEditingWindow >	Window;
		TSharedPtr<STextComboBox>			ActionValueWidget;
		TSharedPtr<FString>					SelectedAction;
		TSharedPtr<IPropertyHandle>			JacketingTargetProp;
	};
}

class FMeshProcessingLibraryModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		if (!IsRunningCommandlet())
		{
			RegisterSettings();
			SetupMenuEntry();
		}
	}

	virtual void ShutdownModule() override
	{
		if (!IsRunningCommandlet())
		{
			UnregisterSettings();
			RemoveMenuEntry();
		}
	}

private:
	void SetupMenuEntry()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked< FLevelEditorModule >("LevelEditor");
		TArray< FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors >& CBMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();

		CBMenuExtenderDelegates.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&FMeshProcessingLibraryModule::OnExtendLevelEditorMenu));
		LevelEditorExtenderDelegateHandle = CBMenuExtenderDelegates.Last().GetHandle();
	}

	void RemoveMenuEntry()
	{
		if (LevelEditorExtenderDelegateHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("LevelEditor"))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >("LevelEditor");
			TArray< FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors >& CBMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
			CBMenuExtenderDelegates.RemoveAll(
				[this](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate)
				{
					return Delegate.GetHandle() == LevelEditorExtenderDelegateHandle;
				}
			);
		}
	}

	static TSharedRef<FExtender> OnExtendLevelEditorMenu(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors)
	{
		TSharedRef<FExtender> Extender = MakeShared<FExtender>();

		bool bShouldExtendActorActions = false;

		TArray<AActor*> ActorsToVisit(SelectedActors);
		while (ActorsToVisit.Num() > 0)
		{
			AActor* Actor = ActorsToVisit.Pop();
			if (Actor == nullptr)
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component->GetClass() == UStaticMeshComponent::StaticClass())
				{
					bShouldExtendActorActions = true;
					break;
				}
			}

			if (bShouldExtendActorActions)
			{
				break;
			}

			// Continue parsing children
			TArray<AActor*> Children;
			Actor->GetAttachedActors(Children);

			ActorsToVisit.Append(Children);
		}

		if (bShouldExtendActorActions)
		{
			// Add the actions sub-menu extender
			Extender->AddMenuExtension("ActorControl", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateLambda(
				[SelectedActors](FMenuBuilder& MenuBuilder)
				{
					MenuBuilder.AddMenuEntry(
						NSLOCTEXT("MeshProcessingLibraryActions", "ObjectContext_Jacketing", "Jacketing"),
						NSLOCTEXT("MeshProcessingLibraryActions", "ObjectContext_JacketingTooltip", "Identify and process occluded meshes or part of meshes"),
						FSlateIcon(FName("PolygonEditingToolbarStyle"), "MeshEditorPolygonMode.Jacketing"), // Should be unified with other UI components used by Datasmith features
						FUIAction(FExecuteAction::CreateStatic(&FMeshProcessingLibraryModule::ApplyJacketing, SelectedActors), FCanExecuteAction()));
				}));
		}

		return Extender;
	}

	// Register UMeshProcessingEnterpriseSettings in settings module to make its properties visible in the editor
	void RegisterSettings()
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "Polygon Editing",
				LOCTEXT("RuntimeSettingsName", "Polygon Editing"),
				LOCTEXT("RuntimeSettingsDescription", "Override the maximum size of the undo buffer"),
				GetMutableDefault<UMeshProcessingEnterpriseSettings>()
			);
		}
	}

	// Unregister UMeshProcessingEnterpriseSettings
	void UnregisterSettings()
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Polygon Editing");
		}
	}

	static void ApplyJacketing(TArray< AActor*> SelectedActors)
	{
		if (SelectedActors.Num() == 0)
		{
			return;
		}

		TStrongObjectPtr<UJacketingOptions> Parameters = TStrongObjectPtr<UJacketingOptions>(NewObject<UJacketingOptions>(GetTransientPackage(), TEXT("Assembly Jacketing Parameters")));

		MeshProcessingLibraryUtils::EJacketingAction Action = MeshProcessingLibraryUtils::SObjectEditingWindow::DisplayDialog(Parameters.Get());
		if (Action == MeshProcessingLibraryUtils::EJacketingAction::None)
		{
			return;
		}

		// Save parameters to config file
		Parameters->SaveConfig(CPF_Config, *Parameters->GetDefaultConfigFilename());

		FScopedTransaction Transaction(LOCTEXT("MeshProcessing", "Jacketing"));

		if (SelectedActors.Num() == 1)
		{
			// Check to see if the selected actor has mesh actors as children or StaticMesh components attached to it
			TArray<AActor*> MeshActors;
			MeshProcessingLibraryUtils::GetStaticMeshActors(SelectedActors[0], MeshActors);

			if (MeshActors.Num() == 0)
			{
				return;
			}

			// Update array of actors to process with the new set of mesh actors
			SelectedActors = MeshActors;
		}

		TArray<AActor*> OccludedActors;
		UMeshProcessingLibrary::ApplyJacketingOnMeshActors(SelectedActors, Parameters.Get(), OccludedActors, false);

		// If user did not require action on mesh, select all fully occluded actors
		if (Parameters->Target == EJacketingTarget::Level)
		{
			const FName JacketingLayerName("Jacketing Layer");

			ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
			if (Action == MeshProcessingLibraryUtils::EJacketingAction::Delete)
			{
				// Destory actor Editor-mode, see EditorLevelLibrary::DestroyActor
				UWorld* World = GEditor->GetEditorWorldContext(false).World();
				if (!World)
				{
					UE_LOG(LogMeshProcessingLibrary, Error, TEXT("AssemblyJacketing: Can't delete actors because there is no world. Occluded meshes will hidden."));
					Action = MeshProcessingLibraryUtils::EJacketingAction::Hide;
				}
			}
			else if (Action == MeshProcessingLibraryUtils::EJacketingAction::Layer)
			{
				if (!LayersSubsystem->GetLayer(JacketingLayerName))
				{
					LayersSubsystem->CreateLayer(JacketingLayerName);
				}

				if (!LayersSubsystem->GetLayer(JacketingLayerName))
				{
					UE_LOG(LogMeshProcessingLibrary, Error, TEXT("AssemblyJacketing: Can't assign actors to the 'Jacketing Layer' layer. Occluded meshes will hidden."));
					Action = MeshProcessingLibraryUtils::EJacketingAction::Hide;
				}
			}

			switch (Action)
			{
				case MeshProcessingLibraryUtils::EJacketingAction::Delete:
				{
					UWorld* World = GEditor->GetEditorWorldContext(false).World();

					for (AActor* Actor : OccludedActors)
					{
						if (Actor->IsSelected())
						{
							GEditor->SelectActor(Actor, false, true);
						}

						LayersSubsystem->DisassociateActorFromLayers(Actor);

						if (!World->DestroyActor(Actor, false, true))
						{
							UE_LOG(LogMeshProcessingLibrary, Error, TEXT("AssemblyJacketing: Cannot delete Actor %s."), *Actor->GetActorLabel());
						}
					}
					break;
				}
				case MeshProcessingLibraryUtils::EJacketingAction::Tag:
				{
					const FName TagName("Jacketing Hidden");
					for (AActor* Actor : OccludedActors)
					{
						for (UActorComponent* Component : Actor->GetComponents())
						{
							if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
							{
								StaticMeshComponent->ComponentTags.Add(TagName);
							}
						}
					}
					break;
				}
				case MeshProcessingLibraryUtils::EJacketingAction::Layer:
				{
					TArray< TWeakObjectPtr<AActor> > ActorsForLayer;
					for (AActor* Actor : OccludedActors)
					{
						ActorsForLayer.Add(TWeakObjectPtr<AActor>(Actor));
					}
					LayersSubsystem->AddActorsToLayer(ActorsForLayer, JacketingLayerName);
					break;
				}
				case MeshProcessingLibraryUtils::EJacketingAction::Hide:
				{
					for (AActor* Actor : OccludedActors)
					{
						TInlineComponentArray<UStaticMeshComponent*> Components;
						Actor->GetComponents(Components);
						for (UStaticMeshComponent* StaticMeshComponent : Components)
						{
							StaticMeshComponent->SetVisibility(false);
							StaticMeshComponent->SetHiddenInGame(true);
						}
					}
					break;
				}
			}
		}
	}

	FDelegateHandle LevelEditorExtenderDelegateHandle;
};

namespace MeshProcessingLibraryUtils
{
	void SObjectEditingWindow::Construct(const FArguments& InArgs)
	{
		Parameters = InArgs._Parameters;
		Window = InArgs._WidgetWindow;
		Action = EJacketingAction::Tag;
		bCanProceed = false;

		ActionOptions.Add(MakeShareable(new FString(TEXT("Tag"))));
		ActionOptions.Add(MakeShareable(new FString(TEXT("Hide"))));
		ActionOptions.Add(MakeShareable(new FString(TEXT("Layer"))));
		ActionOptions.Add(MakeShareable(new FString(TEXT("Delete"))));

		TSharedPtr<SBox> DetailsViewBox;
		ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				[
					SAssignNew(DetailsViewBox, SBox)
					.MaxDesiredHeight(320.0f)
					.MaxDesiredWidth(450.0f)
				]

				+ SVerticalBox::Slot()
				.HAlign(HAlign_Right)
				.MaxHeight(50)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(5)
					+ SUniformGridPanel::Slot(0, 0)
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Text(LOCTEXT("SObjectEditingWindow_ImportCurLevel", "Proceed"))
						.OnClicked(this, &SObjectEditingWindow::OnProceed)
					]
					+ SUniformGridPanel::Slot(1, 0)
					.HAlign(HAlign_Right)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Text(LOCTEXT("SObjectEditingWindow_Cancel", "Cancel"))
						.OnClicked(this, &SObjectEditingWindow::OnCancel)
					]
				]
			];

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		FOnGetDetailCustomizationInstance LayoutComponentDetails = FOnGetDetailCustomizationInstance::CreateStatic(&FJacketingDetailsCustomization::MakeInstance, this);
		DetailsView->RegisterInstancedCustomPropertyLayout(UJacketingOptions::StaticClass(), LayoutComponentDetails);

		DetailsViewBox->SetContent(DetailsView.ToSharedRef());

		TArray<UObject*> Objects;
		Objects.Add(Parameters);
		DetailsView->SetObjects(Objects);
	}

	EJacketingAction SObjectEditingWindow::DisplayDialog(UObject* Object)
	{
		TSharedPtr<SWindow> ParentWindow;

		if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
		{
			IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
			ParentWindow = MainFrame.GetParentWindow();
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("SObjectEditingWindow_Title", "Remove occluded meshes"))
			.SizingRule(ESizingRule::Autosized);

		TSharedPtr<SObjectEditingWindow> ParameterWindow;
		Window->SetContent
		(
			SAssignNew(ParameterWindow, SObjectEditingWindow)
			.Parameters(Object)
			.WidgetWindow(Window)
		);

		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

		return ParameterWindow->GetAction();
	}

	void GetStaticMeshActors(AActor* RootActor, TArray<AActor*>& MeshActors)
	{
		MeshActors.Empty();

		if (RootActor == nullptr)
		{
			return;
		}

		TArray<AActor*> ActorsToVisit;
		ActorsToVisit.Add(RootActor);

		while (ActorsToVisit.Num() > 0)
		{
			AActor* Actor = ActorsToVisit.Pop();
			if (Actor == nullptr)
			{
				continue;
			}

			if (Actor->GetClass() == AStaticMeshActor::StaticClass())
			{
				MeshActors.Add(Actor);
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component->GetClass() == UStaticMeshComponent::StaticClass())
				{
					MeshActors.Add(Actor);
					break;
				}
			}

			// Continue parsing children
			TArray<AActor*> Children;
			Actor->GetAttachedActors(Children);

			ActorsToVisit.Append(Children);
		}
	}
}

#undef LOCTEXT_NAMESPACE // "MeshProcessingLibraryModule"

IMPLEMENT_MODULE(FMeshProcessingLibraryModule, MeshProcessingLibrary);
