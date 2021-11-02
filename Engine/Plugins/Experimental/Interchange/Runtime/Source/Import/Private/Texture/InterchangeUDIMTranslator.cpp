// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeUDIMTranslator.h"

#include "InterchangeManager.h"
#include "InterchangeTexture2DNode.h"
#include "Texture/InterchangeTexturePayloadData.h"
#include "Texture/InterchangeTexturePayloadInterface.h"

#include "UDIMUtilities.h"
#include "Async/ParallelFor.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			namespace InterchangeUDIMTranslator
			{
				class FScopedTranslatorsAndSourceData
				{
				public:
					FScopedTranslatorsAndSourceData(UInterchangeTranslatorBase& BaseTranslator, uint32 NumRequired)
					{
						UObject* TransientPackage = GetTransientPackage();

						TranslatorsAndSourcesData.Reserve(NumRequired);
						TranslatorsAndSourcesData.Emplace(&BaseTranslator, NewObject<UInterchangeSourceData>(TransientPackage, UInterchangeSourceData::StaticClass()));
						
						UClass* TranslatorClass = BaseTranslator.GetClass();
						for (uint32 Index = 1; Index < NumRequired; Index++)
						{
							TranslatorsAndSourcesData.Emplace(
								  NewObject<UInterchangeTranslatorBase>(TransientPackage, TranslatorClass, NAME_None)
								, NewObject<UInterchangeSourceData>(TransientPackage, UInterchangeSourceData::StaticClass(), NAME_None)
								);

						}
					};

					~FScopedTranslatorsAndSourceData()
					{
						for (TPair<UInterchangeTranslatorBase*,UInterchangeSourceData*>& TranslatorAndSourceData : TranslatorsAndSourcesData)
						{
							TranslatorAndSourceData.Key->ClearInternalFlags(EInternalObjectFlags::Async);
							TranslatorAndSourceData.Value->ClearInternalFlags(EInternalObjectFlags::Async);
						}
					};

					TPair<UInterchangeTranslatorBase*,UInterchangeSourceData*>& operator[](uint32 Index)
					{
						return TranslatorsAndSourcesData[Index];
					}

					const TPair<UInterchangeTranslatorBase*,UInterchangeSourceData*>& operator[](uint32 Index) const
					{
						return TranslatorsAndSourcesData[Index];
					}

					uint32 Num() const
					{
						return TranslatorsAndSourcesData.Num();
					}

				private:
					TArray<TPair<UInterchangeTranslatorBase*,UInterchangeSourceData*>> TranslatorsAndSourcesData;
				};
		
				class FScopedClearAsyncFlag
				{
				public:
					FScopedClearAsyncFlag(UObject* InObject)
						: Object(nullptr)
					{
						if (InObject && InObject->HasAnyInternalFlags(EInternalObjectFlags::Async))
						{
							Object = InObject;
						}
					}

					~FScopedClearAsyncFlag()
					{
						if (Object)
						{
							Object->ClearInternalFlags(EInternalObjectFlags::Async);
						}
					}

				private:
					UObject* Object;
				};
			}
		}
	}
}


UInterchangeUDIMTranslator::UInterchangeUDIMTranslator()
	: Super()
	, IInterchangeBlockedTexturePayloadInterface()
	, UdimRegexPattern( UE::TextureUtilitiesCommon::DefaultUdimRegexPattern )
{
}

bool UInterchangeUDIMTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	// 1) Parse the file name for a UDIM pattern
	const FString CurrentFilename = InSourceData->GetFilename();

	const FString FilenameNoExtension = FPaths::GetBaseFilename(CurrentFilename);

	FString PreUDIMName;
	FString PostUDIMName;
	const int32 BaseUDIMIndex = UE::TextureUtilitiesCommon::ParseUDIMName(FilenameNoExtension, UdimRegexPattern, PreUDIMName, PostUDIMName);
	const FString BaseUDIMName = PreUDIMName + PostUDIMName;

	if (BaseUDIMIndex != INDEX_NONE)
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

		// 2) Check if the current the file can be imported
		if (UInterchangeTranslatorBase* Translator = InterchangeManager.GetTranslatorSupportingPayloadInterfaceForSourceData(InSourceData, UInterchangeTexturePayloadInterface::StaticClass()))
		{
			UE::Interchange::Private::InterchangeUDIMTranslator::FScopedClearAsyncFlag AsyncFlagGuard(Translator);
			// 3) If another file with the pattern can be imported
			// Filter for other potential UDIM pages, with the same base name and file extension
			const FString Path = FPaths::GetPath(CurrentFilename);
			const FString UDIMFilter = (Path / PreUDIMName) + TEXT("*") + PostUDIMName + FPaths::GetExtension(CurrentFilename, true);

			TArray<FString> UDIMFiles;
			constexpr bool bFindFiles = true;
			constexpr bool bFindDirectory = false;
			IFileManager::Get().FindFiles(UDIMFiles, *UDIMFilter, bFindFiles, bFindDirectory);

			for (const FString& UDIMFile : UDIMFiles)
			{
				if (!CurrentFilename.EndsWith(UDIMFile))
				{
					const int32 UDIMIndex = UE::TextureUtilitiesCommon::ParseUDIMName(FPaths::GetBaseFilename(UDIMFile), UdimRegexPattern, PreUDIMName, PostUDIMName);
					if (UDIMIndex != BaseUDIMIndex && UDIMIndex != INDEX_NONE)
					{
						const FString UDIMName = PreUDIMName + PostUDIMName;
						if (UDIMName == BaseUDIMName)
						{

							if (Translator->CanImportSourceData(InSourceData))
							{
								return true;
							}
						}
					}
				}
			}
			// If it fail log a warning / info maybe?

		}
	}

	return false;
}

bool UInterchangeUDIMTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	FString Filename = GetSourceData()->GetFilename();
	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	UInterchangeTexture2DNode* TextureNode = NewObject<UInterchangeTexture2DNode>(&BaseNodeContainer, NAME_None);

	if (!ensure(TextureNode))
	{
		return false;
	}

	FString PreUDIMName;
	FString PostUDIMName;
	UE::TextureUtilitiesCommon::ParseUDIMName(FPaths::GetBaseFilename(Filename), UdimRegexPattern, PreUDIMName, PostUDIMName);

	FString DisplayLabel = PreUDIMName + PostUDIMName;

	TextureNode->InitializeNode(Filename, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset);
	TextureNode->SetPayLoadKey(Filename);

	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
	UInterchangeSourceData* TempSourceData = NewObject<UInterchangeSourceData>(GetTransientPackage(), UInterchangeSourceData::StaticClass());
	UE::Interchange::Private::InterchangeUDIMTranslator::FScopedClearAsyncFlag AsyncFlagGuard(TempSourceData);

	if (UInterchangeTranslatorBase* Translator = InterchangeManager.GetTranslatorSupportingPayloadInterfaceForSourceData(GetSourceData(), UInterchangeTexturePayloadInterface::StaticClass()))
	{
		UE::Interchange::Private::InterchangeUDIMTranslator::FScopedClearAsyncFlag AsyncFlagGuardTranslator(Translator);

		const FString Path = FPaths::GetPath(Filename);
		const FString UDIMFilter = (Path / PreUDIMName) + TEXT("*") + PostUDIMName + FPaths::GetExtension(Filename, true);

		TMap<int32, FString> UDIMsAndSourcesFile;
		{
			TArray<FString> UDIMFiles;
			IFileManager::Get().FindFiles(UDIMFiles, *UDIMFilter, true, false);
			UDIMFiles.Reserve(UDIMFiles.Num());

			for (FString& UDIMFile : UDIMFiles)
			{
				const int32 UDIMIndex = UE::TextureUtilitiesCommon::ParseUDIMName(FPaths::GetBaseFilename(UDIMFile), UdimRegexPattern, PreUDIMName, PostUDIMName);
				if (UDIMIndex != INDEX_NONE)
				{
					const FString UDIMName = PreUDIMName + PostUDIMName;
					FString UDIMPath = FPaths::Combine(Path, UDIMFile);
					TempSourceData->SetFilename(UDIMPath);
					if (Translator->CanImportSourceData(TempSourceData))
					{
						UDIMsAndSourcesFile.Add(UDIMIndex, MoveTemp(UDIMPath));
					}
					// else log warning message?
				}
			}
		}
		TextureNode->SetSourceBlocks(UDIMsAndSourcesFile);
	}

	BaseNodeContainer.AddNode(TextureNode);
	
	return true;
}


TOptional<UE::Interchange::FImportBlockedImage> UInterchangeUDIMTranslator::GetBlockedTexturePayloadData(const TMap<int32, FString>& InSourcesData,  const UInterchangeSourceData* OriginalFile) const
{
	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	if (UInterchangeTranslatorBase* Translator = InterchangeManager.GetTranslatorSupportingPayloadInterfaceForSourceData(OriginalFile, UInterchangeTexturePayloadInterface::StaticClass()))
	{
		UE::Interchange::Private::InterchangeUDIMTranslator::FScopedClearAsyncFlag AsyncFlagGuard(Translator);
		TArray<const TPair<int32, FString>*> UDIMsAndSourcesFileArray;
		UDIMsAndSourcesFileArray.Reserve(InSourcesData.Num());
		for (const TPair<int32, FString>& Pair : InSourcesData)
		{
			UDIMsAndSourcesFileArray.Add(&Pair);
		}

		if (UDIMsAndSourcesFileArray.Num() > 1)
		{
			using namespace UE::Interchange::Private::InterchangeUDIMTranslator;
			FScopedTranslatorsAndSourceData TranslatorsAndSourceData(*Translator, UDIMsAndSourcesFileArray.Num());

			/**
			 * Possible improvement notes.
			 * If we are able at some point to extract the size and format of the textures from the translate step for some formats,
			 * We could use those information to init the blocked image RawData and set the ImportsImages RawData to be a view into the blocked image.
			 */ 
			TArray<UE::Interchange::FImportImage> Images;
			Images.AddDefaulted(UDIMsAndSourcesFileArray.Num());

			ParallelFor(UDIMsAndSourcesFileArray.Num(), [&TranslatorsAndSourceData, &UDIMsAndSourcesFileArray, &Images](int32 Index)
				{
					TPair<UInterchangeTranslatorBase*,UInterchangeSourceData*>& TranslatorAndSourceData = TranslatorsAndSourceData[Index];
					UInterchangeTranslatorBase* Translator = TranslatorAndSourceData.Key;
					IInterchangeTexturePayloadInterface* TextureTranslator = CastChecked<IInterchangeTexturePayloadInterface>(TranslatorAndSourceData.Key);
					UInterchangeSourceData* SourceDataForBlock = TranslatorAndSourceData.Value;

					const TPair<int32, FString>& UDIMAndFilename = *UDIMsAndSourcesFileArray[Index];
					const int32 CurrentUDIM = UDIMAndFilename.Key;
					SourceDataForBlock->SetFilename(UDIMAndFilename.Value);
				
					TOptional<UE::Interchange::FImportImage> Payload;
					if (Translator->CanImportSourceData(SourceDataForBlock))
					{
						Translator->SourceData = SourceDataForBlock;
						Payload = TextureTranslator->GetTexturePayloadData(SourceDataForBlock, SourceDataForBlock->GetFilename());
					}

					if (Payload.IsSet())
					{
						UE::Interchange::FImportImage& Image = Payload.GetValue();
						// Consume the image
						Images[Index] = MoveTemp(Image);
					}
					else
					{
						// Todo Capture the error message from the translator?
					}

					// Let the translator release his data.
					Translator->ImportFinish();
				}
				, EParallelForFlags::Unbalanced | EParallelForFlags::BackgroundPriority);


			UE::Interchange::FImportBlockedImage BlockedImage;
			// If the image that triggered the import is not valid. We shouldn't import the rest of the images into the UDIM.
			if (BlockedImage.InitDataSharedAmongBlocks(Images[0]))
			{
				BlockedImage.BlocksData.Reserve(Images.Num());

				for (int32 Index = 0; Index < Images.Num(); ++Index)
				{
					int32 UDIMIndex = UDIMsAndSourcesFileArray[Index]->Key;
					const int32 BlockX = (UDIMIndex - 1001) % 10;
					const int32 BlockY = (UDIMIndex - 1001) / 10;
					BlockedImage.InitBlockFromImage(BlockX, BlockY, Images[Index]);
				}

				BlockedImage.MigrateDataFromImagesToRawData(Images);

				if (BlockedImage.BlocksData.Num() > 0)
				{
					return BlockedImage;
				}
				
			}
		}

		// Import as a non blocked image
		if (IInterchangeTexturePayloadInterface* TextureTranslator = CastChecked<IInterchangeTexturePayloadInterface>(Translator))
		{
			TOptional<UE::Interchange::FImportImage> Payload = TextureTranslator->GetTexturePayloadData(OriginalFile, OriginalFile->GetFilename());
			if (Payload.IsSet())
			{
				UE::Interchange::FImportImage& Image = Payload.GetValue();

				UE::Interchange::FImportBlockedImage BlockedImage;
				if (BlockedImage.InitDataSharedAmongBlocks(Image))
				{
					const FString FilenameNoExtension = FPaths::GetBaseFilename(OriginalFile->GetFilename());

					FString PreUDIMName;
					FString PostUDIMName;
					const int32 UDIMIndex = UE::TextureUtilitiesCommon::ParseUDIMName(FilenameNoExtension, UdimRegexPattern, PreUDIMName, PostUDIMName);

					int32 BlockX;
					int32 BlockY;

					UE::TextureUtilitiesCommon::ExtractUDIMCoordinates(UDIMIndex, BlockX, BlockY);

					BlockedImage.InitBlockFromImage(BlockX, BlockY, Image);

					// Consume the payload
					BlockedImage.RawData = MoveTemp(Image.RawData);
		
					Translator->ImportFinish();
					return BlockedImage;
				}

				// Todo else Grab error message from translator
			}
		}

		Translator->ReleaseSource();
		// Log error?
	}
	return {};
}


