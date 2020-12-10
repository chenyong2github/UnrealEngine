// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTools/CameraAnimationSequenceActions.h"
#include "AssetTools/TemplateSequenceActions.h"
#include "AssetToolsModule.h"
#include "CameraAnimationSequence.h"
#include "ClassViewerModule.h"
#include "Camera/CameraActor.h"
#include "CineCameraActor.h"
#include "DragAndDrop/ActorDragDropGraphEdOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/ClassDragDropOp.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencerModule.h"
#include "ISettingsModule.h"
#include "Misc/MovieSceneSequenceEditor_TemplateSequence.h"
#include "Misc/TemplateSequenceEditorSettings.h"
#include "Misc/TemplateSequenceEditorUtil.h"
#include "Modules/ModuleManager.h"
#include "SequencerSettings.h"
#include "SequencerCustomizationManager.h"
#include "Styles/TemplateSequenceEditorStyle.h"
#include "TemplateSequence.h"
#include "TrackEditors/TemplateSequenceTrackEditor.h"
#include "UObject/Class.h"
#include "UObject/GCObject.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "TemplateSequenceEditor"

class FTemplateSequenceCustomizationBase
{
protected:
	FText GetBoundActorClassName() const;
	void OnBoundActorClassPicked(UClass* ChosenClass);
	void ChangeActorBinding(UObject* Object, UActorFactory* ActorFactory = nullptr, bool bSetupDefaults = true);

	ISequencer* Sequencer;
	UTemplateSequence* TemplateSequence;
};

/**
 * The sequencer customization for template sequences. 
 */
class FTemplateSequenceCustomization : public ISequencerCustomization, FTemplateSequenceCustomizationBase
{
public:
	virtual void RegisterSequencerCustomization(FSequencerCustomizationBuilder& Builder) override;
	virtual void UnregisterSequencerCustomization() override;

private:
	void ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder);

	bool OnSequencerReceivedDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, FReply& OutReply);
	ESequencerDropResult OnSequencerAssetsDrop(const TArray<UObject*>& Assets, const FAssetDragDropOp& DragDropOp);
	ESequencerDropResult OnSequencerClassesDrop(const TArray<TWeakObjectPtr<UClass>>& Classes, const FClassDragDropOp& DragDropOp);
	ESequencerDropResult OnSequencerActorsDrop(const TArray<TWeakObjectPtr<AActor>>& Actors, const FActorDragDropGraphEdOp& DragDropOp);
	TSharedRef<SWidget> GetBoundActorClassMenuContent();
};

/**
 * The sequencer customization for camera animation sequences.
 */
class FCameraAnimationSequenceCustomization : public ISequencerCustomization, FTemplateSequenceCustomizationBase
{
public:
	virtual void RegisterSequencerCustomization(FSequencerCustomizationBuilder& Builder) override;
	virtual void UnregisterSequencerCustomization() override;

private:
	void ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder);

	TSharedRef<SWidget> GetBoundCameraClassMenuContent();
	bool IsBoundToActorClass(UClass* InClass);

	TArray<UClass*> CameraActorClasses;
};

/**
 * Implements the FTemplateSequenceEditor module.
 */
class FTemplateSequenceEditorModule : public IModuleInterface, public FGCObject
{
public:
	FTemplateSequenceEditorModule()
		: Settings(nullptr)
	{
	}

	virtual void StartupModule() override
	{
		RegisterSettings();
		RegisterAssetTools();
		RegisterSequenceEditor();
		RegisterTrackEditors();
		RegisterSequenceCustomizations();
	}

	virtual void ShutdownModule() override
	{
		UnregisterSequenceCustomizations();
		UnregisterTrackEditors();
		UnregisterSequenceEditor();
		UnregisterAssetTools();
		UnregisterSettings();
	}

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		if (Settings != nullptr)
		{
			Collector.AddReferencedObject(Settings);
		}
	}

private:
	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "TemplateSequencer",
				LOCTEXT("TemplateSequencerSettingsName", "Template Sequencer"),
				LOCTEXT("TemplateSequencerSettingsDescription", "Configure the Template Sequence Editor."),
				GetMutableDefault<UTemplateSequenceEditorSettings>()
			);

			Settings = USequencerSettingsContainer::GetOrCreate<USequencerSettings>(TEXT("TemplateSequenceEditor"));

			SettingsModule->RegisterSettings("Editor", "ContentEditors", "TemplateSequenceEditor",
				LOCTEXT("TemplateSequenceEditorSettingsName", "Template Sequence Editor"),
				LOCTEXT("TemplateSequenceEditorSettingsDescription", "Configure the look and feel of the Template Sequence Editor."),
				Settings);
		}
	}

	void UnregisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "TemplateSequencer");
			SettingsModule->UnregisterSettings("Editor", "ContentEditors", "TemplateSequenceEditor");
		}
	}

	void RegisterAssetTools()
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		TemplateSequenceTypeActions = MakeShared<FTemplateSequenceActions>(FTemplateSequenceEditorStyle::Get());
		AssetTools.RegisterAssetTypeActions(TemplateSequenceTypeActions.ToSharedRef());

		CameraAnimationSequenceActions = MakeShared<FCameraAnimationSequenceActions>(FTemplateSequenceEditorStyle::Get());
		AssetTools.RegisterAssetTypeActions(CameraAnimationSequenceActions.ToSharedRef());
	}

	void UnregisterAssetTools()
	{
		FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

		if (AssetToolsModule != nullptr)
		{
			IAssetTools& AssetTools = AssetToolsModule->Get();
			AssetTools.UnregisterAssetTypeActions(TemplateSequenceTypeActions.ToSharedRef());
			AssetTools.UnregisterAssetTypeActions(CameraAnimationSequenceActions.ToSharedRef());
		}
	}

	void RegisterSequenceEditor()
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(UTemplateSequence::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_TemplateSequence>());
	}

	void UnregisterSequenceEditor()
	{
		ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
		if (SequencerModule)
		{
			SequencerModule->UnregisterSequenceEditor(SequenceEditorHandle);
		}
	}

	void RegisterTrackEditors()
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		TemplateSequenceTrackCreateEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FTemplateSequenceTrackEditor::CreateTrackEditor));
	}

	void UnregisterTrackEditors()
	{
		ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
		if (SequencerModule)
		{
			SequencerModule->UnRegisterTrackEditor(TemplateSequenceTrackCreateEditorHandle);
		}
	}

	void RegisterSequenceCustomizations()
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.GetSequencerCustomizationManager()->RegisterInstancedSequencerCustomization(UTemplateSequence::StaticClass(),
				FOnGetSequencerCustomizationInstance::CreateLambda([]()
				{
					return new FTemplateSequenceCustomization();
				}));
		SequencerModule.GetSequencerCustomizationManager()->RegisterInstancedSequencerCustomization(UCameraAnimationSequence::StaticClass(),
				FOnGetSequencerCustomizationInstance::CreateLambda([]()
				{
					return new FCameraAnimationSequenceCustomization();
				}));
	}

	void UnregisterSequenceCustomizations()
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.GetSequencerCustomizationManager()->UnregisterInstancedSequencerCustomization(UCameraAnimationSequence::StaticClass());
		SequencerModule.GetSequencerCustomizationManager()->UnregisterInstancedSequencerCustomization(UTemplateSequence::StaticClass());
	}

private:

	TSharedPtr<FTemplateSequenceActions> TemplateSequenceTypeActions;
	TSharedPtr<FCameraAnimationSequenceActions> CameraAnimationSequenceActions;

	FDelegateHandle SequenceEditorHandle;
	FDelegateHandle TemplateSequenceTrackCreateEditorHandle;

	USequencerSettings* Settings;
};

IMPLEMENT_MODULE(FTemplateSequenceEditorModule, TemplateSequenceEditor);

FText FTemplateSequenceCustomizationBase::GetBoundActorClassName() const
{
	const UClass* BoundActorClass = TemplateSequence ? TemplateSequence->BoundActorClass.Get() : nullptr;
	return BoundActorClass ? BoundActorClass->GetDisplayNameText() : FText::FromName(NAME_None);
}

void FTemplateSequenceCustomizationBase::OnBoundActorClassPicked(UClass* ChosenClass)
{
	FSlateApplication::Get().DismissAllMenus();

	if (TemplateSequence != nullptr)
	{
		ChangeActorBinding(ChosenClass);

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

void FTemplateSequenceCustomizationBase::ChangeActorBinding(UObject* Object, UActorFactory* ActorFactory, bool bSetupDefaults)
{
	FTemplateSequenceEditorUtil Util(TemplateSequence, *Sequencer);
	Util.ChangeActorBinding(Object, ActorFactory, bSetupDefaults);
}


void FTemplateSequenceCustomization::RegisterSequencerCustomization(FSequencerCustomizationBuilder& Builder) 
{
	Sequencer = &Builder.GetSequencer();
	TemplateSequence = Cast<UTemplateSequence>(&Builder.GetFocusedSequence());

	FSequencerCustomizationInfo Customization;

	TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension("Base Commands", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateRaw(this, &FTemplateSequenceCustomization::ExtendSequencerToolbar));
	Customization.ToolbarExtender = ToolbarExtender;

	Customization.OnAssetsDrop.BindRaw(this, &FTemplateSequenceCustomization::OnSequencerAssetsDrop);
	Customization.OnClassesDrop.BindRaw(this, &FTemplateSequenceCustomization::OnSequencerClassesDrop);
	Customization.OnActorsDrop.BindRaw(this, &FTemplateSequenceCustomization::OnSequencerActorsDrop);

	Builder.AddCustomization(Customization);
}

void FTemplateSequenceCustomization::UnregisterSequencerCustomization()
{
	Sequencer = nullptr;
	TemplateSequence = nullptr;
}

void FTemplateSequenceCustomization::ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.AddSeparator();

	ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FTemplateSequenceCustomization::GetBoundActorClassMenuContent),
			LOCTEXT("BoundActorClassPicker", "Bound Actor Class"),
			LOCTEXT("BoundActorClassPickerTooltip", "Change the base actor type that this template sequence can bind to"),
			FSlateIcon(FTemplateSequenceEditorStyle::Get()->GetStyleSetName(), "TemplateSequenceEditor.Chain"));
}

TSharedRef<SWidget> FTemplateSequenceCustomization::GetBoundActorClassMenuContent()
{
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bIsActorsOnly = true;

	TSharedRef<SWidget> ClassPicker = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateRaw(this, &FTemplateSequenceCustomization::OnBoundActorClassPicked));

	return SNew(SBox)
		.WidthOverride(350.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.MaxHeight(400.0f)
			.AutoHeight()
			[
				ClassPicker
			]
		];
}

bool FTemplateSequenceCustomization::OnSequencerReceivedDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, FReply& OutReply)
{
	bool bIsDragSupported = false;

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && (
		(Operation->IsOfType<FAssetDragDropOp>() && StaticCastSharedPtr<FAssetDragDropOp>(Operation)->GetAssetPaths().Num() <= 1) ||
		(Operation->IsOfType<FClassDragDropOp>() && StaticCastSharedPtr<FClassDragDropOp>(Operation)->ClassesToDrop.Num() <= 1) ||
		(Operation->IsOfType<FActorDragDropGraphEdOp>() && StaticCastSharedPtr<FActorDragDropGraphEdOp>(Operation)->Actors.Num() <= 1)))
	{
		bIsDragSupported = true;
	}

	OutReply = (bIsDragSupported ? FReply::Handled() : FReply::Unhandled());
	return true;
}

ESequencerDropResult FTemplateSequenceCustomization::OnSequencerAssetsDrop(const TArray<UObject*>& Assets, const FAssetDragDropOp& DragDropOp)
{
	if (Assets.Num() > 0)
	{
		// Only drop the first asset.
		UObject* CurObject = Assets[0];

		// TODO: check for dropping a sequence?

		ChangeActorBinding(CurObject, DragDropOp.GetActorFactory());

		return ESequencerDropResult::DropHandled;
	}

	return ESequencerDropResult::Unhandled;
}

ESequencerDropResult FTemplateSequenceCustomization::OnSequencerClassesDrop(const TArray<TWeakObjectPtr<UClass>>& Classes, const FClassDragDropOp& DragDropOp)
{
	if (Classes.Num() > 0 && Classes[0].IsValid())
	{
		// Only drop the first class.
		UClass* CurClass = Classes[0].Get();

		ChangeActorBinding(CurClass);

		return ESequencerDropResult::DropHandled;
	}
	return ESequencerDropResult::Unhandled;
}

ESequencerDropResult FTemplateSequenceCustomization::OnSequencerActorsDrop(const TArray<TWeakObjectPtr<AActor>>& Actors, const FActorDragDropGraphEdOp& DragDropOp)
{
	return ESequencerDropResult::Unhandled;
}

void FCameraAnimationSequenceCustomization::RegisterSequencerCustomization(FSequencerCustomizationBuilder& Builder)
{
	Sequencer = &Builder.GetSequencer();
	TemplateSequence = Cast<UTemplateSequence>(&Builder.GetFocusedSequence());
	CameraActorClasses = { ACameraActor::StaticClass(), ACineCameraActor::StaticClass() };

	FSequencerCustomizationInfo Customization;

	TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension("Base Commands", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateRaw(this, &FCameraAnimationSequenceCustomization::ExtendSequencerToolbar));
	Customization.ToolbarExtender = ToolbarExtender;

	Customization.OnAssetsDrop.BindLambda([](const TArray<UObject*>&, const FAssetDragDropOp&) -> ESequencerDropResult { return ESequencerDropResult::DropDenied; });
	Customization.OnClassesDrop.BindLambda([](const TArray<TWeakObjectPtr<UClass>>&, const FClassDragDropOp&) -> ESequencerDropResult { return ESequencerDropResult::DropDenied; });
	Customization.OnActorsDrop.BindLambda([](const TArray<TWeakObjectPtr<AActor>>&, const FActorDragDropGraphEdOp&) -> ESequencerDropResult { return ESequencerDropResult::DropDenied; });

	Builder.AddCustomization(Customization);
}

void FCameraAnimationSequenceCustomization::UnregisterSequencerCustomization()
{
	Sequencer = nullptr;
	TemplateSequence = nullptr;
}

void FCameraAnimationSequenceCustomization::ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.AddSeparator();

	ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FCameraAnimationSequenceCustomization::GetBoundCameraClassMenuContent),
			LOCTEXT("CameraTypePicker", "Camera Type"),
			LOCTEXT("CameraTypePickerTooltip", "Change the base camera actor type that this template sequence can bind to"),
			FSlateIcon(FTemplateSequenceEditorStyle::Get()->GetStyleSetName(), "TemplateSequenceEditor.Chain"));
}

TSharedRef<SWidget> FCameraAnimationSequenceCustomization::GetBoundCameraClassMenuContent()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (UClass* CameraActorClass : CameraActorClasses)
	{
		MenuBuilder.AddMenuEntry(
				FText::FromString(CameraActorClass->GetName()),
				LOCTEXT("CameraTypeChoice", "Choose this type of camera as the type this template sequence can bind to"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, CameraActorClass]() { 
						OnBoundActorClassPicked(CameraActorClass); 
						}),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([this, CameraActorClass]() {
						return IsBoundToActorClass(CameraActorClass) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; 
						})
					),
				NAME_None,
				EUserInterfaceActionType::RadioButton
				);
	}

	return MenuBuilder.MakeWidget();
}

bool FCameraAnimationSequenceCustomization::IsBoundToActorClass(UClass* InClass)
{
	UClass* BoundActorClass = TemplateSequence ? TemplateSequence->BoundActorClass.Get() : nullptr;
	return BoundActorClass == InClass;
}

#undef LOCTEXT_NAMESPACE
