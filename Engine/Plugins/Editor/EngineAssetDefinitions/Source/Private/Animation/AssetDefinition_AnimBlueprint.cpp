// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AssetDefinition_AnimBlueprint.h"

#include "AnimationEditorUtils.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Widgets/Layout/SBorder.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Animation/AnimInstance.h"
#include "Factories/AnimBlueprintFactory.h"
#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "SBlueprintDiff.h"
#include "SSkeletonWidget.h"
#include "Styling/SlateIconFinder.h"
#include "IContentBrowserSingleton.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "IAnimationBlueprintEditorModule.h"
#include "IAssetTools.h"
#include "Algo/AllOf.h"
#include "Algo/NoneOf.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

namespace UE::AnimBlueprint
{
	bool ReplaceMissingSkeleton(const TArray<TObjectPtr<UObject>>& InAnimationAssets)
	{
		// record anim assets that need skeleton replaced
		const TArray<TWeakObjectPtr<UObject>> AnimsToFix = TArray<TWeakObjectPtr<UObject>>(InAnimationAssets);
		
		// get a skeleton from the user and replace it
		const TSharedPtr<SReplaceMissingSkeletonDialog> PickSkeletonWindow = SNew(SReplaceMissingSkeletonDialog).AnimAssets(AnimsToFix);
		const bool bWasSkeletonReplaced = PickSkeletonWindow.Get()->ShowModal();
		return bWasSkeletonReplaced;
	}

	EBlueprintType GetBlueprintType(const FAssetData& InAssetData)
	{
		const UEnum* BlueprintTypeEnum = StaticEnum<EBlueprintType>();
		const FString EnumString = InAssetData.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UBlueprint, BlueprintType));
		const EBlueprintType BlueprintType = (!EnumString.IsEmpty() ? static_cast<EBlueprintType>(BlueprintTypeEnum->GetValueByName(*EnumString)) : BPTYPE_Normal);
		return BlueprintType;
	}

	bool IsAnimBlueprintTemplate(const FAssetData& InAssetData)
	{
		return InAssetData.GetTagValueRef<bool>(GET_MEMBER_NAME_CHECKED(UAnimBlueprint, bIsTemplate));
	}

	TSoftObjectPtr<USkeleton> GetAnimBlueprintTargetSkeleton(const FAssetData& InAssetData)
	{
		return TSoftObjectPtr<USkeleton>(FSoftObjectPath(InAssetData.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UAnimBlueprint, TargetSkeleton))));
	}
}

FText UAssetDefinition_AnimBlueprint::GetAssetDisplayName(const FAssetData& AssetData) const
{
	FString OutBlueprintType;
	if (AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UBlueprint, BlueprintType), OutBlueprintType))
	{
		if (OutBlueprintType == TEXT("BPTYPE_Interface"))
		{
			return LOCTEXT("AssetTypeActions_AnimLayerInterface", "Animation Layer Interface");
		}
	}

	return Super::GetAssetDisplayName(AssetData);
}

void UAssetDefinition_AnimBlueprint::BuildFilters(TArray<FAssetFilterData>& OutFilters) const
{
	// Intentionally skipping UAssetDefinition_Blueprint.
	UAssetDefinition::BuildFilters(OutFilters);

	{
		FAssetFilterData Filter;
		Filter.Name = GetAssetClass().ToSoftObjectPath().ToString() + TEXT("Interface");
		Filter.DisplayText = LOCTEXT("AssetTypeActions_AnimLayerInterface", "Animation Layer Interface");
		Filter.FilterCategories = { EAssetCategoryPaths::Animation };
		Filter.Filter.ClassPaths.Add(GetAssetClass().ToSoftObjectPath().GetAssetPath());
		Filter.Filter.TagsAndValues.Add(GET_MEMBER_NAME_CHECKED(UBlueprint, BlueprintType), FString(TEXT("BPTYPE_Interface")));
		OutFilters.Add(MoveTemp(Filter));
	}
}

UFactory* UAssetDefinition_AnimBlueprint::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UAnimBlueprint* AnimBlueprint = CastChecked<UAnimBlueprint>(InBlueprint);

	if (InBlueprint->BlueprintType == BPTYPE_Interface)
	{
		return NewObject<UAnimLayerInterfaceFactory>();
	}
	else
	{
		UAnimBlueprintFactory* AnimBlueprintFactory = NewObject<UAnimBlueprintFactory>();
		AnimBlueprintFactory->ParentClass = TSubclassOf<UAnimInstance>(*InBlueprint->GeneratedClass);
		AnimBlueprintFactory->TargetSkeleton = AnimBlueprint->TargetSkeleton;
		AnimBlueprintFactory->bTemplate = AnimBlueprint->bIsTemplate;
		return AnimBlueprintFactory;
	}
}

TSharedPtr<SWidget> UAssetDefinition_AnimBlueprint::GetThumbnailOverlay(const FAssetData& AssetData) const
{
	const FSlateBrush* Icon = FSlateIconFinder::FindIconBrushForClass(UAnimBlueprint::StaticClass());

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetNoBrush())
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		[
			SNew(SImage)
			.Image(Icon)
		];
}

EAssetCommandResult UAssetDefinition_AnimBlueprint::PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
{
	UBlueprint* OldBlueprint = CastChecked<UBlueprint>(DiffArgs.OldAsset);
	UBlueprint* NewBlueprint = CastChecked<UBlueprint>(DiffArgs.NewAsset);

	// sometimes we're comparing different revisions of one single asset (other 
	// times we're comparing two completely separate assets altogether)
	bool bIsSingleAsset = (NewBlueprint->GetName() == OldBlueprint->GetName());

	FText WindowTitle = LOCTEXT("NamelessAnimationBlueprintDiff", "Animation Blueprint Diff");
	// if we're diffing one asset against itself 
	if (bIsSingleAsset)
	{
		// identify the assumed single asset in the window's title
		WindowTitle = FText::Format(LOCTEXT("AnimationBlueprintDiff", "{0} - Animation Blueprint Diff"), FText::FromString(NewBlueprint->GetName()));
	}

	SBlueprintDiff::CreateDiffWindow(WindowTitle, OldBlueprint, NewBlueprint, DiffArgs.OldRevision, DiffArgs.NewRevision);

	return EAssetCommandResult::Handled;
}


EAssetCommandResult UAssetDefinition_AnimBlueprint::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	TArray<FAssetData> OutAssetsThatFailedToLoad;
	for (UAnimBlueprint* AnimBlueprint : OpenArgs.LoadObjects<UAnimBlueprint>({}, &OutAssetsThatFailedToLoad))
	{
		if (AnimBlueprint->SkeletonGeneratedClass && AnimBlueprint->GeneratedClass)
		{
			if (AnimBlueprint->BlueprintType != BPTYPE_Interface && !AnimBlueprint->TargetSkeleton && !AnimBlueprint->bIsTemplate)
			{
				FText ShouldRetargetMessage = LOCTEXT("ShouldRetarget_Message", "Could not find the skeleton for Anim Blueprint '{BlueprintName}' Would you like to choose a new one?");
				
				FFormatNamedArguments Arguments;
				Arguments.Add( TEXT("BlueprintName"), FText::FromString(AnimBlueprint->GetName()));

				if (FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(ShouldRetargetMessage, Arguments)) == EAppReturnType::Yes)
				{
					TArray<TObjectPtr<UObject>> AssetsToRetarget;
					AssetsToRetarget.Add(AnimBlueprint);
					const bool bSkeletonReplaced = UE::AnimBlueprint::ReplaceMissingSkeleton(AssetsToRetarget);
					if (!bSkeletonReplaced)
					{
						return EAssetCommandResult::Handled; // Persona will crash if trying to load asset without a skeleton
					}
				}
				else
				{
					return EAssetCommandResult::Handled;
				}
			}
			
			const bool bBringToFrontIfOpen = true;
#if WITH_EDITOR
			if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(AnimBlueprint, bBringToFrontIfOpen))
			{
				EditorInstance->FocusWindow(AnimBlueprint);
			}
			else
#endif
			{
				IAnimationBlueprintEditorModule& AnimationBlueprintEditorModule = FModuleManager::LoadModuleChecked<IAnimationBlueprintEditorModule>("AnimationBlueprintEditor");
				AnimationBlueprintEditorModule.CreateAnimationBlueprintEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, AnimBlueprint);
			}
		}
		else
		{
			FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("FailedToLoadCorruptAnimBlueprint", "The Anim Blueprint could not be loaded because it is corrupt."));
		}
	}

	return EAssetCommandResult::Handled;
}

// Menu Extensions
//--------------------------------------------------------------------

namespace MenuExtension_AnimBlueprint
{
	void ExecuteFindSkeleton(const FToolMenuContext& InContext)
	{
		const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);

		TArray<UObject*> ObjectsToSync;
		for (UAnimBlueprint* AnimBlueprint : CBContext->LoadSelectedObjects<UAnimBlueprint>())
		{
			if (USkeleton* Skeleton = AnimBlueprint->TargetSkeleton)
			{
				ObjectsToSync.AddUnique(Skeleton);
			}
		}
    
		if ( ObjectsToSync.Num() > 0 )
		{
			IAssetTools::Get().SyncBrowserToAssets(ObjectsToSync);
		}
	}

	void ExecuteAssignSkeleton(const FToolMenuContext& InContext)
	{
		const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);

		TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
			.Title(LOCTEXT("ChooseSkeletonWindowTitle", "Choose Skeleton"))
			.ClientSize(FVector2D(400, 600));

		TSharedPtr<SSkeletonSelectorWindow> SkeletonSelectorWindow;
		WidgetWindow->SetContent
		(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SAssignNew(SkeletonSelectorWindow, SSkeletonSelectorWindow)
				.WidgetWindow(WidgetWindow)
			]
		);

		GEditor->EditorAddModalWindow(WidgetWindow);

		if (USkeleton* SelectedSkeleton = SkeletonSelectorWindow->GetSelectedSkeleton())
		{
			for (UAnimBlueprint* AnimBlueprint : CBContext->LoadSelectedObjects<UAnimBlueprint>())
			{
				if (AnimBlueprint->TargetSkeleton != SelectedSkeleton)
				{
					AnimBlueprint->Modify();
					AnimBlueprint->TargetSkeleton = SelectedSkeleton;
				}
			}
		}
	}

	void BuildNewSkeletonChildBlueprintMenu(UToolMenu* Menu, const FAssetData InAnimBlueprintAsset)
	{
		auto HandleAssetSelected = [InAnimBlueprintAsset](const FAssetData& InSelectedSkeletonAsset)
		{
			FSlateApplication::Get().DismissAllMenus();
			
			if (UAnimBlueprint* TargetParentBP = Cast<UAnimBlueprint>(InAnimBlueprintAsset.GetAsset()))
			{
				USkeleton* TargetSkeleton = CastChecked<USkeleton>(InSelectedSkeletonAsset.GetAsset());
				UClass* TargetParentClass = TargetParentBP->GeneratedClass;

				if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(TargetParentClass))
				{
					FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("InvalidClassToMakeBlueprintFrom", "Invalid class with which to make a Blueprint."));
					return;
				}

				FString Name;
				FString PackageName;
				IAssetTools::Get().CreateUniqueAssetName(TargetParentBP->GetOutermost()->GetName(), TEXT("_Child"), PackageName, Name);
				const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

				UAnimBlueprintFactory* AnimBlueprintFactory = NewObject<UAnimBlueprintFactory>();
				AnimBlueprintFactory->ParentClass = TSubclassOf<UAnimInstance>(*TargetParentBP->GeneratedClass);
				AnimBlueprintFactory->TargetSkeleton = TargetSkeleton;
				AnimBlueprintFactory->bTemplate = false;

				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().CreateNewAsset(Name, PackagePath, TargetParentBP->GetClass(), AnimBlueprintFactory);
			}
		};

		TSoftObjectPtr<USkeleton> AnimBlueprintSkeletonPtr = UE::AnimBlueprint::GetAnimBlueprintTargetSkeleton(InAnimBlueprintAsset);
		if (USkeleton* AnimBlueprintSkeleton = AnimBlueprintSkeletonPtr.Get())
		{
			TArray<FAssetData> CompatibleSkeletonAssets;
			AnimBlueprintSkeleton->GetCompatibleSkeletonAssets(CompatibleSkeletonAssets);

			TArray<FSoftObjectPath> CompatibleSkeletonPaths;
			Algo::Transform(CompatibleSkeletonAssets, CompatibleSkeletonPaths, [](const FAssetData& InAsset) { return InAsset.GetSoftObjectPath(); });
		

			FAssetPickerConfig AssetPickerConfig;
			AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateLambda([HandleAssetSelected](const TArray<FAssetData>& SelectedAssetData)
			{
				if (SelectedAssetData.Num() == 1)
				{
					HandleAssetSelected(SelectedAssetData[0]);
				}
			});
			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda(HandleAssetSelected);
			AssetPickerConfig.bAllowNullSelection = false;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
			AssetPickerConfig.Filter.SoftObjectPaths = MoveTemp(CompatibleSkeletonPaths);
		
			FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

			FToolMenuSection& InSection = Menu->AddSection("CompatibleSkeletonMenu", LOCTEXT("CompatibleSkeletonHeader", "Compatible Skeletons"));
			InSection.AddEntry(
				FToolMenuEntry::InitWidget("CompatibleSkeletonPicker",
					SNew(SBox)
					.WidthOverride(300.0f)
					.HeightOverride(300.0f)
					[
						ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
					],
					FText::GetEmpty()
				)
			);
		}
	}
		
	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []{ 
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UAnimBlueprint::StaticClass());
	        
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				if (const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection))
				{
					if (const FAssetData* SelectedAnimBlueprintPtr = CBContext->GetSingleSelectedAssetOfType(UAnimBlueprint::StaticClass(), EIncludeSubclasses::No))
					{
						// Accept (non-interface) template anim BPs or anim BPs with compatible skeletons
						if (SelectedAnimBlueprintPtr &&
							UE::AnimBlueprint::GetBlueprintType(*SelectedAnimBlueprintPtr) != BPTYPE_Interface &&
							(
								(UE::AnimBlueprint::GetAnimBlueprintTargetSkeleton(*SelectedAnimBlueprintPtr).IsNull() && UE::AnimBlueprint::IsAnimBlueprintTemplate(*SelectedAnimBlueprintPtr))
								||
								(!UE::AnimBlueprint::GetAnimBlueprintTargetSkeleton(*SelectedAnimBlueprintPtr).IsNull() /* && AnimBlueprint->TargetSkeleton->GetCompatibleSkeletons().Num() > 0 */)
							)
						)
						{
							InSection.AddSubMenu(
								"AnimBlueprint_NewSkeletonChildBlueprint",
								LOCTEXT("AnimBlueprint_NewSkeletonChildBlueprint", "Create Child Anim Blueprint with Skeleton"),
								LOCTEXT("AnimBlueprint_NewSkeletonChildBlueprint_Tooltip", "Create a child Anim Blueprint that uses a different compatible skeleton"),
								FNewToolMenuDelegate::CreateStatic(&BuildNewSkeletonChildBlueprintMenu, *SelectedAnimBlueprintPtr),
								false,
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Blueprint")
							);
						}
					}
					
					if (Algo::NoneOf(CBContext->SelectedAssets, &UE::AnimBlueprint::IsAnimBlueprintTemplate) &&
						Algo::AllOf(CBContext->SelectedAssets, [](const auto& AssetData) { return BPTYPE_Normal == UE::AnimBlueprint::GetBlueprintType(AssetData); } ))
					{
						{
							const TAttribute<FText> Label = LOCTEXT("AnimBlueprint_FindSkeleton", "Find Skeleton");
							const TAttribute<FText> ToolTip = LOCTEXT("AnimBlueprint_FindSkeletonTooltip", "Finds the skeleton used by the selected Anim Blueprints in the content browser.");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Find");
				
							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteFindSkeleton);
							InSection.AddMenuEntry("AnimBlueprint_FindSkeleton", Label, ToolTip, Icon, UIAction);
						}
						{
							const TAttribute<FText> Label = LOCTEXT("AnimBlueprint_AssignSkeleton", "Assign Skeleton");
							const TAttribute<FText> ToolTip = LOCTEXT("AnimBlueprint_AssignSkeletonTooltip", "Assigns a skeleton to the selected Animation Blueprint(s).");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Persona.AssetActions.AssignSkeleton");
				
							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteAssignSkeleton);
							InSection.AddMenuEntry("AnimBlueprint_AssignSkeleton", Label, ToolTip, Icon, UIAction);
						}
					}
				}
			}));
		}));
	});
}

#undef LOCTEXT_NAMESPACE
