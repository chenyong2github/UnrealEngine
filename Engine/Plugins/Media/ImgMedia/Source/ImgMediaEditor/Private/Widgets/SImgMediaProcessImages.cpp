// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SImgMediaProcessImages.h"

#include "Async/Async.h"
#include "Customizations/ImgMediaFilePathCustomization.h"
#include "EditorStyleSet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapperModule.h"
#include "IImgMediaModule.h"
#include "ImageWrapperHelper.h"
#include "ImgMediaEditorModule.h"
#include "ImgMediaProcessImagesOptions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SlateOptMacros.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#if IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
#include "OpenExrWrapper.h"
#endif // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM

#define LOCTEXT_NAMESPACE "ImgMediaProcessImages"

SImgMediaProcessImages::~SImgMediaProcessImages()
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SImgMediaProcessImages::Construct(const FArguments& InArgs)
{
	// Set up widgets.
	TSharedPtr<SBox> DetailsViewBox;
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
			.Padding(0, 20, 0, 0)
			.AutoHeight()

		// Add details view.
		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(DetailsViewBox, SBox)
			]
			
		// Add process images button.
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f)
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
					.OnClicked(this, &SImgMediaProcessImages::OnProcessImagesClicked)
					.Text(LOCTEXT("StartProcessImages", "Process Images"))
					.ToolTipText(LOCTEXT("ProcesssImagesButtonToolTip", "Start processing images."))
			]
	];

	// Create object with our options.
	Options = TStrongObjectPtr<UImgMediaProcessImagesOptions>(NewObject<UImgMediaProcessImagesOptions>(GetTransientPackage(), NAME_None));

	// Create detail view with our options.
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->RegisterInstancedCustomPropertyTypeLayout(FName(TEXT("FilePath")),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(FImgMediaFilePathCustomization::MakeInstance));
	DetailsView->SetObject(Options.Get());

	DetailsViewBox->SetContent(DetailsView->AsShared());
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SImgMediaProcessImages::OnProcessImagesClicked()
{
	// Create notification.
	FNotificationInfo Info(FText::GetEmpty());
	Info.bFireAndForget = false;
	TSharedPtr<SNotificationItem> ConfirmNotification = FSlateNotificationManager::Get().AddNotification(Info);

	// Start async task to process files.
	Async(EAsyncExecution::Thread, [this, ConfirmNotification]()
	{
		ProcessAllImages(ConfirmNotification);
	});

	return FReply::Handled();
}

void SImgMediaProcessImages::ProcessAllImages(TSharedPtr<SNotificationItem> ConfirmNotification)
{
	bool bUseCustomFormat = Options->bUseCustomFormat;
	int32 InTileWidth = Options->TileSizeX;
	int32 InTileHeight = Options->TileSizeY;
	int32 TileBorder = Options->TileBorder;

	// Create output directory.
	FString OutPath = Options->OutputPath.Path;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutPath);

	// Get source files.
	FString SequencePath = FPaths::GetPath(Options->SequencePath.FilePath);
	
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
			int TotalNum = FoundFiles.Num();
			for (const FString& FileName : FoundFiles)
			{
				// Update notification with current status.
				Async(EAsyncExecution::TaskGraphMainThread, [ConfirmNotification, NumDone, TotalNum]()
				{
					if (ConfirmNotification.IsValid())
					{
						ConfirmNotification->SetText(
							FText::Format(LOCTEXT("ImgMediaCompleted", "ImgMedia Completed {0}/{1}"),
								FText::AsNumber(NumDone), FText::AsNumber(TotalNum)));
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
				FString Name = FPaths::Combine(OutPath, FileName);
				if (bUseCustomFormat)
				{
					ProcessImageCustom(ImageWrapper, InTileWidth, InTileHeight, TileBorder, Name);
				}
				else
				{
					Name = FPaths::ChangeExtension(Name, TEXT(""));
					ProcessImage(ImageWrapper, InTileWidth, InTileHeight, Name, Ext);
				}
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

void SImgMediaProcessImages::ProcessImage(TSharedPtr<IImageWrapper>& InImageWrapper,
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

void SImgMediaProcessImages::ProcessImageCustom(TSharedPtr<IImageWrapper>& InImageWrapper,
	int32 InTileWidth, int32 InTileHeight, int32 InTileBorder, const FString& InName)
{
#if IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
	// Get image data.
	ERGBFormat Format = InImageWrapper->GetFormat();
	int32 Width = InImageWrapper->GetWidth();
	int32 Height = InImageWrapper->GetHeight();
	int32 DestWidth = Width;
	int32 DestHeight = Height;
	int32 BitDepth = InImageWrapper->GetBitDepth();
	TArray64<uint8> RawData;
	InImageWrapper->GetRaw(Format, BitDepth, RawData);

	int32 NumTilesX = InTileWidth > 0 ? Width / InTileWidth : 1;
	int32 NumTilesY = InTileHeight > 0 ? Height / InTileHeight : 1;
	int32 TileWidth = Width / NumTilesX;
	int32 TileHeight = Height / NumTilesY;
	int32 BytesPerPixel = RawData.Num() / (Width * Height);

	bool bIsTiled = (NumTilesX > 1) || (NumTilesY > 1);
	uint8* RawDataPtr = RawData.GetData();

	// Tile data.
	TArray64<uint8> RawDataTiled;
	if (bIsTiled)
	{
		// We don't support tile borders larger than a tile size,
		// but this shuld not happen in practice.
		if ((InTileBorder > TileWidth) || (InTileBorder > TileHeight))
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("Tile border is larger than tile size. Clamping to tile size."));
			InTileBorder = FMath::Min(TileWidth, TileHeight);
		}

		DestWidth = Width + InTileBorder * 2 * NumTilesX;
		DestHeight = Height + InTileBorder * 2 * NumTilesY;
		RawDataTiled.AddUninitialized(DestWidth * DestHeight * BytesPerPixel);
		RawDataPtr = RawDataTiled.GetData();

		uint8* SourceData = RawData.GetData();
		uint8* DestData = RawDataTiled.GetData();
		int32 DestTileWidth = TileWidth + InTileBorder * 2;
		int32 DestTileHeight = TileHeight + InTileBorder * 2;
		int32 BytesPerTile = TileWidth * TileHeight * BytesPerPixel;
		int32 ByterPerDestTile = DestTileWidth * DestTileHeight * BytesPerPixel;

		// Loop over y tiles.
		for (int32 TileY = 0; TileY < NumTilesY; ++TileY)
		{
			// Loop over x tiles.
			for (int32 TileX = 0; TileX < NumTilesX; ++TileX)
			{
				// Get address of the source and destination tiles.
				uint8* SourceTile = SourceData +
					(TileX * TileWidth + TileY * Width * TileHeight) * BytesPerPixel;
				uint8* DestTile = DestData + (TileX + TileY * NumTilesX) * ByterPerDestTile;
				
				int32 NumberOfPixelsToCopy = TileWidth;

				// Create a left border.
				if (TileX > 0)
				{
					NumberOfPixelsToCopy += InTileBorder;
					// Offset the source to get the extra pixels.
					SourceTile -= InTileBorder * BytesPerPixel;
				}
				else
				{
					// Offset the destination as we are skipping this border as we have no data.
					DestTile += InTileBorder * BytesPerPixel;
				}

				// Create a right border.
				if (TileX < NumTilesX - 1)
				{
					NumberOfPixelsToCopy += InTileBorder;
				}
				
				// Loop over each row in the tile.
				for (int32 Row = 0; Row < DestTileHeight; ++Row)
				{
					// Make sure we don't go beyond the source data.
					int32 SourceRow = Row - InTileBorder;
					if (TileY == 0)
					{
						SourceRow = FMath::Max(SourceRow, 0);
					}
					if (TileY == NumTilesY - 1)
					{
						SourceRow = FMath::Min(SourceRow, TileHeight - 1);
					}

					uint8* SourceLine = SourceTile + SourceRow * Width * BytesPerPixel;
					uint8* DestLine = DestTile + Row * DestTileWidth * BytesPerPixel;
					
					// Copy the main data.
					FMemory::Memcpy(DestLine, SourceLine, NumberOfPixelsToCopy * BytesPerPixel);
				}
			}
		}
	}

	// Names for our channels.
	const FString RChannelName = FString(TEXT("R"));
	const FString GChannelName = FString(TEXT("G"));
	const FString BChannelName = FString(TEXT("B"));
	const FString AChannelName = FString(TEXT("A"));

	int32 NumChannels = 4;

	FIntPoint Stride(2, DestWidth * BytesPerPixel);

	// Create tiled exr file.
	FTiledOutputFile OutFile(FIntPoint(0, 0), FIntPoint(DestWidth - 1, DestHeight - 1),
		FIntPoint(0, 0), FIntPoint(DestWidth - 1, DestHeight - 1));

	// Add attributes.
	OutFile.AddIntAttribute(IImgMediaModule::CustomFormatAttributeName.Resolve().ToString(), 1);
	OutFile.AddIntAttribute(IImgMediaModule::CustomFormatTileWidthAttributeName.Resolve().ToString(),
		bIsTiled ? TileWidth : 0);
	OutFile.AddIntAttribute(IImgMediaModule::CustomFormatTileHeightAttributeName.Resolve().ToString(),
		bIsTiled ? TileHeight : 0);

	// Add channels.
	OutFile.AddChannel(AChannelName);
	OutFile.AddChannel(BChannelName);
	OutFile.AddChannel(GChannelName);
	OutFile.AddChannel(RChannelName);

	// Create output.
	OutFile.CreateOutputFile(InName, DestWidth, DestHeight, false);
	OutFile.AddFrameBufferChannel(AChannelName, RawDataPtr, Stride);
	OutFile.AddFrameBufferChannel(BChannelName, RawDataPtr + DestWidth * 2, Stride);
	OutFile.AddFrameBufferChannel(GChannelName, RawDataPtr + DestWidth * 4, Stride);
	OutFile.AddFrameBufferChannel(RChannelName, RawDataPtr + DestWidth * 6, Stride);
	OutFile.SetFrameBuffer();
	
	OutFile.WriteTile(0, 0, 0);

#else // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
	UE_LOG(LogImgMediaEditor, Error, TEXT("EXR not supported on this platform."));
#endif // IMGMEDIAEDITOR_EXR_SUPPORTED_PLATFORM
}

#undef LOCTEXT_NAMESPACE
