// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditorDetailCustomization_ImportExport.h"
#include "SlateOptMacros.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/SToolTip.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "DesktopPlatformModule.h"

#include "LandscapeEditorObject.h"
#include "LandscapeEditorModule.h"
#include "LandscapeImportHelper.h"

#define LOCTEXT_NAMESPACE "LandscapeEditor.ImportExport"

TSharedRef<IDetailCustomization> FLandscapeEditorDetailCustomization_ImportExport::MakeInstance()
{
	return MakeShareable(new FLandscapeEditorDetailCustomization_ImportExport);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorDetailCustomization_ImportExport::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	if (!IsToolActive("ImportExport"))
	{
		return;
	}

	DetailBuilder.HideCategory("New Landscape");
	IDetailCategoryBuilder& ImportExportCategory = DetailBuilder.EditCategory("Import / Export");

	ImportExportCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(10, 2))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SCheckBox)
			.Style(FEditorStyle::Get(), "RadioButton")
			.IsChecked_Static(&FLandscapeEditorDetailCustomization_ImportExport::ModeIsChecked, EImportExportMode::Import)
			.OnCheckStateChanged_Static(&FLandscapeEditorDetailCustomization_ImportExport::OnModeChanged, EImportExportMode::Import)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Import", "Import"))
			]
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SCheckBox)
			.Style(FEditorStyle::Get(), "RadioButton")
			.IsChecked_Static(&FLandscapeEditorDetailCustomization_ImportExport::ModeIsChecked, EImportExportMode::Export)
			.OnCheckStateChanged_Static(&FLandscapeEditorDetailCustomization_ImportExport::OnModeChanged, EImportExportMode::Export)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Export", "Export"))
			]
		]
	];

	TSharedRef<IPropertyHandle> PropertyHandle_ImportHeightmapFilename = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, ImportLandscape_HeightmapFilename));
	TSharedRef<IPropertyHandle> PropertyHandle_ExportHeightmapFilename = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, HeightmapExportFilename));
	
	PropertyHandle_ImportHeightmapFilename->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([PropertyHandle_ImportHeightmapFilename]()
	{
		FLandscapeEditorDetailCustomization_ImportExport::FormatFilename(PropertyHandle_ImportHeightmapFilename);
		FLandscapeEditorDetailCustomization_ImportExport::OnImportHeightmapFilenameChanged();
	}));

	PropertyHandle_ExportHeightmapFilename->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([PropertyHandle_ExportHeightmapFilename]()
	{
		FLandscapeEditorDetailCustomization_ImportExport::FormatFilename(PropertyHandle_ExportHeightmapFilename);
	}));

	TSharedRef<IPropertyHandle> PropertyHandle_HeightmapImportResult = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, ImportLandscape_HeightmapImportResult));
	TSharedRef<IPropertyHandle> PropertyHandle_HeightmapErrorMessage = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, ImportLandscape_HeightmapErrorMessage));
	DetailBuilder.HideProperty(PropertyHandle_HeightmapImportResult);
	DetailBuilder.HideProperty(PropertyHandle_HeightmapErrorMessage);
		
	TSharedRef<IPropertyHandle> PropertyHandle_ExportEditLayer = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, bExportEditLayer));

	auto AddHeightmapFileName = [&](TSharedRef<IPropertyHandle> PropertyHandle_HeightmapFilename, TAttribute<EVisibility> PropertyVisibility)
	{
		return ImportExportCategory.AddProperty(PropertyHandle_HeightmapFilename)
			.Visibility(PropertyVisibility)
			.CustomWidget()
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Static(&FLandscapeEditorDetailCustomization_ImportExport::GetHeightmapSelectedCheckState)
					.OnCheckStateChanged_Static(&FLandscapeEditorDetailCustomization_ImportExport::OnHeightmapSelectedCheckStateChanged)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				[
					PropertyHandle_HeightmapFilename->CreatePropertyNameWidget()
				]
			]
			.ValueContent()
			.MinDesiredWidth(250.0f)
			.MaxDesiredWidth(0)
			[
				SNew(SHorizontalBox)
				.IsEnabled_Static(&FLandscapeEditorDetailCustomization_ImportExport::IsHeightmapEnabled)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				[
					SNew(SErrorText)
					.Visibility_Static(&GetImportResultErrorVisibility, PropertyHandle_HeightmapImportResult)
					.BackgroundColor_Static(&GetImportResultErrorColor, PropertyHandle_HeightmapImportResult)
					.ErrorText(NSLOCTEXT("UnrealEd", "Error", "!"))
					.ToolTip(
						SNew(SToolTip)
						.Text_Static(&GetPropertyValue<FText>, PropertyHandle_HeightmapErrorMessage)
					)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				[
					SNew(SEditableTextBox)
					.Font(DetailBuilder.GetDetailFont())
					.Text_Static(&GetPropertyValueText, PropertyHandle_HeightmapFilename)
					.OnTextCommitted_Static(&SetFilename, PropertyHandle_HeightmapFilename)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1, 0, 0, 0)
				[
					SNew(SButton)
					.ContentPadding(FMargin(4, 0))
					.Text(NSLOCTEXT("UnrealEd", "GenericOpenDialog", "..."))
					.OnClicked_Static(&OnBrowseFilenameButtonClicked, PropertyHandle_HeightmapFilename)
				]
			];
	};

	AddHeightmapFileName(PropertyHandle_ExportHeightmapFilename, MakeAttributeLambda([]() { return FLandscapeEditorDetailCustomization_ImportExport::GetImportExportVisibility(false); }));
	AddHeightmapFileName(PropertyHandle_ImportHeightmapFilename, MakeAttributeLambda([]() { return FLandscapeEditorDetailCustomization_ImportExport::GetImportExportVisibility(true); }));

	TSharedRef<IPropertyHandle> PropertyHandle_ImportType = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, ImportType));
	ImportExportCategory.AddProperty(PropertyHandle_ImportType).Visibility(MakeAttributeLambda([]() { return FLandscapeEditorDetailCustomization_ImportExport::GetImportExportVisibility(true); }));

	TSharedRef<IPropertyHandle> PropertyHandle_FlipYAxis = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, bFlipYAxis));
	ImportExportCategory.AddProperty(PropertyHandle_FlipYAxis).Visibility(MakeAttributeLambda([]() { return (IsImporting() && !GetEditorMode()->UseSingleFileImport()) ? EVisibility::Visible : EVisibility::Collapsed; }));

	ImportExportCategory.AddProperty(PropertyHandle_ExportEditLayer).Visibility(MakeAttributeLambda([this]()
	{
		if (ULandscapeInfo* LandscapeInfo = GetEditorMode()->CurrentToolTarget.LandscapeInfo.Get())
		{
			if (!LandscapeInfo->CanHaveLayersContent())
			{
				return EVisibility::Collapsed;
			}
		}

		return FLandscapeEditorDetailCustomization_ImportExport::GetImportExportVisibility(false);
	}));

	ImportExportCategory.AddCustomRow(LOCTEXT("ImportResolution", "Import Resolution"))
		.Visibility(MakeAttributeLambda([]() { return FLandscapeEditorDetailCustomization_ImportExport::GetImportExportVisibility(true); }))
		.NameContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0, 0, 2, 0))
			[
				SNew(STextBlock)
				.Font(DetailBuilder.GetDetailFont())
				.Text(LOCTEXT("ImportResolution", "Import Resolution"))
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.Padding(FMargin(0, 0, 12, 0)) // Line up with the other properties due to having no reset to default button
			[
				SNew(SComboButton)
				.OnGetMenuContent_Static(&FLandscapeEditorDetailCustomization_ImportExport::GetImportLandscapeResolutionMenu)
				.ContentPadding(2)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(DetailBuilder.GetDetailFont())
					.Text_Static(&FLandscapeEditorDetailCustomization_ImportExport::GetImportLandscapeResolution)
				]
			]
		];

	TSharedRef<IPropertyHandle> PropertyHandle_Layers = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeEditorObject, ImportLandscape_Layers));
	ImportExportCategory.AddProperty(PropertyHandle_Layers);

	ImportExportCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text_Static(&GetImportExportButtonText)
			.OnClicked_Static(&OnImportExportButtonClicked)
			.IsEnabled_Static(&GetImportExportButtonIsEnabled)
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

EVisibility FLandscapeEditorDetailCustomization_ImportExport::GetImportExportVisibility(bool bImport)
{
	return IsImporting() == bImport ? EVisibility::Visible : EVisibility::Collapsed;
}

bool FLandscapeEditorDetailCustomization_ImportExport::IsHeightmapEnabled()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	if (LandscapeEdMode->UISettings)
	{
		return LandscapeEdMode->UISettings->bHeightmapSelected;
	}

	return false;
}

ECheckBoxState FLandscapeEditorDetailCustomization_ImportExport::GetHeightmapSelectedCheckState()
{
	return IsHeightmapEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FLandscapeEditorDetailCustomization_ImportExport::OnHeightmapSelectedCheckStateChanged(ECheckBoxState CheckState)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	if (LandscapeEdMode->UISettings)
	{
		if (CheckState != ECheckBoxState::Undetermined)
		{
			LandscapeEdMode->UISettings->bHeightmapSelected = CheckState == ECheckBoxState::Checked;
		}
	}
}

ECheckBoxState FLandscapeEditorDetailCustomization_ImportExport::ModeIsChecked(EImportExportMode Value)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	return LandscapeEdMode->ImportExportMode == Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FLandscapeEditorDetailCustomization_ImportExport::OnModeChanged(ECheckBoxState NewCheckedState, EImportExportMode Value)
{
	if (NewCheckedState == ECheckBoxState::Checked)
	{
		FEdModeLandscape* LandscapeEdMode = GetEditorMode();
		check(LandscapeEdMode != nullptr);
		LandscapeEdMode->ImportExportMode = Value;
	}
}

EVisibility FLandscapeEditorDetailCustomization_ImportExport::GetImportResultErrorVisibility(TSharedRef<IPropertyHandle> PropertyHandle_Result)
{
	ELandscapeImportResult ImportResult;
	FPropertyAccess::Result Result = PropertyHandle_Result->GetValue((uint8&)ImportResult);

	if (Result == FPropertyAccess::Fail)
	{
		return EVisibility::Collapsed;
	}

	if (Result == FPropertyAccess::MultipleValues)
	{
		return EVisibility::Visible;
	}

	if (ImportResult != ELandscapeImportResult::Success)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

FSlateColor FLandscapeEditorDetailCustomization_ImportExport::GetImportResultErrorColor(TSharedRef<IPropertyHandle> PropertyHandle_Result)
{
	ELandscapeImportResult ImportResult;
	FPropertyAccess::Result Result = PropertyHandle_Result->GetValue((uint8&)ImportResult);

	if (Result == FPropertyAccess::Fail ||
		Result == FPropertyAccess::MultipleValues)
	{
		return FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor");
	}

	switch (ImportResult)
	{
	case ELandscapeImportResult::Success:
		return FCoreStyle::Get().GetColor("InfoReporting.BackgroundColor");
	case ELandscapeImportResult::Warning:
		return FCoreStyle::Get().GetColor("ErrorReporting.WarningBackgroundColor");
	case ELandscapeImportResult::Error:
		return FCoreStyle::Get().GetColor("ErrorReporting.BackgroundColor");
	default:
		check(0);
		return FSlateColor();
	}
}

void FLandscapeEditorDetailCustomization_ImportExport::SetFilename(const FText& NewValue, ETextCommit::Type CommitInfo, TSharedRef<IPropertyHandle> PropertyHandle_Filename)
{
	FString Filename = NewValue.ToString();
	ensure(PropertyHandle_Filename->SetValue(Filename) == FPropertyAccess::Success);
}

FReply FLandscapeEditorDetailCustomization_ImportExport::OnBrowseFilenameButtonClicked(TSharedRef<IPropertyHandle> PropertyHandle_Filename)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);

	// Prompt the user for the Filenames
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr)
	{
		bool bSuccess = false;
		TArray<FString> Filenames;
		ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");
		if (IsImporting())
		{
			bSuccess = DesktopPlatform->OpenFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				LOCTEXT("ImportHeightmap", "Import Heightmap").ToString(),
				LandscapeEdMode->UISettings->LastImportPath,
				TEXT(""),
				LandscapeEditorModule.GetHeightmapImportDialogTypeString(),
				EFileDialogFlags::None,
				Filenames);
		}
		else
		{
			bSuccess = DesktopPlatform->SaveFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				LOCTEXT("ExportHeightmap", "Export Heightmap").ToString(),
				LandscapeEdMode->UISettings->LastImportPath,
				TEXT(""),
				LandscapeEditorModule.GetHeightmapExportDialogTypeString(),
				EFileDialogFlags::None,
				Filenames);
		}

		if (bSuccess)
		{
			ensure(PropertyHandle_Filename->SetValue(Filenames[0]) == FPropertyAccess::Success);
			LandscapeEdMode->UISettings->LastImportPath = FPaths::GetPath(Filenames[0]);
		}
	}

	return FReply::Handled();
}

void FLandscapeEditorDetailCustomization_ImportExport::OnImportHeightmapFilenameChanged()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	LandscapeEdMode->UISettings->OnImportHeightmapFilenameChanged();
}

void FLandscapeEditorDetailCustomization_ImportExport::FormatFilename(TSharedRef<IPropertyHandle> PropertyHandle_Filename)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	if (!LandscapeEdMode->UseSingleFileImport())
	{
		// If selected file as the _xN_yM suffix remove it
		FString	FilePath;
		if (PropertyHandle_Filename->GetValue(FilePath) == FPropertyAccess::Success)
		{
			FIntPoint OutCoord;
			FString OutBaseFilePattern;
			if (FLandscapeImportHelper::ExtractCoordinates(FPaths::GetBaseFilename(FilePath), OutCoord, OutBaseFilePattern))
			{
				PropertyHandle_Filename->SetValue(FString::Format(TEXT("{0}/{1}{2}"), { FPaths::GetPath(FilePath), OutBaseFilePattern, FPaths::GetExtension(FilePath, true) }));
			}
		}
	}
}

bool FLandscapeEditorDetailCustomization_ImportExport::IsImporting()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	return LandscapeEdMode->ImportExportMode == EImportExportMode::Import;
}

FText FLandscapeEditorDetailCustomization_ImportExport::GetImportExportButtonText()
{
	if (IsImporting())
	{
		return LOCTEXT("Import", "Import");
	}

	return LOCTEXT("Export", "Export");
}

FReply FLandscapeEditorDetailCustomization_ImportExport::OnImportExportButtonClicked()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode);
	if (ULandscapeInfo* LandscapeInfo = LandscapeEdMode->CurrentToolTarget.LandscapeInfo.Get())
	{
		FIntRect LandscapeExtent;
		if (!LandscapeInfo->GetLandscapeExtent(LandscapeExtent))
		{
			return FReply::Handled();
		}
	
		if (IsImporting())
		{
			FGuid CurrentLayerGuid = LandscapeEdMode->GetCurrentLayerGuid();
						
			const ELandscapeLayerPaintingRestriction PaintRestriction = ELandscapeLayerPaintingRestriction::None;
						
			ELandscapeImportTransformType TransformType = LandscapeEdMode->UISettings->ImportType;
									
			FVector LocalGizmoPosition = LandscapeInfo->GetLandscapeProxy()->LandscapeActorToWorld().InverseTransformPosition(LandscapeEdMode->CurrentGizmoActor->GetActorLocation());
			FIntPoint LocalGizmoPoint = FIntPoint(FMath::FloorToInt(LocalGizmoPosition.X), FMath::FloorToInt(LocalGizmoPosition.Y));
			
			// Update Gizmo Position if we exit and comeback into tool
			LandscapeEdMode->UISettings->ImportLandscape_GizmoLocalPosition = LocalGizmoPoint;

			FIntRect ImportRegion = LandscapeExtent;
			FIntPoint ImportOffset(0, 0);
			if (TransformType == ELandscapeImportTransformType::ExpandOffset)
			{
				ImportOffset = LocalGizmoPoint - FIntPoint(LandscapeExtent.Min.X, LandscapeExtent.Min.Y);
			}
			else if (TransformType == ELandscapeImportTransformType::None)
			{
				ImportRegion = FIntRect(LocalGizmoPoint.X, LocalGizmoPoint.Y, LocalGizmoPoint.X + LandscapeEdMode->UISettings->ImportLandscape_Width, LocalGizmoPoint.Y + LandscapeEdMode->UISettings->ImportLandscape_Height);
			}
			
			// Import Heightmap
			if (LandscapeEdMode->UISettings->bHeightmapSelected)
			{
				check(LandscapeEdMode->UISettings->ImportLandscape_HeightmapImportResult != ELandscapeImportResult::Error);
				LandscapeEdMode->ImportHeightData(LandscapeInfo, CurrentLayerGuid, LandscapeEdMode->UISettings->ImportLandscape_HeightmapFilename, ImportRegion, TransformType, ImportOffset, PaintRestriction, LandscapeEdMode->UISettings->bFlipYAxis);
			}

			for (const FLandscapeImportLayer& ImportLayer : LandscapeEdMode->UISettings->ImportLandscape_Layers)
			{
				if (ImportLayer.bSelected)
				{
					check(ImportLayer.ImportResult != ELandscapeImportResult::Error);
					LandscapeEdMode->ImportWeightData(LandscapeInfo, CurrentLayerGuid, ImportLayer.LayerInfo, ImportLayer.SourceFilePath, ImportRegion, TransformType, ImportOffset, PaintRestriction, LandscapeEdMode->UISettings->bFlipYAxis);
				}
			}
		}
		else
		{
			auto BuildExportFileName = [](const FString& Filename, FIntPoint FileOffset, bool bUseOffset)
			{
				if (bUseOffset)
				{
					FString Extension = FPaths::GetExtension(Filename, true);
					FString BaseFilename = FPaths::GetBaseFilename(Filename, false);
					return FString::Format(TEXT("{0}_x{1}_y{2}{3}"), { BaseFilename, FString::FromInt(FileOffset.X), FString::FromInt(FileOffset.Y), Extension });
				}
				
				return Filename;
			};

			FScopedSetLandscapeEditingLayer Scope(LandscapeInfo->LandscapeActor.Get(), LandscapeEdMode->UISettings->bExportEditLayer ? LandscapeEdMode->GetCurrentLayerGuid() : FGuid());

			LandscapeInfo->ForAllLandscapeProxies([LandscapeInfo, LandscapeEdMode, LandscapeExtent, BuildExportFileName](ALandscapeProxy* LandscapeProxy)
			{
				FIntRect ExportRegion;
				if (LandscapeInfo->GetLandscapeExtent(LandscapeProxy, ExportRegion))
				{
					FIntPoint FileOffset;
					if (!LandscapeEdMode->UseSingleFileImport())
					{
						ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get();
						check(Landscape);
						FileOffset = FIntPoint((ExportRegion.Min.X - LandscapeExtent.Min.X) / Landscape->GridSize,
								(ExportRegion.Min.Y - LandscapeExtent.Min.Y) / Landscape->GridSize);

						// Remove the shared line/column that this proxy has with its neighbors because it 
						// will be included by the neighbor or lost if there is none (that could become an option to avoid that loss)
						ExportRegion.Max.X -= 1;
						ExportRegion.Max.Y -= 1;
					}
											

					if (LandscapeEdMode->UISettings->bHeightmapSelected)
					{
						FString ExportFilename = BuildExportFileName(LandscapeEdMode->UISettings->HeightmapExportFilename, FileOffset, !LandscapeEdMode->UseSingleFileImport());
						LandscapeInfo->ExportHeightmap(ExportFilename, ExportRegion);
					}
					
					for (const FLandscapeImportLayer& ImportLayer : LandscapeEdMode->UISettings->ImportLandscape_Layers)
					{
						if (ImportLayer.bSelected)
						{
							FString ExportFilename = BuildExportFileName(ImportLayer.ExportFilePath, FileOffset, !LandscapeEdMode->UseSingleFileImport());
							LandscapeInfo->ExportLayer(ImportLayer.LayerInfo, ExportFilename, ExportRegion);
						}
					}
				}
			});
			
		}
	}

	return FReply::Handled();
}

bool FLandscapeEditorDetailCustomization_ImportExport::GetImportExportButtonIsEnabled()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode);

	bool bHasOneSelection = false;
	if (LandscapeEdMode->UISettings->bHeightmapSelected)
	{
		bHasOneSelection = true;
		if (IsImporting())
		{
			if (LandscapeEdMode->UISettings->ImportLandscape_HeightmapImportResult == ELandscapeImportResult::Error || LandscapeEdMode->UISettings->ImportLandscape_HeightmapFilename.IsEmpty())
			{
				return false;
			}
		}
		else
		{
			if (LandscapeEdMode->UISettings->HeightmapExportFilename.IsEmpty())
			{
				return false;
			}
		}
	}

	for (const FLandscapeImportLayer& ImportLayer : LandscapeEdMode->UISettings->ImportLandscape_Layers)
	{
		if (ImportLayer.bSelected)
		{
			bHasOneSelection = true;
			
			if (IsImporting())
			{
				if (ImportLayer.ImportResult == ELandscapeImportResult::Error || ImportLayer.SourceFilePath.IsEmpty())
				{
					return false;
				}
			}
			else
			{
				if (ImportLayer.ExportFilePath.IsEmpty())
				{
					return false;
				}
			}
		}
	}

	return bHasOneSelection;
}

TSharedRef<SWidget> FLandscapeEditorDetailCustomization_ImportExport::GetImportLandscapeResolutionMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	for (int32 i = 0; i < LandscapeEdMode->UISettings->HeightmapImportDescriptor.ImportResolutions.Num(); i++)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), LandscapeEdMode->UISettings->HeightmapImportDescriptor.ImportResolutions[i].Width);
		Args.Add(TEXT("Height"), LandscapeEdMode->UISettings->HeightmapImportDescriptor.ImportResolutions[i].Height);
		MenuBuilder.AddMenuEntry(FText::Format(LOCTEXT("ImportResolution_Format", "{Width}\u00D7{Height}"), Args), FText(), FSlateIcon(), FExecuteAction::CreateStatic(&FLandscapeEditorDetailCustomization_ImportExport::OnChangeImportLandscapeResolution, i));
	}
	
	return MenuBuilder.MakeWidget();
}

void FLandscapeEditorDetailCustomization_ImportExport::OnChangeImportLandscapeResolution(int32 Index)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	LandscapeEdMode->UISettings->OnChangeImportLandscapeResolution(Index);
}

FText FLandscapeEditorDetailCustomization_ImportExport::GetImportLandscapeResolution()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	check(LandscapeEdMode != nullptr);
	const int32	Width = LandscapeEdMode->UISettings->ImportLandscape_Width;
	const int32	Height = LandscapeEdMode->UISettings->ImportLandscape_Height;
		
	if (Width != 0 && Height != 0)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Width"), Width);
		Args.Add(TEXT("Height"), Height);
		return FText::Format(LOCTEXT("ImportResolution_Format", "{Width}\u00D7{Height}"), Args);
	}
	else
	{
		return LOCTEXT("ImportResolution_Invalid", "(invalid)");
	}
	
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
