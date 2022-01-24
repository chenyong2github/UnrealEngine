// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSourceCustomizationImportInfo.h"

#include "Async/Async.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformFileManager.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IImageWrapperModule.h"
#include "ImageWrapperHelper.h"
#include "IMediaModule.h"
#include "ImgMediaSourceCustomization.h"
#include "ImgMediaEditorModule.h"
#include "ImgMediaSource.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FImgMediaSourceCustomizationImportInfo"

/* IPropertyTypeCustomization interface
 *****************************************************************************/

void FImgMediaSourceCustomizationImportInfo::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	HeaderRow
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("SequencePathImport", "Import Sequence"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
						.ForegroundColor(FSlateColor::UseForeground())
						.OnClicked(this, &FImgMediaSourceCustomizationImportInfo::OnImportClicked)
						.ToolTipText(LOCTEXT("SequencePathImportButtonToolTip", "Import files for this sequence and generate tiles and/or mips if requested."))
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image(FEditorStyle::GetBrush("Icons.Adjust"))
						]
				]
		];
}

void FImgMediaSourceCustomizationImportInfo::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// Add tiles.
	IDetailGroup& TileGroup = StructBuilder.AddGroup(FName(TEXT("TilesGroup")), LOCTEXT("TileGroup_DisplayName", "Tiles"));
	
	// Add tile width.
	TileGroup.AddWidgetRow()
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("ImportTileWidth", "Width"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SNumericEntryBox<int32>)
				.Value(this, &FImgMediaSourceCustomizationImportInfo::GetTileWidth)
				.OnValueChanged(this, &FImgMediaSourceCustomizationImportInfo::SetTileWidth)
		];

	// Add tile height.
	TileGroup.AddWidgetRow()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ImportTileHeight", "Height"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SNumericEntryBox<int32>)
				.Value(this, &FImgMediaSourceCustomizationImportInfo::GetTileHeight)
				.OnValueChanged(this, &FImgMediaSourceCustomizationImportInfo::SetTileHeight)
		];
}

/* FImgMediaSourceCustomizationImport implementation
 *****************************************************************************/

TOptional<int32> FImgMediaSourceCustomizationImportInfo::GetTileWidth() const
{
	return TileWidth;
}

void FImgMediaSourceCustomizationImportInfo::SetTileWidth(int32 InWidth)
{
	TileWidth = InWidth;
}

TOptional<int32> FImgMediaSourceCustomizationImportInfo::GetTileHeight() const
{
	return TileHeight;
}

void FImgMediaSourceCustomizationImportInfo::SetTileHeight(int32 InHeight)
{
	TileHeight = InHeight;
}

FReply FImgMediaSourceCustomizationImportInfo::OnImportClicked()
{
	// Create notification.
	FNotificationInfo Info(FText::GetEmpty());
	Info.bFireAndForget = false;
	TSharedPtr<SNotificationItem> ConfirmNotification = FSlateNotificationManager::Get().AddNotification(Info);

	// Start async task to import files.
	Async(EAsyncExecution::Thread, [this, ConfirmNotification]()
	{
		FString SequencePath = FImgMediaSourceCustomization::GetSequencePathFromChildProperty(PropertyHandle);
		ImportFiles(SequencePath, ConfirmNotification, TileWidth, TileHeight);
	});

	return FReply::Handled();
}

void FImgMediaSourceCustomizationImportInfo::ImportFiles(const FString& SequencePath,
	TSharedPtr<SNotificationItem> ConfirmNotification,
	int32 InTileWidth, int32 InTileHeight)
{
	// Create output directory.
	FString OutPath = FPaths::Combine(SequencePath, TEXT("Imported"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutPath);

	// Get source files.
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *SequencePath, TEXT("*"));
	FoundFiles.Sort();
	UE_LOG(LogImgMediaEditor, Warning, TEXT("Found %i image files in %s to import."), FoundFiles.Num(), *SequencePath);
	if (FoundFiles.Num() == 0)
	{
		UE_LOG(LogImgMediaEditor, Error, TEXT("No files to import."));
	}
	else
	{
		// Create image wrapper
		FString Ext = FPaths::GetExtension(FoundFiles[0]);
		EImageFormat ImageFormat = ImageWrapperHelper::GetImageFormat(Ext);

		if (ImageFormat == EImageFormat::Invalid)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("Invalid file format %s"), *Ext);
		}
		else
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

			// Loop through all files.
			int NumDone = 0;
			for (const FString& FileName : FoundFiles)
			{
				// Update notification with current status.
				int PercentageDone = (100 * NumDone) / FoundFiles.Num();
				Async(EAsyncExecution::TaskGraphMainThread, [ConfirmNotification, PercentageDone]()
				{
					if (ConfirmNotification.IsValid())
					{
						ConfirmNotification->SetText(
							FText::Format(LOCTEXT("ImgMediaCompleted", "ImgMedia Completed {0}%"),
								FText::AsNumber(PercentageDone)));
					}
				});
				NumDone++;

				FString FullFileName = FPaths::Combine(SequencePath, FileName);

				// Load image into buffer.
				TArray64<uint8> InputBuffer;
				if (!FFileHelper::LoadFileToArray(InputBuffer, *FullFileName))
				{
					UE_LOG(LogImgMediaEditor, Error, TEXT("Failed to load %s"), *FullFileName);
					break;
				}
				if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(InputBuffer.GetData(), InputBuffer.Num()))
				{
					UE_LOG(LogImgMediaEditor, Error, TEXT("Failed to create image wrapper for %s"), *FullFileName);
					break;
				}

				// Import this image.
				FString Name = FPaths::ChangeExtension(FPaths::Combine(OutPath, FileName), TEXT(""));
				ImportImage(ImageWrapper, InTileWidth, InTileHeight, Name, Ext);
			}
		}
	}

	// Close notification. Must be run on the main thread.
	Async(EAsyncExecution::TaskGraphMainThread, [ConfirmNotification]()
	{
		if (ConfirmNotification.IsValid())
		{
			ConfirmNotification->SetEnabled(false);
			ConfirmNotification->SetCompletionState(SNotificationItem::CS_Success);
			ConfirmNotification->ExpireAndFadeout();
		}
	});
}

void FImgMediaSourceCustomizationImportInfo::ImportImage(
	TSharedPtr<IImageWrapper>& InImageWrapper, 
	int32 InTileWidth, int32 InTileHeight, const FString& InName, const FString& FileExtension)
{
	// Get image data.
	ERGBFormat Format = InImageWrapper->GetFormat();
	int32 Width = InImageWrapper->GetWidth();
	int32 Height = InImageWrapper->GetHeight();
	int32 BitDepth = InImageWrapper->GetBitDepth();
	TArray64<uint8> RawData;
	InImageWrapper->GetRaw(Format, BitDepth, RawData);

	int32 NumTilesX = InTileWidth > 0 ? Width / InTileWidth : 1;
	int32 NumTilesY = InTileHeight > 0 ? Height / InTileHeight : 1;
	int32 TileWidth = Width / NumTilesX;
	int32 TileHeight = Height / NumTilesY;
	int32 BytesPerPixel = RawData.Num() / (Width * Height);
	TArray64<uint8> TileRawData;
	TileRawData.AddZeroed(TileWidth * TileHeight * BytesPerPixel);
	bool bIsTiled = (NumTilesX > 1) || (NumTilesY > 1);

	// Create a directory if we have tiles.
	FString FileName;
	if (bIsTiled)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*InName);
		FileName = FPaths::Combine(InName, FPaths::GetCleanFilename(InName));;
	}
	else
	{
		FileName = InName;
	}
	
	// Loop over y tiles.
	for (int TileY = 0; TileY < NumTilesY; ++TileY)
	{
		// Loop over x tiles.
		for (int TileX = 0; TileX < NumTilesX; ++TileX)
		{
			// Copy tile line by line.
			uint8* DestPtr = TileRawData.GetData();
			uint8* SrcPtr = RawData.GetData() + TileX * TileWidth * BytesPerPixel +
				TileY * TileHeight * Width * BytesPerPixel;
			for (int LineY = 0; LineY < TileHeight; ++LineY)
			{
				FMemory::Memcpy(DestPtr, SrcPtr,
					TileWidth * BytesPerPixel);
				DestPtr += TileWidth * BytesPerPixel;
				SrcPtr += Width * BytesPerPixel;
			}
			
			// Compress data.
			InImageWrapper->SetRaw(TileRawData.GetData(), TileRawData.Num(),
				TileWidth, TileHeight, Format, BitDepth);
			const TArray64<uint8> CompressedData = InImageWrapper->GetCompressed((int32)EImageCompressionQuality::Uncompressed);
			
			// Write out tile.
			FString Name = FString::Format(TEXT("{0}_x{1}_y{2}.{3}"),
				{*FileName, TileX, TileY, *FileExtension});
			FFileHelper::SaveArrayToFile(CompressedData, *Name);
		}
	}
}

#undef LOCTEXT_NAMESPACE
