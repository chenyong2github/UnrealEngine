// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeUDIMTranslator.h"

#include "InterchangeManager.h"
#include "InterchangeTextureNode.h"
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
	, UdimRegexPattern( TEXT(R"((.+?)[._](\d{4})$)") )
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

bool UInterchangeUDIMTranslator::Translate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	FString Filename = SourceData->GetFilename();
	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	UInterchangeTextureNode* TextureNode = NewObject<UInterchangeTextureNode>(&BaseNodeContainer, NAME_None);

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

	if (UInterchangeTranslatorBase* Translator = InterchangeManager.GetTranslatorSupportingPayloadInterfaceForSourceData(SourceData, UInterchangeTexturePayloadInterface::StaticClass()))
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
	const FString CurrentFilename = OriginalFile->GetFilename();

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

			UE::Interchange::FImportBlockedImage BlockedImage;
			BlockedImage.InitZeroed(UDIMsAndSourcesFileArray.Num());

			TArray<int32> InvalidTextureIndex;

			ParallelFor(UDIMsAndSourcesFileArray.Num(), [&TranslatorsAndSourceData, &UDIMsAndSourcesFileArray, &BlockedImage, &InvalidTextureIndex](int32 Index)
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
						Payload = TextureTranslator->GetTexturePayloadData(SourceDataForBlock, SourceDataForBlock->GetFilename());
					}

					if (Payload.IsSet())
					{
						UE::Interchange::FImportImage& Image = Payload.GetValue();
						// Consume the image
						BlockedImage.ImagesData[Index] = MoveTemp(Image);

						FTextureSourceBlock& Block =  BlockedImage.BlocksData[Index];
						Block.BlockX = (CurrentUDIM - 1001) % 10;
						Block.BlockY = (CurrentUDIM - 1001) / 10;
						Block.SizeX = Image.SizeX;
						Block.SizeY = Image.SizeY;
						Block.NumSlices = 1;
						Block.NumMips = Image.NumMips;
					}
					else
					{
						// Todo Interchange Log error
						InvalidTextureIndex.Add(Index);
					}

					// Let the translator release it data.
					Translator->ImportFinish();
				}
				, EParallelForFlags::Unbalanced | EParallelForFlags::BackgroundPriority);

			for (int32 Index : InvalidTextureIndex)
			{
				BlockedImage.RemoveBlock(Index);
			}

			if (BlockedImage.HasData())
			{
				return BlockedImage;
			}
		}

		// Import as a non blocked image
		if (IInterchangeTexturePayloadInterface* TextureTranslator = CastChecked<IInterchangeTexturePayloadInterface>(Translator))
		{
			TOptional<UE::Interchange::FImportImage> Payload = TextureTranslator->GetTexturePayloadData(OriginalFile, OriginalFile->GetFilename());
			if (Payload.IsSet())
			{
				UE::Interchange::FImportBlockedImage BlockedImage;
				// Consume the image
				BlockedImage.ImagesData.Add(MoveTemp(Payload.GetValue()));
				Translator->ImportFinish();
				return BlockedImage;
			}
		}

		Translator->ReleaseSource();
		// Log error?
	}
	return {};
}


