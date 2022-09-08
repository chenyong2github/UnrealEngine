// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GenerateModerationArtifactsCommandlet.cpp: Commandlet provides basic package iteration functionaliy for derived commandlets
=============================================================================*/
#include "Commandlets/GenerateModerationArtifactsCommandlet.h"
#include "CoreMinimal.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/Texture.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"

// localization includes 
#include "Serialization/PropertyLocalizationDataGathering.h"
#include "LocTextHelper.h"
// string proxy includes
//#include "Serialization/NameAsStringProxyArchive.h"
// saving json manifest file
#include "JsonObjectConverter.h"

PRAGMA_DISABLE_OPTIMIZATION

/**-----------------------------------------------------------------------------
 *	UGenerateModerationArtifactsCommandlet commandlet.
 *
 * This commandlet exposes some functionality for iterating packages 
 *
 *
----------------------------------------------------------------------------**/

DEFINE_LOG_CATEGORY(LogModerationArtifactsCommandlet);



UGenerateModerationArtifactsCommandlet::UGenerateModerationArtifactsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UGenerateModerationArtifactsCommandlet::InitializeParameters( const TArray<FString>& Tokens, TArray<FString>& PackageNames )
{
	

	for (int32 SwitchIdx = 0; SwitchIdx < Switches.Num(); SwitchIdx++)
	{
		const FString& CurrentSwitch = Switches[SwitchIdx];
		if (FParse::Value(*CurrentSwitch, TEXT("OutputDir="), OutputPath))
		{
			continue;
		}
	}

	return Super::InitializeParameters(Tokens, PackageNames);
}

void UGenerateModerationArtifactsCommandlet::PerformAdditionalOperations(class UPackage* Package, bool& bSavePackage)
{

	// need to process localization, fstrings in structs, probably need to do something special with datatables... 

	GatherLocalizationFromPackage(Package);
}

class FDefaultObjectPropertyIterator
{
public:

	virtual ~FDefaultObjectPropertyIterator() {}

	void IterateObject(UObject* Object, bool bRecurse)
	{
		IterateUObjectProperty(Object->GetClass(), Object, Object->GetClass()->GetDefaultObject());
	}

	virtual bool ProcessProperty(FProperty* Property, void* Ptr, void* DefaultPtr) = 0;


private:
	TSet<void*> ProcessedObjectsSet;

	void IterateProperty(FProperty* Property, void* ObjectPtr, void* DefaultObjectPtr)
	{
		if (ProcessProperty(Property, ObjectPtr, DefaultObjectPtr) == false)
		{
			// stop recursion into this property
			return;
		}
		if (Property->IsA(FStructProperty::StaticClass()))
		{
			IterateStructProperty((FStructProperty*)Property, ObjectPtr, DefaultObjectPtr);
		}
		else if (Property->IsA(FObjectPropertyBase::StaticClass()))
		{
			FObjectPropertyBase* ObjectProperty = (FObjectPropertyBase*)Property;
			void* NewObject = ObjectProperty->GetObjectPropertyValue(ObjectPtr);
			void* NewDefaultObject = DefaultObjectPtr ? ObjectProperty->GetObjectPropertyValue(DefaultObjectPtr) : nullptr;
			IterateUObjectProperty(ObjectProperty->PropertyClass, NewObject, NewDefaultObject);
		}
		else if (Property->IsA(FArrayProperty::StaticClass()))
		{
			FArrayProperty* ArrayProperty = (FArrayProperty*)Property;
			IterateArrayProperty(ArrayProperty, ObjectPtr, DefaultObjectPtr);
		}
		else if (Property->IsA(FMapProperty::StaticClass()))
		{
			FMapProperty* MapProperty = (FMapProperty*)Property;
			IterateMapProperty(MapProperty, ObjectPtr, DefaultObjectPtr);
		}
		else if (Property->IsA(FSetProperty::StaticClass()))
		{
			FSetProperty* SetProperty = (FSetProperty*)Property;
			IterateSetProperty(SetProperty, ObjectPtr, DefaultObjectPtr);
		}
	}

	void IterateSetProperty(FSetProperty* SetProperty, void* ObjectPtr, void* DefaultObjectPtr)
	{
		FScriptSetHelper SetHelper(SetProperty, ObjectPtr);
		FScriptSetHelper DefaultSetHelper(SetProperty, DefaultObjectPtr);
		bool bUseDefault = DefaultObjectPtr ? SetHelper.Num() == DefaultSetHelper.Num() : false;
		for (int32 Index = 0; Index < SetHelper.Num(); ++Index)
		{
			void* KeyPtr = SetHelper.GetElementPtr(Index);
			void* DefaultKeyPtr = bUseDefault ? DefaultSetHelper.GetElementPtr(Index) : nullptr;
			IterateProperty(SetHelper.GetElementProperty(), KeyPtr, DefaultKeyPtr);
		}
	}

	void IterateMapProperty(FMapProperty* MapProperty, void* ObjectPtr, void* DefaultObjectPtr)
	{
		FScriptMapHelper MapHelper(MapProperty, ObjectPtr);
		FScriptMapHelper DefaultMapHelper(MapProperty, DefaultObjectPtr);
		bool bUseDefault = DefaultObjectPtr ? DefaultMapHelper.Num() == MapHelper.Num() : false;
		for (int32 Index = 0; Index < MapHelper.Num(); ++Index)
		{
			void* KeyPtr = MapHelper.GetKeyPtr(Index);
			void* DefaultKeyPtr = bUseDefault ? DefaultMapHelper.GetKeyPtr(Index) : nullptr;
			IterateProperty(MapHelper.GetKeyProperty(), KeyPtr, DefaultKeyPtr);
			void* ValuePtr = MapHelper.GetValuePtr(Index);
			void* DefaultValuePtr = bUseDefault ? DefaultMapHelper.GetValuePtr(Index) : nullptr;
			IterateProperty(MapHelper.GetValueProperty(), ValuePtr, DefaultValuePtr);
		}
	}

	void IterateArrayProperty(FArrayProperty* ArrayProperty, void* ObjectPtr, void* DefaultObjectPtr)
	{
		FScriptArrayHelper ArrayHelper(ArrayProperty, ObjectPtr);
		FScriptArrayHelper DefaultArrayHelper(ArrayProperty, DefaultObjectPtr);
		bool bUseDefault = DefaultObjectPtr ? DefaultArrayHelper.Num() == ArrayHelper.Num() : false;
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			void* ArrayValuePtr = ArrayHelper.GetRawPtr(Index);
			void* DefaultValuePtr = bUseDefault ? DefaultArrayHelper.GetRawPtr(Index) : nullptr;
			IterateProperty(ArrayProperty->Inner, ArrayValuePtr, DefaultValuePtr);
		}
	}

	void IterateStructProperty(const FStructProperty* StructProperty, void* ObjectPtr, void* DefaultObjectPtr)
	{
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
			{
				void* PropertyPtr = Property->ContainerPtrToValuePtr<void>(ObjectPtr, Index);
				void* DefaultPropertyPtr = nullptr;
				if(DefaultObjectPtr)
				{
					DefaultPropertyPtr = Property->ContainerPtrToValuePtr<void>(DefaultObjectPtr, Index);
				}
				IterateProperty(Property, PropertyPtr, DefaultPropertyPtr);
			}
		}
	}

	void IterateUObjectProperty(const UClass* Class, void* ObjectPtr, void* DefaultObjectPtr)
	{
		UObject* Object = (UObject*)ObjectPtr;
		if (Object == nullptr)
		{
			return;
		}
		UObject* DefaultObject = (UObject*)DefaultObjectPtr;
		// make sure they are the same type
		bool bUseDefaultObject = true;
		if (DefaultObject == nullptr || Object->GetClass() != DefaultObject->GetClass())
		{
			bUseDefaultObject = false;
		}
		if (ProcessedObjectsSet.Contains(ObjectPtr))
		{
			// ignore ciricular references
			return;
		}
		ProcessedObjectsSet.Add(ObjectPtr);

		for (TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			// static const FName NAME_Outer = GET_MEMBER_NAME_CHECKED(UObject, OuterPrivate);
			if (Property->GetFName() == NAME_Outer)
			{
				// don't follow owner property
				continue;
			}
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
			{
				void* PropertyPtr = Property->ContainerPtrToValuePtr<void>(ObjectPtr, Index);
				void* DefaultPropertyPtr = nullptr;
				if (DefaultObjectPtr && bUseDefaultObject)
				{
					DefaultPropertyPtr = Property->ContainerPtrToValuePtr<void>(DefaultObjectPtr, Index);
				}
				IterateProperty(Property, PropertyPtr, DefaultPropertyPtr);
			}
		}
	}
};

class FStringCollector : public FDefaultObjectPropertyIterator
{
public:
	TSet<FString> AllStrings;
	virtual bool ProcessProperty(FProperty* Property, void* Ptr, void* DefaultPtr) override
	{
		if (Property->IsA(FNameProperty::StaticClass()))
		{
			const FName* NameValue = (FName*)Ptr;
			if (*NameValue != NAME_None)
			{
				if (Property->Identical(Ptr, DefaultPtr))
				{
					UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found property name %s value %s is same as default property, ignoring string"), *Property->GetName(), *NameValue->ToString());
				}
				else
				{
					AllStrings.Add(NameValue->ToString());
				}
			}
		}

		if (Property->IsA(FStrProperty::StaticClass()))
		{
			const FString* StringValue = (FString*)Ptr;
			if (StringValue != nullptr && !StringValue->IsEmpty())
			{
				if( Property->Identical(Ptr, DefaultPtr))
				{
					UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found property name %s value %s is same as default property, ignoring string"), *Property->GetName(), **StringValue);
				}
				else
				{
					AllStrings.Add(*StringValue);
				}
			}
		}
		return true;
	}
};


void UGenerateModerationArtifactsCommandlet::GatherFStringsFromObject(class UObject* Object)
{
	/*FGatherStringArchive GatherStringArchive;
	GatherStringArchive << Object;*/

	FStringCollector StringCollector;
	StringCollector.IterateObject(Object, true);

	if (StringCollector.AllStrings.Num() > 0)
	{
		FString FileName = CreateOutputFileName(Object, TEXT("str"));
		TArray<FString> AllStringsArray;
		for (const FString& Str : StringCollector.AllStrings)
		{
			AllStringsArray.Add(Str);
		}
		FFileHelper::SaveStringArrayToFile(AllStringsArray, *FileName);
	}

#if 0


	TArray<FString> AllStrings;
	for (TPropertyValueIterator<FStrProperty> It(Object->GetClass(), Object); It; ++It)
	{
		const FStrProperty* Property = It->Key;
		const FString* StringValue = (FString*)It->Value;
		if (Property->GetClass())
		{
			UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found property %s of type class %s"), *Property->GetName(), *Property->GetClass()->GetName());
		}
		if (StringValue != nullptr && !StringValue->IsEmpty())
		{
			AllStrings.Add(*StringValue);
		}
	}

	for (TPropertyValueIterator<FNameProperty> It(Object->GetClass(), Object); It; ++It)
	{
		const FNameProperty* Property = It->Key;
		const FName* NameValue = (FName*)It->Value;
		if (Property->GetClass())
		{
			UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found property %s of type class %s"), *Property->GetName(), *Property->GetClass()->GetName());
		}

		// Property->Identical()
		// Property->GetClass()->GetDefaultObject()->G

		if (NameValue != nullptr && *NameValue != NAME_None)
		{
			AllStrings.Add(NameValue->ToString());
		}
	}


	if(AllStrings.Num() > 0)
	{
		FString FileName = CreateOutputFileName(Object, TEXT("str"));
		FFileHelper::SaveStringArrayToFile(AllStrings, *FileName);
	}
#endif
}


void UGenerateModerationArtifactsCommandlet::GatherLocalizationFromPackage(class UPackage* Package)
{


	TArray<FGatherableTextData> GatherableTextDataArray;
	EPropertyLocalizationGathererResultFlags GatherableTextResultFlags = EPropertyLocalizationGathererResultFlags::Empty;
	FPropertyLocalizationDataGatherer(GatherableTextDataArray, Package, GatherableTextResultFlags);

	FString OutputFilePath = CreateOutputFileName(Package, TEXT("loc"));


	FString LocalizationTargetName = FPaths::GetBaseFilename(OutputFilePath);
	/* {
		FString ManifestName;
		GetStringFromConfig(TEXT("CommonSettings"), TEXT("ManifestName"), ManifestName, GatherTextConfigPath);
		LocalizationTargetName = FPaths::GetBaseFilename(ManifestName);
	}*/

	// Basic helper that can be used only to gather a new manifest for writing
	TSharedRef<FLocTextHelper> GatherManifestHelper = MakeShared<FLocTextHelper>(LocalizationTargetName, nullptr);
	GatherManifestHelper->LoadManifest(ELocTextHelperLoadFlags::Create);
	bool bContainsText = false;
	for (const FGatherableTextData& GatherableTextData : GatherableTextDataArray)
	{
		for (const FTextSourceSiteContext& TextSourceSiteContext : GatherableTextData.SourceSiteContexts)
		{

			if (TextSourceSiteContext.KeyName.IsEmpty())
			{
				UE_LOG(LogModerationArtifactsCommandlet, Warning, TEXT("Detected missing key on asset \"%s\"."), *TextSourceSiteContext.SiteDescription);
				continue;
			}

			static const FLocMetadataObject DefaultMetadataObject;

			FManifestContext Context;
			Context.Key = TextSourceSiteContext.KeyName;
			Context.KeyMetadataObj = !(FLocMetadataObject::IsMetadataExactMatch(&TextSourceSiteContext.KeyMetaData, &DefaultMetadataObject)) ? MakeShareable(new FLocMetadataObject(TextSourceSiteContext.KeyMetaData)) : nullptr;
			Context.InfoMetadataObj = !(FLocMetadataObject::IsMetadataExactMatch(&TextSourceSiteContext.InfoMetaData, &DefaultMetadataObject)) ? MakeShareable(new FLocMetadataObject(TextSourceSiteContext.InfoMetaData)) : nullptr;
			Context.bIsOptional = TextSourceSiteContext.IsOptional;
			Context.SourceLocation = TextSourceSiteContext.SiteDescription;
			//Context.PlatformName = GetSplitPlatformNameFromPath(TextSourceSiteContext.SiteDescription);

			FLocItem Source(GatherableTextData.SourceData.SourceString);

			GatherManifestHelper->AddSourceText(GatherableTextData.NamespaceName, Source, Context);
			bContainsText = true;
		}
	}
	if (bContainsText)
	{
		FText Error;
		if (GatherManifestHelper->SaveManifest(OutputFilePath, &Error)==false)
		{
			UE_LOG(LogModerationArtifactsCommandlet, Error, TEXT("Failed to save localization manifest for package %s, error: %s"), *Package->GetFullName(), *Error.ToString());
		}
	}
}


void UGenerateModerationArtifactsCommandlet::PerformAdditionalOperations(class UObject* Object, bool& bSavePackage)
{
	bSavePackage = false;

	GatherFStringsFromObject(Object);

	if (Object->GetClass()->IsChildOf(UTexture::StaticClass()))
	{
		GenerateArtifact(StaticCast<UTexture*>(Object));
	}
	else if (Object->GetClass()->IsChildOf(UStaticMeshComponent::StaticClass()))
	{
		GenerateArtifact(StaticCast<UStaticMeshComponent*>(Object));
	}
#if 0 // todo add suppport for datatables 
	else if (Object->GetClass()->IsChildOf(UDataTable::StaticClass()))
	{
		GenerateArtifact(StaticCast<UDataTable*>(Object));
	}
#endif
}

FString UGenerateModerationArtifactsCommandlet::CreateOutputFileName(class UObject* Object, const FString& Extension)
{
	FString FileName = Manifest.CreateModerationAssetFileName(Object, Extension);
	FString FullPath = FPaths::Combine(OutputPath, FileName);
	return FullPath;
}

void UGenerateModerationArtifactsCommandlet::GenerateArtifact(UTexture* Texture)
{
	UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found texture %s"), *Texture->GetFullName());

	if (Texture->Source.IsValid())
	{
		FString OutputFileName = CreateOutputFileName(Texture, TEXT("png"));


		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);



		FImage Image;
		if (ImageWrapper.IsValid() && Texture->Source.GetMipImage(Image, 0))
		{
			ERGBFormat RGBFormat;
			int BitsPerChannel;
			switch (Image.Format)
			{
			case ERawImageFormat::G8:
				RGBFormat = ERGBFormat::Gray;
				BitsPerChannel = 8;
				break;
			case ERawImageFormat::BGRA8:
				RGBFormat = ERGBFormat::BGRA;
				BitsPerChannel = 8;
				break;
			case ERawImageFormat::BGRE8:
				RGBFormat = ERGBFormat::BGRE;
				BitsPerChannel = 8;
				break;
			case ERawImageFormat::RGBA16:
				RGBFormat = ERGBFormat::RGBA;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::RGBA16F:
				RGBFormat = ERGBFormat::RGBAF;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::RGBA32F:
				RGBFormat = ERGBFormat::RGBAF;
				BitsPerChannel = 32;
				break;
			case ERawImageFormat::G16:
				RGBFormat = ERGBFormat::Gray;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::R16F:
				RGBFormat = ERGBFormat::GrayF;
				BitsPerChannel = 16;
				break;
			case ERawImageFormat::R32F:
				RGBFormat = ERGBFormat::GrayF;
				BitsPerChannel = 32;
				break;
			default:
				UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Texture %s source image format %s is unsupported"), *Texture->GetFullName(), ERawImageFormat::GetName(Image.Format));
				return;
			}
			
			if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Image.RawData.GetData(), Image.RawData.Num(), Image.GetWidth(), Image.GetHeight(), RGBFormat, BitsPerChannel))
			{
				EImageCompressionQuality PngQuality = EImageCompressionQuality::Default; // 0 means default 
				TArray64<uint8> CompressedData = ImageWrapper->GetCompressed((int32)PngQuality);
				if (CompressedData.Num() > 0)
				{
					FFileHelper::SaveArrayToFile(CompressedData, *OutputFileName);
				}
			}
		}

	}
	/*

	ImageWrapper->SetRaw(SurfaceData.GetData(), SurfaceData.GetAllocatedSize(), UnprojectedAtlasWidth, UnprojectedAtlasHeight, ERGBFormat::BGRA, 32);
	const TArray64<uint8> PNGDataUnprojected = ImageWrapper->GetCompressed(100);
	FFileHelper::SaveArrayToFile(PNGDataUnprojected, *AtlasNameUnprojected);
	ImageWrapper.Reset();*/
	
}

/*
void UGenerateModerationArtifactsCommandlet::GenerateArtifact(UDataTable* DataTable)
{
	check(0);  // not implemented yet
	// FString UDataTable::GetTableAsString(const EDataTableExportFlags InDTExportFlags) const
}*/
void UGenerateModerationArtifactsCommandlet::GenerateArtifact(UStaticMeshComponent* StaticMesh)
{
	UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Found staticmesh %s"), *StaticMesh->GetFullName());
}

void UGenerateModerationArtifactsCommandlet::PostProcessPackages()
{
	FString JsonManifestFile;
	if (FJsonObjectConverter::UStructToJsonObjectString(Manifest, JsonManifestFile))
	{
		FString ManifestFilename = FPaths::Combine(OutputPath, TEXT("ModerationManifest.manifest"));
		if (FFileHelper::SaveStringToFile(JsonManifestFile, *ManifestFilename) == false)
		{
			UE_LOG(LogModerationArtifactsCommandlet, Error, TEXT("Unable to save manifest file to %s"), *ManifestFilename);
		}
	}
	else
	{
		UE_LOG(LogModerationArtifactsCommandlet,Error, TEXT("Unable to generate json manifest file"));
	}

}


FModerationPackage* FModerationManifest::FindOrCreateModerationPackage(UPackage* InPackage)
{
	
	FModerationPackage* ModerationPackage = Packages.FindByPredicate([InPackage](const FModerationPackage& Other) {
				return (Other.Package == InPackage);
			});

	if (ModerationPackage == nullptr)
	{
		int32 Index = Packages.AddDefaulted();
		ModerationPackage = &Packages[Index];
		ModerationPackage->Package = InPackage;
		const FPackagePath& PackagePath = InPackage->GetLoadedPath();
		ModerationPackage->PackagePath = PackagePath.GetLocalFullPath();
		FMD5Hash FileHash = FMD5Hash::HashFile(*ModerationPackage->PackagePath);
		ModerationPackage->PackageHash = LexToString(FileHash);
	}
	return ModerationPackage;
}

FString FModerationManifest::CreateModerationAssetFileName(const UObject* Object, const FString& Extension)
{
	UPackage* Package = Object->GetOutermost();
	check(Package);

	FModerationPackage* ModerationPackage = FindOrCreateModerationPackage(Package);
	check(ModerationPackage);
	FModerationAsset* ModerationAsset = ModerationPackage->FindOrCreateModerationAsset(Object);
	check(ModerationAsset);
	
	FString Path = ModerationAsset->FullPath;
	Path.RemoveFromStart(TEXT("/"));
	Path.ReplaceInline(TEXT("/"), TEXT("+"));
	Path.ReplaceInline(TEXT(":"), TEXT(""));
	FString FileName = FString::Printf(TEXT("%s-%s-%s.%s"), *ModerationAsset->ClassName, *Path, *ModerationPackage->PackageHash, *Extension);
	ModerationAsset->ModerationArtifactFilenames.Add(FileName);

	UE_LOG(LogModerationArtifactsCommandlet, Display, TEXT("Created moderation file %s for asset %s"), *FileName, *Object->GetPathName());

	return FileName;
}


FModerationAsset* FModerationPackage::FindOrCreateModerationAsset(const UObject* InObject)
{
	FModerationAsset* Asset = Assets.FindByPredicate([InObject](const FModerationAsset& Other){ return Other.Object == InObject; });

	if (Asset == nullptr)
	{
		int32 Index = Assets.AddDefaulted();
		Asset = &Assets[Index];
		Asset->FullPath = InObject->GetPathName();
		Asset->ClassName = InObject->GetClass()->GetName();
	}
	return Asset;
}

PRAGMA_ENABLE_OPTIMIZATION