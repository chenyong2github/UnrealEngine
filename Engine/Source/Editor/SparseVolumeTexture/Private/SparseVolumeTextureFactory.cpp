// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTextureFactory.h"

#include "SparseVolumeTexture/SparseVolumeTexture.h"

#if WITH_EDITOR

#include "SparseVolumeTextureOpenVDB.h"
#include "SparseVolumeTextureOpenVDBUtility.h"

#include "Serialization/EditorBulkDataWriter.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/FileHelper.h"

#include "Editor.h"

#include "OpenVDBImportWindow.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IMainFrameModule.h"

#define LOCTEXT_NAMESPACE "USparseVolumeTextureFactory"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureFactory, Log, All);

USparseVolumeTextureFactory::USparseVolumeTextureFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = true;
	SupportedClass = USparseVolumeTexture::StaticClass();

	Formats.Add(TEXT("vdb;OpenVDB Format"));
}

FText USparseVolumeTextureFactory::GetDisplayName() const
{
	return LOCTEXT("SparseVolumeTextureFactoryDescription", "Sparse Volume Texture");
}

bool USparseVolumeTextureFactory::ConfigureProperties()
{
	return true;
}

bool USparseVolumeTextureFactory::ShouldShowInNewMenu() const
{
	return false;
}


///////////////////////////////////////////////////////////////////////////////
// Create asset


bool USparseVolumeTextureFactory::CanCreateNew() const
{
	return false;	// To be able to import files and call 
}

UObject* USparseVolumeTextureFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	USparseVolumeTexture* Object = NewObject<USparseVolumeTexture>(InParent, InClass, InName, Flags);

	// SVT_TODO initialize similarly to UTexture2DFactoryNew

	return Object;
}


///////////////////////////////////////////////////////////////////////////////
// Import asset


bool USparseVolumeTextureFactory::DoesSupportClass(UClass* Class)
{
	return Class == USparseVolumeTexture::StaticClass();
}

UClass* USparseVolumeTextureFactory::ResolveSupportedClass()
{
	return USparseVolumeTexture::StaticClass();
}

bool USparseVolumeTextureFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);
	if (Extension == TEXT("vdb"))
	{
		return true;
	}
	return false;
}

void USparseVolumeTextureFactory::CleanUp()
{
	Super::CleanUp();
}

UObject* USparseVolumeTextureFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
	const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{

#if OPENVDB_AVAILABLE

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Parms);
	TArray<UObject*> ResultAssets;

	auto IsFilePotentiallyPartOfASequence = [](const FString& Filename)
	{
		// The file is potnetially a sequence of the character before the `.vdb` is a number.
		return FChar::IsDigit(Filename[Filename.Len() - 5]);
	};

	if (IsFilePotentiallyPartOfASequence(Filename))
	{
		// Import as an animated sparse volume texture asset.

		const FString FilenameWithoutExt = Filename.LeftChop(4);
		const int32 LastNonDigitIndex = FilenameWithoutExt.FindLastCharByPredicate([](TCHAR Letter) { return !FChar::IsDigit(Letter); }) + 1;
		const int32 DigitCount = FilenameWithoutExt.Len() - LastNonDigitIndex;
		FString FilenameWithoutSuffix = FilenameWithoutExt.LeftChop(FilenameWithoutExt.Len() - LastNonDigitIndex);
		TCHAR LastDigit = FilenameWithoutExt[FilenameWithoutExt.Len() - 5];

		bool IndexStartsAtOne = false;
		auto GetOpenVDBFileNameForFrame = [&](int32 FrameIndex)
		{
			FString IndexString = FString::FromInt(FrameIndex + (IndexStartsAtOne ? 1 : 0));
			// User must select a frame with index in [0-9] so that we can count leading 0s
			check(DigitCount==1 || (DigitCount>1 && IndexString.Len() <= DigitCount));
			const int32 MissingLeadingZeroCount = DigitCount - IndexString.Len();
			const FString StringZero = FString::FromInt(0);
			for (int32 i = 0; i < MissingLeadingZeroCount; ++i)
			{
				IndexString = StringZero + IndexString;
			}
			return FString(FilenameWithoutSuffix + IndexString) + TEXT(".vdb");
		};

		const FString VDBFileAt0 = GetOpenVDBFileNameForFrame(0);
		const FString VDBFileAt1 = GetOpenVDBFileNameForFrame(1);
		const bool VDBFileAt0Exists = FPaths::FileExists(VDBFileAt0);
		const bool VDBFileAt1Exists = FPaths::FileExists(VDBFileAt1);
		if (!VDBFileAt0Exists && !VDBFileAt1Exists)
		{
			UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("An OpenVDB animated sequence must start at index 0 or 1: %s or %s not found."), *VDBFileAt0, *VDBFileAt1);
			return nullptr;
		}
		IndexStartsAtOne = !VDBFileAt0Exists;

		FName NewName(InName .ToString()+ TEXT("VDBAnim"));
		UAnimatedSparseVolumeTexture* AnimatedSVTexture = NewObject<UAnimatedSparseVolumeTexture>(InParent, UAnimatedSparseVolumeTexture::StaticClass(), NewName, Flags);

		// Go over all the frame index and stop at the first missing one.
		int32 FrameCount = 0;
		while(FPaths::FileExists(GetOpenVDBFileNameForFrame(FrameCount)))
		{
			FrameCount++;
		}

		FScopedSlowTask ImportTask(FrameCount, LOCTEXT("ImportingVDBAnim", "Importing OpenVDB animation"));
		ImportTask.MakeDialog(true);

		// Allocate space for each frame
		AnimatedSVTexture->FrameCount = FrameCount;
		AnimatedSVTexture->AnimationFrames.SetNum(FrameCount);

		UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("Serializing: %i frame"), FrameCount);

		// Load vdb files for all frames.
		for(int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			const FString FrameFilename = GetOpenVDBFileNameForFrame(FrameIndex);
			TArray<uint8> LoadedFile;

			if (!FFileHelper::LoadFileToArray(LoadedFile, *FrameFilename))
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB file could not be opened: %s"), *FrameFilename);
				return nullptr;
			}

			uint32 DensityGridIndex = 0;
			FOpenVDBData OpenVDBData;
			if (!FindDensityGridIndex(LoadedFile, FrameFilename, &DensityGridIndex, &OpenVDBData))
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB file contains no suitable density grid: %s"), *FrameFilename);
				return nullptr;
			}

			UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("  - frame %i (active dimension %i x %i x %i)"), FrameIndex,
				OpenVDBData.VolumeActiveDim.X, OpenVDBData.VolumeActiveDim.Y, OpenVDBData.VolumeActiveDim.Z);

			FSparseVolumeRawSource SparseVolumeRawSource{};
			SparseVolumeRawSource.PackedDataA.Format = ESparseVolumePackedDataFormat::Unorm8;
			SparseVolumeRawSource.PackedDataA.SourceGridIndex = FUintVector4(0, INDEX_NONE, INDEX_NONE, INDEX_NONE);
			SparseVolumeRawSource.PackedDataA.SourceComponentIndex = FUintVector4(0, INDEX_NONE, INDEX_NONE, INDEX_NONE);
			SparseVolumeRawSource.SourceAssetFile = MoveTemp(LoadedFile);
			LoadedFile.Reset();

			// Serialize the raw source data from this frame into the asset object.
			{
				UE::Serialization::FEditorBulkDataWriter RawDataArchiveWriter(AnimatedSVTexture->AnimationFrames[FrameIndex].RawData);
				SparseVolumeRawSource.Serialize(RawDataArchiveWriter);
			}

			if (ImportTask.ShouldCancel())
			{
				return nullptr;
			}
			ImportTask.EnterProgressFrame(1.0f, LOCTEXT("ConvertingVDBAnim", "Converting OpenVDB animation"));
		}

		ResultAssets.Add(AnimatedSVTexture);
	}
	else
	{
		// Import as a static sparse volume texture asset.

		FName NewName(InName.ToString() + TEXT("VDB"));
		UStaticSparseVolumeTexture* StaticSVTexture = NewObject<UStaticSparseVolumeTexture>(InParent, UStaticSparseVolumeTexture::StaticClass(), NewName, Flags);

		// Load file and get info about each contained grid
		TArray<uint8> LoadedFile;
		TArray<TSharedPtr<FOpenVDBGridComponentInfo>> GridComponentInfoPtrs;
		TArray<TSharedPtr<ESparseVolumePackedDataFormat>> SupportedFormats =
		{ 
			MakeShared<ESparseVolumePackedDataFormat>(ESparseVolumePackedDataFormat::Float32),
			MakeShared<ESparseVolumePackedDataFormat>(ESparseVolumePackedDataFormat::Float16),
			MakeShared<ESparseVolumePackedDataFormat>(ESparseVolumePackedDataFormat::Unorm8)
		};
		FString FileInfoString;
		{
			if (!FFileHelper::LoadFileToArray(LoadedFile, *Filename))
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB file could not be loaded: %s"), *Filename);
				return nullptr;
			}

			TArray<FOpenVDBGridInfo> GridInfo;
			if (!GetOpenVDBGridInfo(LoadedFile, &GridInfo))
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("Failed to read OpenVDB file: %s"), *Filename);
				return nullptr;
			}
			if (GridInfo.IsEmpty())
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB file contains no grids: %s"), *Filename);
				return nullptr;
			}

			// Create individual entries for each component of all valid source grids
			TArray<FOpenVDBGridComponentInfo> GridComponentInfo;
			for (const FOpenVDBGridInfo& Grid : GridInfo)
			{
				// Append all grids to the string, even if we don't actually support them
				FileInfoString.Append(Grid.DisplayString);
				FileInfoString.AppendChar(TEXT('\n'));

				if (Grid.Type == EOpenVDBGridType::Unknown || !IsOpenVDBDataValid(Grid.OpenVDBData, Filename))
				{
					continue;
				}

				// Create one entry per component
				for (uint32 ComponentIdx = 0; ComponentIdx < Grid.NumComponents; ++ComponentIdx)
				{
					FOpenVDBGridComponentInfo ComponentInfo;
					ComponentInfo.Index = Grid.Index;
					ComponentInfo.ComponentIndex = ComponentIdx;
					ComponentInfo.Name = Grid.Name;

					const TCHAR* ComponentNames[] = { TEXT(".X"), TEXT(".Y"),TEXT(".Z"),TEXT(".W") };
					FStringFormatOrderedArguments FormatArgs;
					FormatArgs.Add(ComponentInfo.Index);
					FormatArgs.Add(ComponentInfo.Name);
					FormatArgs.Add(Grid.NumComponents == 1 ? TEXT("") : ComponentNames[ComponentIdx]);

					ComponentInfo.DisplayString = FString::Format(TEXT("{0}. {1}{2}"), FormatArgs);

					GridComponentInfo.Add(MoveTemp(ComponentInfo));
				}
			}

			if (GridComponentInfo.IsEmpty())
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB file contains no grids of supported type: %s"), *Filename);
				return nullptr;
			}

			// Convert to array of TSharedPtr. SComboBox requires this.
			GridComponentInfoPtrs.Empty(GridComponentInfo.Num() + 1);
			
			// We need a <None> option to leave channels empty
			FOpenVDBGridComponentInfo NoneGridComponentInfo;
			NoneGridComponentInfo.Index = INDEX_NONE;
			NoneGridComponentInfo.ComponentIndex = INDEX_NONE;
			NoneGridComponentInfo.Name = TEXT("<None>");
			NoneGridComponentInfo.DisplayString = TEXT("<None>");
			
			GridComponentInfoPtrs.Add(MakeShared<FOpenVDBGridComponentInfo>(NoneGridComponentInfo));
			
			for (const FOpenVDBGridComponentInfo& GridCompInfo : GridComponentInfo)
			{
				GridComponentInfoPtrs.Add(MakeShared<FOpenVDBGridComponentInfo>(GridCompInfo));
			}
		}

		// Show import dialog
		FSparseVolumeRawSourcePackedData PackedDataA{};
		{
			TSharedPtr<SWindow> ParentWindow;

			if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
			{
				IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
				ParentWindow = MainFrame.GetParentWindow();
			}

			// Compute centered window position based on max window size, which include when all categories are expanded
			const float ImportWindowWidth = 450.0f;
			const float ImportWindowHeight = 750.0f;
			FVector2D ImportWindowSize = FVector2D(ImportWindowWidth, ImportWindowHeight); // Max window size it can get based on current slate


			FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
			FVector2D DisplayTopLeft(WorkAreaRect.Left, WorkAreaRect.Top);
			FVector2D DisplaySize(WorkAreaRect.Right - WorkAreaRect.Left, WorkAreaRect.Bottom - WorkAreaRect.Top);

			float ScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(DisplayTopLeft.X, DisplayTopLeft.Y);
			ImportWindowSize *= ScaleFactor;

			FVector2D WindowPosition = (DisplayTopLeft + (DisplaySize - ImportWindowSize) / 2.0f) / ScaleFactor;

			TSharedRef<SWindow> Window = SNew(SWindow)
				.Title(NSLOCTEXT("UnrealEd", "OpenVDBImportOptionsTitle", "OpenVDB Import Options"))
				.SizingRule(ESizingRule::Autosized)
				.AutoCenter(EAutoCenter::None)
				.ClientSize(ImportWindowSize)
				.ScreenPosition(WindowPosition);

			TSharedPtr<SOpenVDBImportWindow> OpenVDBOptionWindow;
			Window->SetContent
			(
				SAssignNew(OpenVDBOptionWindow, SOpenVDBImportWindow)
				.PackedDataA(&PackedDataA)
				.OpenVDBGridComponentInfo(&GridComponentInfoPtrs)
				.FileInfoString(FileInfoString)
				.OpenVDBSupportedTargetFormats(&SupportedFormats)
				.WidgetWindow(Window)
				.FullPath(FText::FromString(Filename))
				.MaxWindowHeight(ImportWindowHeight)
				.MaxWindowWidth(ImportWindowWidth)
			);

			FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

			if (!OpenVDBOptionWindow->ShouldImport())
			{
				bOutOperationCanceled = true;
				return nullptr;
			}
		}

		FScopedSlowTask ImportTask(1.0f, LOCTEXT("ImportingVDBStatic", "Importing static OpenVDB"));
		ImportTask.MakeDialog(true);

		FSparseVolumeRawSource SparseVolumeRawSource{};
		SparseVolumeRawSource.PackedDataA = PackedDataA;
		SparseVolumeRawSource.SourceAssetFile = MoveTemp(LoadedFile);
		LoadedFile.Reset();

		// Serialize the raw source data into the asset object.
		{
			UE::Serialization::FEditorBulkDataWriter RawDataArchiveWriter(StaticSVTexture->StaticFrame.RawData);
			SparseVolumeRawSource.Serialize(RawDataArchiveWriter);
		}

		if (ImportTask.ShouldCancel())
		{
			return nullptr;
		}
		ImportTask.EnterProgressFrame(1.0f, LOCTEXT("ConvertingVDBStatic", "Converting static OpenVDB"));

		ResultAssets.Add(StaticSVTexture);
	}

	// Now notify the system about the imported/updated/created assets
	AdditionalImportedObjects.Reserve(ResultAssets.Num());
	for (UObject* Object : ResultAssets)
	{
		if (Object)
		{
			GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Object);
			Object->MarkPackageDirty();
			Object->PostEditChange();
			AdditionalImportedObjects.Add(Object);
		}
	}

	return (ResultAssets.Num() > 0) ? ResultAssets[0] : nullptr;

#else // OPENVDB_AVAILABLE

	// SVT_TODO Make sure we can also import on more platforms such as Linux. See SparseVolumeTextureOpenVDB.h
	UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("Cannot import OpenVDB asset any platform other than Windows."));
	return nullptr;

#endif // OPENVDB_AVAILABLE

}

#endif // WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE

#include "Serialization/EditorBulkDataWriter.h"
