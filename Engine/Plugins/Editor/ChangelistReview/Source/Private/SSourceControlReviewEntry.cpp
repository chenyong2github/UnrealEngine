// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlReviewEntry.h"

#include "AssetToolsModule.h"
#include "ClassIconFinder.h"
#include "SourceControlHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Styling/StarshipCoreStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SourceControlReviewEntry"

namespace ReviewEntryConsts
{
	static const FString TempFolder = TEXT("/Temp/");
}

void SSourceControlReviewEntry::Construct(const FArguments& InArgs)
{
	static const UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Foundation/Fonts/NotoSans.NotoSans"));
	
	ChangelistFileData = InArgs._FileData;
	ChildSlot
	[
		SNew(SHorizontalBox)
		
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(10.f, 0.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(SourceActionIcon, SImage)
			.Image(this, &SSourceControlReviewEntry::GetSourceControlIcon)
		]
		
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0.f, 0.f, 5.f, 0.f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SSourceControlReviewEntry::GetAssetNameText)
				.Font(FStyleFonts::Get().Large)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &SSourceControlReviewEntry::GetLocalAssetPathText)
				.Font(FStyleFonts::Get().Small)
			]
		]
		
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(10.f, 0.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(32.f)
			.HeightOverride(32.f)
			[
				SAssignNew(AssetTypeIcon, SImage)
				.Image(GetAssetTypeIcon())
			]
		]
		
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(FileDeletedTextBlock, STextBlock)
		]
		
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(10.f, 0.f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(ReviewInputsBox, SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 5.f, 0.f)
			[
				SNew(SButton)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ViewDiffButtonText, STextBlock)
					.Justification(ETextJustify::Center)
					.Text(LOCTEXT("ViewDiffButton", "View Diff"))
				]
				.OnClicked(this, &SSourceControlReviewEntry::OnDiffClicked)
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 5.f, 0.f)
			[
				SNew(SButton)
				.IsEnabled(this, &SSourceControlReviewEntry::CanBrowseToAsset)
				.ContentPadding(FMargin(8.f, 4.f))
				.OnClicked(this, &SSourceControlReviewEntry::OnBrowseToAssetClicked)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Search"))
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text(LOCTEXT("BrowseToAssetButton", "Browse To Asset"))
					]
				]
			]
		]
	];

	// figure out how this asset diffs, and bind it to this->DiffMethod
	TryBindDiffMethod();
}

void SSourceControlReviewEntry::SetEntryData(const FChangelistFileData& InChangelistFileData)
{
	ChangelistFileData = InChangelistFileData;
	AssetTypeIcon->SetImage(GetAssetTypeIcon());

	// if asset changed, we might diff differently. rebind the diff method.
	DiffMethod.Unbind();
	TryBindDiffMethod();
}

FReply SSourceControlReviewEntry::OnDiffClicked() const
{
	DiffMethod.Execute();
	return FReply::Handled();
}

FReply SSourceControlReviewEntry::OnBrowseToAssetClicked() const
{
	TArray<FAssetData> Assets;
	USourceControlHelpers::GetAssetData(ChangelistFileData.AssetFilePath, Assets);
	const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	AssetToolsModule.Get().SyncBrowserToAssets(Assets);
	return FReply::Handled();
}

bool SSourceControlReviewEntry::CanBrowseToAsset() const
{
	TArray<FAssetData> Assets;
	return USourceControlHelpers::GetAssetData(ChangelistFileData.AssetFilePath, Assets);
}

void SSourceControlReviewEntry::TryBindDiffMethod()
{
	if (!DiffMethod.IsBound())
	{
		TryBindUAssetDiff();
	}
	if (!DiffMethod.IsBound())
	{
		TryBindTextDiff();
	}
}

bool SSourceControlReviewEntry::CanDiff() const
{
	return DiffMethod.IsBound();
}

void SSourceControlReviewEntry::TryBindUAssetDiff()
{
	if (UPackage* ReviewFilePkg = LoadPackage(nullptr, *ChangelistFileData.ReviewFileName, LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks))
	{
		if (UObject* ReviewAsset = FindObject<UObject>(ReviewFilePkg, *ChangelistFileData.AssetName))
		{
			UPackage* PreviousFilePkg = LoadPackage(nullptr, *ChangelistFileData.PreviousFileName, LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks);

			if (UObject* PreviousAsset = PreviousFilePkg ? FindObject<UObject>(PreviousFilePkg, *ChangelistFileData.AssetName) : nullptr)
			{
				DiffMethod.BindLambda([this, PreviousAsset, ReviewAsset]
				{
					FAssetToolsModule::GetModule().Get().DiffAssets(
						PreviousAsset,
						ReviewAsset,
						GetPreviousFileRevisionInfo(),
						GetReviewFileRevisionInfo()
					);
				});
				
			}
			else
			{
				if (const UBlueprint* BlueprintReviewAsset = Cast<UBlueprint>(ReviewAsset))
				{
					DiffMethod.BindLambda([this, BlueprintReviewAsset, ReviewAsset]
					{
						FAssetToolsModule::GetModule().Get().DiffAssets(
							GetOrCreateBlueprintForDiff(BlueprintReviewAsset->GeneratedClass, BlueprintReviewAsset->BlueprintType),
							ReviewAsset,
							GetPreviousFileRevisionInfo(),
							GetReviewFileRevisionInfo()
						);
					});
				}
				else
				{
					//In case when file is not blueprint we are falling back on trying to create a UObject 
					const FName EmptyObjectName = ReviewAsset->GetFName();
					const UClass* EmptyObjectAssetClass = ReviewAsset->GetClass();
					
					if (UObject* EmptyObject = NewObject<UObject>(ReviewAsset, EmptyObjectAssetClass, EmptyObjectName, ReviewAsset->GetFlags()))
					{
						DiffMethod.BindLambda([this, EmptyObject, ReviewAsset]
						{
							FAssetToolsModule::GetModule().Get().DiffAssets(
								EmptyObject,
								ReviewAsset,
								GetPreviousFileRevisionInfo(),
								GetReviewFileRevisionInfo()
							);
						});
					}
				}
			}
		}
	}
}

void SSourceControlReviewEntry::TryBindTextDiff()
{
	DiffMethod.BindLambda([this]
	{
		const FString& DiffCommand = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;
		const FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		AssetToolsModule.Get().CreateDiffProcess(DiffCommand, ChangelistFileData.PreviousFileName, ChangelistFileData.ReviewFileName);
	});
}

FRevisionInfo SSourceControlReviewEntry::GetReviewFileRevisionInfo() const
{
	FRevisionInfo ReviewFileRevisionInfo;
	ReviewFileRevisionInfo.Changelist = ChangelistFileData.ChangelistNum;
	ReviewFileRevisionInfo.Date = ChangelistFileData.ReviewFileDateTime;

	if (ChangelistFileData.ChangelistState == EChangelistState::Pending)
	{
		ReviewFileRevisionInfo.Revision = TEXT("Pending");
	}
	else
	{
		ReviewFileRevisionInfo.Revision = ChangelistFileData.ReviewFileRevisionNum;
	}

	return ReviewFileRevisionInfo;
}

FRevisionInfo SSourceControlReviewEntry::GetPreviousFileRevisionInfo() const
{
	//We need to have valid revision data for some DiffAssets implementations (Although for now we don't have full data on previous file we are showing correct previous revision information)
	FRevisionInfo PreviousFileRevisionInfo;
	PreviousFileRevisionInfo.Revision = ChangelistFileData.PreviousFileRevisionNum.IsEmpty() ? TEXT("0") : ChangelistFileData.PreviousFileRevisionNum;
	return PreviousFileRevisionInfo;
}

const FSlateBrush* SSourceControlReviewEntry::GetSourceControlIcon() const
{
	// setup lookup table for all the brushes so we don't have to re-call FAppStyle::Get().GetBrush every frame
	static TArray<const FSlateBrush*> Brushes = []()
	{
		TArray<const FSlateBrush*> Temp;
		Temp.AddZeroed(static_cast<uint8>(ESourceControlAction::ActionCount));
		
		//Source control images can be found at Engine\Content\Slate\Starship\SourceControl
		Temp[static_cast<uint8>(ESourceControlAction::Add)] = FAppStyle::Get().GetBrush(TEXT("SourceControl.Add"));
		Temp[static_cast<uint8>(ESourceControlAction::Edit)] = FAppStyle::Get().GetBrush(TEXT("SourceControl.Edit"));
		Temp[static_cast<uint8>(ESourceControlAction::Delete)] = FAppStyle::Get().GetBrush(TEXT("SourceControl.Delete"));
		Temp[static_cast<uint8>(ESourceControlAction::Branch)] = FAppStyle::Get().GetBrush(TEXT("SourceControl.Branch"));
		Temp[static_cast<uint8>(ESourceControlAction::Integrate)] = FAppStyle::Get().GetBrush(TEXT("SourceControl.Integrate"));
		Temp[static_cast<uint8>(ESourceControlAction::Unset)] = FAppStyle::Get().GetBrush(TEXT("SourceControl.Edit"));
		
		return MoveTemp(Temp);
	}();

	uint8 Index = static_cast<uint8>(ChangelistFileData.FileSourceControlAction);
	if (!Brushes.IsValidIndex(Index))
	{
		Index = static_cast<uint8>(ESourceControlAction::Unset);
	}
	return Brushes[Index];
}

const FSlateBrush* SSourceControlReviewEntry::GetAssetTypeIcon() const
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>( TEXT( "AssetRegistry" ) );
	
	FString TempPackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(ChangelistFileData.ReviewFileName, TempPackageName))
	{
		TArray<FAssetData> OutAssetData;
		AssetRegistryModule.Get().GetAssetsByPackageName( *TempPackageName, OutAssetData );
		
		if (OutAssetData.Num() > 0)
		{
			if (const FSlateBrush* FileTypeBrush = FClassIconFinder::FindThumbnailForClass(FClassIconFinder::GetIconClassForAssetData(OutAssetData[0])))
			{
				return FileTypeBrush;
			}
		}
	}
	
	return FAppStyle::Get().GetBrush(TEXT("NoBrush"));
}

FText SSourceControlReviewEntry::GetAssetNameText() const
{
	return FText::FromString(ChangelistFileData.AssetName);
}

FText SSourceControlReviewEntry::GetLocalAssetPathText() const
{
	return FText::FromString(ChangelistFileData.RelativeFilePath);
}

const FString& SSourceControlReviewEntry::GetSearchableString() const
{
	return ChangelistFileData.AssetName;
}

UBlueprint* SSourceControlReviewEntry::GetOrCreateBlueprintForDiff(UClass* InGeneratedClass, EBlueprintType InBlueprintType) const
{
	if (!InGeneratedClass || !InGeneratedClass->ClassGeneratedBy)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ReviewChangelistEntry", "Unable to show the diff for added file because generated class is not valid"));
		return nullptr;
	}
	
	const FString PackageName = ReviewEntryConsts::TempFolder / InGeneratedClass->GetName();
	const FName BPName = InGeneratedClass->GetFName();
	
	UPackage* BlueprintPackage = CreatePackage(*PackageName);
	BlueprintPackage->SetPackageFlags(PKG_ForDiffing);
	
	if (UBlueprint* ExistingBlueprint = FindObject<UBlueprint>(BlueprintPackage, *InGeneratedClass->GetName()))
	{
		return ExistingBlueprint;
	}

	UBlueprint* BlueprintObject = FKismetEditorUtilities::CreateBlueprint(InGeneratedClass, BlueprintPackage, BPName, InBlueprintType,
																		  InGeneratedClass->ClassGeneratedBy->GetClass(),
																		  InGeneratedClass->GetClass(), FName("DiffToolActions"));
	
	if (BlueprintObject)
	{
		FAssetRegistryModule::AssetCreated(BlueprintObject);
	}
	
	return BlueprintObject;
}

#undef LOCTEXT_NAMESPACE
