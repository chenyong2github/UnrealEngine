// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant.h"

#include "PropertyValue.h"
#include "VariantManagerContentLog.h"
#include "VariantManagerObjectVersion.h"
#include "VariantObjectBinding.h"
#include "VariantSet.h"

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "ImageUtils.h"

#if WITH_EDITORONLY_DATA
#include "ObjectTools.h"
#endif

#define LOCTEXT_NAMESPACE "Variant"

UVariant::UVariant(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayText = FText::FromString(TEXT("Variant"));
}

UVariantSet* UVariant::GetParent()
{
	return Cast<UVariantSet>(GetOuter());
}

void UVariant::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FVariantManagerObjectVersion::GUID);
	int32 CustomVersion = Ar.CustomVer(FVariantManagerObjectVersion::GUID);

	if (CustomVersion < FVariantManagerObjectVersion::CategoryFlagsAndManualDisplayText)
	{
		// Recover name from back when it was an UPROPERTY
		if (Ar.IsLoading())
		{
			if (!DisplayText_DEPRECATED.IsEmpty())
			{
				DisplayText = DisplayText_DEPRECATED;
				DisplayText_DEPRECATED = FText();
			}
		}
	}
	else
	{
		Ar << DisplayText;
	}
}

void UVariant::SetDisplayText(const FText& NewDisplayText)
{
	Modify();

	DisplayText = NewDisplayText;
}

FText UVariant::GetDisplayText() const
{
	return DisplayText;
}

void UVariant::AddBindings(const TArray<UVariantObjectBinding*>& NewBindings, int32 Index)
{
	Modify();

	if (Index == INDEX_NONE)
	{
		Index = ObjectBindings.Num();
	}

	// Inserting first ensures we preserve the target order
	ObjectBindings.Insert(NewBindings, Index);

	bool bIsMoveOperation = false;
	TSet<UVariant*> ParentsModified;
	for (UVariantObjectBinding* NewBinding : NewBindings)
	{
		if (NewBinding == nullptr)
		{
			continue;
		}

		UVariant* OldParent = NewBinding->GetParent();

		if (OldParent)
		{
			if (OldParent != this)
			{
				if (!ParentsModified.Contains(OldParent))
				{
					OldParent->Modify();
					ParentsModified.Add(OldParent);
				}
				OldParent->ObjectBindings.RemoveSingle(NewBinding);
			}
			else
			{
				bIsMoveOperation = true;
			}
		}

		NewBinding->Modify();
		NewBinding->Rename(nullptr, this, REN_DontCreateRedirectors);
	}

	// If it's a move operation, we'll have to manually clear the old pointers from the array
	if (!bIsMoveOperation)
	{
		return;
	}

	TSet<FString> NewBindingPaths = TSet<FString>();
	for (UVariantObjectBinding* NewBinding : NewBindings)
	{
		NewBindingPaths.Add(NewBinding->GetObjectPath());
	}

	// Sweep back from insertion point nulling old bindings with the same path
	for (int32 SweepIndex = Index-1; SweepIndex >= 0; SweepIndex--)
	{
		if (NewBindingPaths.Contains(ObjectBindings[SweepIndex]->GetObjectPath()))
		{
			ObjectBindings[SweepIndex] = nullptr;
		}
	}
	// Sweep forward from the end of the inserted segment nulling old bindings with the same path
	for (int32 SweepIndex = Index + NewBindings.Num(); SweepIndex < ObjectBindings.Num(); SweepIndex++)
	{
		if (NewBindingPaths.Contains(ObjectBindings[SweepIndex]->GetObjectPath()))
		{
			ObjectBindings[SweepIndex] = nullptr;
		}
	}

	// Finally remove null entries
	for (int32 IterIndex = ObjectBindings.Num() - 1; IterIndex >= 0; IterIndex--)
	{
		if (ObjectBindings[IterIndex] == nullptr)
		{
			ObjectBindings.RemoveAt(IterIndex);
		}
	}
}

int32 UVariant::GetBindingIndex(UVariantObjectBinding* Binding)
{
	if (Binding == nullptr)
	{
		return INDEX_NONE;
	}

	return ObjectBindings.Find(Binding);
}

const TArray<UVariantObjectBinding*>& UVariant::GetBindings() const
{
	return ObjectBindings;
}

void UVariant::RemoveBindings(const TArray<UVariantObjectBinding*>& Bindings)
{
	Modify();

	for (UVariantObjectBinding* Binding : Bindings)
	{
		ObjectBindings.RemoveSingle(Binding);
	}
}

int32 UVariant::GetNumActors()
{
	return ObjectBindings.Num();
}

AActor* UVariant::GetActor(int32 ActorIndex)
{
	if (ObjectBindings.IsValidIndex(ActorIndex))
	{
		UVariantObjectBinding* Binding = ObjectBindings[ActorIndex];
		UObject* Obj = Binding->GetObject();
		if (AActor* Actor = Cast<AActor>(Obj))
		{
			return Actor;
		}
	}

	return nullptr;
}

UVariantObjectBinding* UVariant::GetBindingByName(const FString& ActorName)
{
	UVariantObjectBinding** FoundBindingPtr = ObjectBindings.FindByPredicate([&ActorName](const UVariantObjectBinding* Binding)
	{
		UObject* ThisActor = Binding->GetObject();
		return ThisActor && ThisActor->GetName() == ActorName;
	});

	if (FoundBindingPtr)
	{
		return *FoundBindingPtr;
	}

	return nullptr;
}

void UVariant::SwitchOn()
{
	for (UVariantObjectBinding* Binding : ObjectBindings)
	{
		for (UPropertyValue* PropCapture : Binding->GetCapturedProperties())
		{
			PropCapture->ApplyDataToResolvedObject();
		}

		Binding->ExecuteAllTargetFunctions();
	}
}

bool UVariant::IsActive()
{
	if (ObjectBindings.Num() == 0)
	{
		return false;
	}

	for (UVariantObjectBinding* Binding : ObjectBindings)
	{
		for (UPropertyValue* PropCapture : Binding->GetCapturedProperties())
		{
			if (!PropCapture->IsRecordedDataCurrent())
			{
				return false;
			}
		}
	}

	return true;
}

void UVariant::SetThumbnail(UTexture2D* NewThumbnail)
{
	if (NewThumbnail == Thumbnail)
	{
		return;
	}

	if (NewThumbnail != nullptr)
	{
		int32 OriginalWidth = NewThumbnail->PlatformData->SizeX;
		int32 OriginalHeight = NewThumbnail->PlatformData->SizeY;
		int32 TargetWidth = FMath::Min(OriginalWidth, VARIANT_THUMBNAIL_SIZE);
		int32 TargetHeight = FMath::Min(OriginalHeight, VARIANT_THUMBNAIL_SIZE);

		// We need to guarantee this UTexture2D is serialized with us but that we don't take ownership of it,
		// and also that it is shown without compression, so we duplicate it here
		if (TargetWidth != OriginalWidth || TargetHeight != OriginalHeight || NewThumbnail->GetOuter() != this)
		{
			const FColor* OriginalBytes = reinterpret_cast<const FColor*>(NewThumbnail->PlatformData->Mips[0].BulkData.LockReadOnly());
			TArrayView<const FColor> OriginalColors(OriginalBytes, OriginalWidth * OriginalHeight);

			TArray<FColor> TargetColors;
			TargetColors.SetNumUninitialized(TargetWidth * TargetHeight);

			if (TargetWidth != OriginalWidth || TargetHeight != OriginalHeight)
			{
				FImageUtils::ImageResize(OriginalWidth, OriginalHeight, OriginalColors, TargetWidth, TargetHeight, TArrayView<FColor>(TargetColors), false);
			}
			else
			{
				TargetColors = TArray<FColor>(OriginalColors.GetData(), OriginalColors.GetTypeSize() * OriginalColors.Num());
			}

			NewThumbnail->PlatformData->Mips[0].BulkData.Unlock();

			FCreateTexture2DParameters Params;
			Params.bDeferCompression = true;
			Params.CompressionSettings = TC_EditorIcon;

			UTexture2D* ResizedThumbnail = FImageUtils::CreateTexture2D(TargetWidth, TargetHeight, TargetColors, this, FString(), RF_NoFlags, Params);
			if (ResizedThumbnail)
			{
				NewThumbnail = ResizedThumbnail;
			}
			else
			{
				UE_LOG(LogVariantContent, Warning, TEXT("Failed to resize texture '%s' as a thumbnail for variant '%s'"), *NewThumbnail->GetName(), *GetDisplayText().ToString());
				return;
			}
		}
	}

	Modify();

	Thumbnail = NewThumbnail;
}

UTexture2D* UVariant::GetThumbnail()
{
#if WITH_EDITORONLY_DATA
	if (Thumbnail == nullptr)
	{
		// Try to convert old thumbnails to a new thumbnail
		FName VariantName = *GetFullName();
		FThumbnailMap Map;
		ThumbnailTools::ConditionallyLoadThumbnailsForObjects({ VariantName }, Map);

		FObjectThumbnail* OldThumbnail = Map.Find(VariantName);
		if (OldThumbnail && !OldThumbnail->IsEmpty())
		{
			const TArray<uint8>& OldBytes = OldThumbnail->GetUncompressedImageData();

			TArray<FColor> OldColors;
			OldColors.SetNumUninitialized(OldBytes.Num() / sizeof(FColor));
			FMemory::Memcpy(OldColors.GetData(), OldBytes.GetData(), OldBytes.Num());

			// Resize if needed
			int32 Width = OldThumbnail->GetImageWidth();
			int32 Height = OldThumbnail->GetImageHeight();
			if (Width != VARIANT_THUMBNAIL_SIZE || Height != VARIANT_THUMBNAIL_SIZE)
			{
				TArray<FColor> ResizedColors;
				ResizedColors.SetNum(VARIANT_THUMBNAIL_SIZE * VARIANT_THUMBNAIL_SIZE);

				FImageUtils::ImageResize(Width, Height, OldColors, VARIANT_THUMBNAIL_SIZE, VARIANT_THUMBNAIL_SIZE, ResizedColors, false);

				OldColors = MoveTemp(ResizedColors);
			}

			FCreateTexture2DParameters Params;
			Params.bDeferCompression = true;

			Thumbnail = FImageUtils::CreateTexture2D(VARIANT_THUMBNAIL_SIZE, VARIANT_THUMBNAIL_SIZE, OldColors, this, FString(), RF_NoFlags, Params);

			UPackage* Package = GetOutermost();
			if (Package)
			{
				// After this our thumbnail will be empty, and we won't get in here ever again for this variant
				ThumbnailTools::CacheEmptyThumbnail(GetFullName(), GetOutermost());

				// Updated the thumbnail in the package, so we need to notify that it changed
				Package->MarkPackageDirty();
			}
		}
	}
#endif

	return Thumbnail;
}

#undef LOCTEXT_NAMESPACE