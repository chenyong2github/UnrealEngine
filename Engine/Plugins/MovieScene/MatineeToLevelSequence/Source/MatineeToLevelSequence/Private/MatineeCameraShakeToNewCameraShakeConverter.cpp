// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatineeCameraShakeToNewCameraShakeConverter.h"
#include "Animation/AnimSequence.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Camera/CameraAnim.h"
#include "CameraAnimToTemplateSequenceConverter.h"
#include "CameraAnimationSequence.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetRegistry.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "MatineeCameraShake.h"
#include "MatineeConverter.h"
#include "MatineeToLevelSequenceLog.h"
#include "Misc/ScopedSlowTask.h"
#include "SequenceCameraShake.h"
#include "SourceControlOperations.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MatineeCameraShakeToNewCameraShakeConverter"

DECLARE_DELEGATE_TwoParams(FOnConvertMatineeCameraShakeAsset, const TArray<FAssetData>&, bool);

namespace UE
{
namespace MovieScene
{

/**
 * Returns whether the given blueprint has one of the given class names as its parent class.
 */
bool HasAnyBlueprintParentClass(const FAssetData& AssetData, const TSet<FName>* ParentClassNames)
{
	const FAssetDataTagMapSharedView::FFindTagResult FoundGeneratedClassTag = AssetData.TagsAndValues.FindTag(TEXT("GeneratedClass"));
	if (FoundGeneratedClassTag.IsSet())
	{
		const FString GeneratedClassPath = FPackageName::ExportTextPathToObjectPath(FoundGeneratedClassTag.GetValue());
		const FString GeneratedClassName = FPackageName::ObjectPathToObjectName(GeneratedClassPath);
		if (ParentClassNames->Contains(*GeneratedClassName))
		{
			return true;
		}
	}
	return false;
}

/**
 * Opens the given asset in the editor.
 */
void OpenEditorForAsset(const FAssetData& AssetData)
{
	if (UObject* Asset = AssetData.GetAsset())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
	}
}

}
}

/**
 * A widget that lets the user upgrade one or more Matinee camera shakes.
 */
class SMatineeCameraShakeConverterWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMatineeCameraShakeConverterWidget) {}
	SLATE_ARGUMENT(TSet<FName>, MatineeCameraShakeClassNames)
	SLATE_ARGUMENT(const FMatineeConverter*, MatineeConverter)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		MatineeCameraShakeClassNames = InArgs._MatineeCameraShakeClassNames;
		MatineeConverter = InArgs._MatineeConverter;

		AssetRegistry = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		AssetRegistry->WaitForCompletion();

		IContentBrowserSingleton& ContentBrowserSingleton = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

		// Setup an asset picker that lists all Blueprint assets that have a known MatineeCameraShake parent class.
		FARFilter Filter;
		Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
		Filter.bRecursiveClasses = true;

		FAssetPickerConfig AssetPickerConfig;
		AssetPickerConfig.Filter = Filter;
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SMatineeCameraShakeConverterWidget::FilterNonMatineeCameraShakes);
		AssetPickerConfig.GetCurrentSelectionDelegates.Add(&GetCurrentAssetPickerSelectionDelegate);
		AssetPickerConfig.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateStatic(&UE::MovieScene::OpenEditorForAsset);
		AssetPickerConfig.RefreshAssetViewDelegates.Add(&RefreshAssetPickerAssetViewDelegate);

		AssetPicker = ContentBrowserSingleton.CreateAssetPicker(AssetPickerConfig);

		// Setup the UI.
		ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				AssetPicker.ToSharedRef()
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(10.f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("FilterAnimCameraShakes", "Only show camera shakes with a Matinee camera animation"))
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
						{
							bFilterNonAnimCameraShakes = !bFilterNonAnimCameraShakes; 
							RefreshAssetPickerAssetViewDelegate.Execute(true);
						})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FilterAnimCameraShakes", "Filter Camera Anim Shakes"))
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(10.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ConvertSelectedCameraAnims", "Convert Camera Anims"))
					.ToolTipText(LOCTEXT("ConvertSelectedSimple_Tooltip", "Upgrade the Matinee camera animation part of a shake into a Sequence. Shakes with no camera animation (only oscillation) will not be modified."))
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
					.IsEnabled(this, &SMatineeCameraShakeConverterWidget::IsConvertSelectedButtonEnabled)
					.OnPressed(this, &SMatineeCameraShakeConverterWidget::OnConvertSelected)
				]
				+SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
				+SHorizontalBox::Slot()
				.Padding(10.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Danger")
					.OnPressed_Lambda([this]() { FSlateApplication::Get().FindWidgetWindow(AsShared())->RequestDestroyWindow(); })
				]
			]
		];
	}

private:
	bool FilterNonMatineeCameraShakes(const FAssetData& AssetData) const
	{
		if (!UE::MovieScene::HasAnyBlueprintParentClass(AssetData, &MatineeCameraShakeClassNames))
		{
			// Filter out things that are not Blueprints inheriting from our known Matinee shake classes.
			return true;
		}
		if (bFilterNonAnimCameraShakes)
		{
			// Filter out shakes that don't have any external dependencies.
			TArray<FName> AssetDependencies;
			AssetRegistry->GetDependencies(AssetData.PackageName, AssetDependencies);
			if (AssetDependencies.Num() == 0)
			{
				return true;
			}

			// Filter out shakes that don't have CameraAnim assets in their external dependencies.
			for (const FName& AssetDependency : AssetDependencies)
			{
				TArray<FAssetData> AssetDependencyDatas;
				AssetRegistry->GetAssetsByPackageName(AssetDependency, AssetDependencyDatas);
				for (const FAssetData& AssetDependencyData : AssetDependencyDatas)
				{
					const bool bIsCameraAnimDependency = AssetDependencyData.GetClass()->IsChildOf<UCameraAnim>();
					if (bIsCameraAnimDependency)
					{
						return false;
					}
				}
			}
			return true;
		}
		return false;
	}

	bool IsConvertSelectedButtonEnabled() const
	{
		const TArray<FAssetData> CurrentSelection = GetCurrentAssetPickerSelectionDelegate.Execute();
		return CurrentSelection.Num() > 0;
	}

	void OnConvertSelected()
	{
		const TArray<FAssetData> CurrentSelection = GetCurrentAssetPickerSelectionDelegate.Execute();
		FMatineeCameraShakeToNewCameraShakeConverter Converter(MatineeConverter);
		Converter.ConvertMatineeCameraShakes(CurrentSelection);
	}

	TSet<FName> MatineeCameraShakeClassNames;
	const FMatineeConverter* MatineeConverter = nullptr;

	bool bFilterNonAnimCameraShakes = false;
	IAssetRegistry* AssetRegistry = nullptr;

	TSharedPtr<SWidget> AssetPicker;
	FGetCurrentSelectionDelegate GetCurrentAssetPickerSelectionDelegate;
	FRefreshAssetViewDelegate RefreshAssetPickerAssetViewDelegate;
};

FMatineeCameraShakeToNewCameraShakeConverter::FMatineeCameraShakeToNewCameraShakeConverter(const FMatineeConverter* InMatineeConverter)
	: MatineeConverter(InMatineeConverter)
{
}

TSharedRef<SWidget> FMatineeCameraShakeToNewCameraShakeConverter::CreateMatineeCameraShakeConverter(const FMatineeConverter* InMatineeConverter)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	AssetRegistry.WaitForCompletion();

	TArray<FName> ClassNames = { TEXT("CameraShake"), TEXT("MatineeCameraShake") };
	TSet<FName> ExcludedClassNames;
	TSet<FName> MatineeCameraShakeClassNames;
	AssetRegistry.GetDerivedClassNames(ClassNames, ExcludedClassNames, MatineeCameraShakeClassNames);

	return SNew(SMatineeCameraShakeConverterWidget)
		.MatineeCameraShakeClassNames(MatineeCameraShakeClassNames)
		.MatineeConverter(InMatineeConverter);
}

void FMatineeCameraShakeToNewCameraShakeConverter::ConvertMatineeCameraShakes(const TArray<FAssetData>& AssetDatas)
{
	using namespace UE::MovieScene;

	UFactory* CameraAnimationSequenceFactoryNew = FindFactoryForClass(UCameraAnimationSequence::StaticClass());

	bool bConvertSuccess = false;
	TOptional<bool> bAutoReuseExistingAsset;
	FMatineeCameraShakeToNewCameraShakeConversionStats Stats;
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FCameraAnimToTemplateSequenceConverter CameraAnimConverter(MatineeConverter);

	FScopedSlowTask SlowTask((float)AssetDatas.Num(), LOCTEXT("ConvertCameraShakes", "Converting camera shakes"));
	SlowTask.MakeDialog(true);

	for (const FAssetData& AssetData : AssetDatas)
	{
		SlowTask.EnterProgressFrame(1.f, FText::FromName(AssetData.PackageName));

		UBlueprint* ShakeBlueprint = Cast<UBlueprint>(AssetData.GetAsset());
		bool bCurConvertSuccess = ConvertSingleMatineeCameraShakeToNewCameraShakeSimple(
				CameraAnimConverter, AssetTools, AssetRegistry, CameraAnimationSequenceFactoryNew, ShakeBlueprint, bAutoReuseExistingAsset, Stats);
		bConvertSuccess = bCurConvertSuccess || bConvertSuccess;

		if (SlowTask.ShouldCancel())
		{
			break;
		}
	}

	ReportConversionStats(Stats, true);
}

void FMatineeCameraShakeToNewCameraShakeConverter::ReportConversionStats(const FMatineeCameraShakeToNewCameraShakeConversionStats& Stats, bool bPromptForCheckoutAndSave)
{
	UE_LOG(LogMatineeToLevelSequence, Log, TEXT("Conversion stats:"));
	UE_LOG(LogMatineeToLevelSequence, Log, TEXT("- Total converted assets: %d"), Stats.ConvertedPackages.Num());
	UE_LOG(LogMatineeToLevelSequence, Log, TEXT("- Total new assets: %d"), Stats.NewPackages.Num());
	UE_LOG(LogMatineeToLevelSequence, Log, TEXT("- Animations: %d"), Stats.NumAnims);

	if (bPromptForCheckoutAndSave)
	{
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Append(Stats.ConvertedPackages);
		PackagesToSave.Append(Stats.NewPackages);
		PackagesToSave.Append(Stats.ReusedPackages);
		UEditorLoadingAndSavingUtils::SavePackagesWithDialog(PackagesToSave, true);
		//FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, true, true);

		// Camera animation sequences that we reused could be local assets that existed before this whole operation,
		// like for instance from a previous run of the upgrade tool. In this case, the file would not be added to
		// source control by the above function. We need to fix up any problem, otherwise we run the risk of the 
		// user submitting an upgrade that is missing local assets.
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		if (ISourceControlModule::Get().IsEnabled() && SourceControlProvider.IsAvailable())
		{
			TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> ReusedPackagesStates;
			SourceControlProvider.GetState(Stats.ReusedPackages, ReusedPackagesStates, EStateCacheUsage::ForceUpdate);

			TArray<UPackage*> PackagesToMarkForAdd;
			for (int32 Index = 0; Index < ReusedPackagesStates.Num(); ++Index)
			{
				const TSharedRef<ISourceControlState, ESPMode::ThreadSafe>& MiscPackageState(ReusedPackagesStates[Index]);
				if (!MiscPackageState->IsSourceControlled() && !MiscPackageState->IsAdded())
				{
					if (ensure(MiscPackageState->CanAdd()))
					{
						PackagesToMarkForAdd.Add(Stats.ReusedPackages[Index]);
					}
				}
			}
			if (PackagesToMarkForAdd.Num() > 0)
			{
				SourceControlProvider.Execute(ISourceControlOperation::Create<FMarkForAdd>(), PackagesToMarkForAdd);
			}
		}
	}

	FText NotificationText = FText::Format(
			LOCTEXT("MatineeCameraShake_ConvertToSequenceCameraShake_Notification", "Converted {0} assets with {1} warnings"),
			FText::AsNumber(Stats.ConvertedPackages.Num()), FText::AsNumber(Stats.NumWarnings));
	FNotificationInfo NotificationInfo(NotificationText);
	NotificationInfo.ExpireDuration = 5.f;
	NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); });
	NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
	FSlateNotificationManager::Get().AddNotification(NotificationInfo);
}

bool FMatineeCameraShakeToNewCameraShakeConverter::ConvertSingleMatineeCameraShakeToNewCameraShakeSimple(FCameraAnimToTemplateSequenceConverter& CameraAnimConverter, IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, UBlueprint* OldShakeBlueprint, TOptional<bool>& bAutoReuseExistingAsset, FMatineeCameraShakeToNewCameraShakeConversionStats& Stats)
{
	if (!ensure(OldShakeBlueprint))
	{
		return false;
	}

	UMatineeCameraShake* OldCameraShake = GetMutableDefault<UMatineeCameraShake>(OldShakeBlueprint->GeneratedClass);
	if (!ensure(OldCameraShake))
	{
		return false;
	}

	if (!OldCameraShake->Anim)
	{
		// No camera anim to convert... we're done.
		return true;
	}

	bool bAssetCreated = false;
	UObject* CameraSequenceObj = CameraAnimConverter.ConvertCameraAnim(AssetTools, AssetRegistry, CameraAnimationSequenceFactoryNew, OldCameraShake->Anim, bAutoReuseExistingAsset, Stats.NumWarnings, bAssetCreated);
	if (bAssetCreated)
	{
		Stats.NewPackages.Add(CameraSequenceObj->GetPackage());
	}
	else
	{
		Stats.ReusedPackages.Add(CameraSequenceObj->GetPackage());
	}

	UCameraAnimationSequence* CameraSequence = Cast<UCameraAnimationSequence>(CameraSequenceObj);
	if (ensure(CameraSequence))
	{
		OldCameraShake->AnimSequence = CameraSequence;
		OldCameraShake->Anim = nullptr;
		OldCameraShake->Modify();

		++Stats.NumAnims;
		Stats.ConvertedPackages.Add(OldShakeBlueprint->GetPackage());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
