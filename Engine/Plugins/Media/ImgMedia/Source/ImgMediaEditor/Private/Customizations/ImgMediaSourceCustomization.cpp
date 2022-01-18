// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSourceCustomization.h"

#include "IMediaModule.h"
#include "ImgMediaEditorModule.h"
#include "ImgMediaMipMapInfo.h"
#include "ImgMediaSource.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "EditorStyleSet.h"

#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SDirectoryPicker.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SFilePathPicker.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"


#define LOCTEXT_NAMESPACE "FImgMediaSourceCustomization"


/* IDetailCustomization interface
 *****************************************************************************/

void FImgMediaSourceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	// customize 'File' category
	HeaderRow
		.NameContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.Text(LOCTEXT("SequencePathPropertyName", "Sequence Path"))
							.ToolTipText(GetSequencePathProperty()->GetToolTipText())
					]

				+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SImage)
							.Image(FCoreStyle::Get().GetBrush("Icons.Warning"))
							.ToolTipText(LOCTEXT("SequencePathWarning", "The selected image sequence will not get packaged, because its path points to a directory outside the project's /Content/Movies/ directory."))
							.Visibility(this, &FImgMediaSourceCustomization::HandleSequencePathWarningIconVisibility)
					]
			]
		.ValueContent()
			.MaxDesiredWidth(0.0f)
			.MinDesiredWidth(125.0f)
			[
				SNew(SFilePathPicker)
					.BrowseButtonImage(FEditorStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
					.BrowseButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.BrowseButtonToolTip(LOCTEXT("SequencePathBrowseButtonToolTip", "Choose a file from this computer"))
					.BrowseDirectory_Lambda([this]() -> FString
					{
						const FString SequencePath = GetSequencePath();
						return !SequencePath.IsEmpty() ? SequencePath : (FPaths::ProjectContentDir() / TEXT("Movies"));
					})
					.FilePath_Lambda([this]() -> FString
					{
						return GetSequencePath();
					})
					.FileTypeFilter_Lambda([]() -> FString
					{
						return TEXT("All files (*.*)|*.*|EXR files (*.exr)|*.exr");
					})
					.OnPathPicked(this, &FImgMediaSourceCustomization::HandleSequencePathPickerPathPicked)
					.ToolTipText(LOCTEXT("SequencePathToolTip", "The path to an image sequence file on this computer"))
			];
}

void FImgMediaSourceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

void FImgMediaSourceCustomization::CustomizeMipMapInfo(IDetailLayoutBuilder& DetailBuilder)
{
	// Mipmap inspection only works for one object.
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	if (CustomizedObjects.Num() == 1)
	{
		UImgMediaSource* ImgMediaSource = Cast<UImgMediaSource>(CustomizedObjects[0].Get());
		if (ImgMediaSource != nullptr)
		{
			const FImgMediaMipMapInfo* MipMapInfo = ImgMediaSource->GetMipMapInfo();
			if (MipMapInfo != nullptr)
			{
				IDetailCategoryBuilder& MipMapInfoCategory = DetailBuilder.EditCategory("MipMapInfo", LOCTEXT("MipMapInfoCategoryName", "MipMapInfo"));

				// Add objects.
				AddMipMapObjects(MipMapInfoCategory, MipMapInfo);

				// Loop over all cameras.
				const TArray<FImgMediaMipMapCameraInfo>& CameraInfos = MipMapInfo->GetCameraInfo();
				const TArray<float>& MipLevelDistances = MipMapInfo->GetMipLevelDistances();
				float ViewportDistAdjust = MipMapInfo->GetViewportDistAdjust();
				const TArray<FImgMediaMipMapObjectInfo*>& Objects = MipMapInfo->GetObjects();
				if (CameraInfos.Num() > 0)
				{
					for (const FImgMediaMipMapCameraInfo& CameraInfo : CameraInfos)
					{
						// Add group for this camera.
						IDetailGroup& Group = MipMapInfoCategory.AddGroup(*CameraInfo.Name, FText::FromString(CameraInfo.Name));

						// Get mip level distances adjusted so they are in real space.
						TArray<float> AdjustedMipLevelDistances;
						for (float Dist : MipLevelDistances)
						{
							if (CameraInfo.ScreenSize == 0.0f)
							{
								Dist /= ViewportDistAdjust;
							}
							Dist /= CameraInfo.DistAdjust;
							AdjustedMipLevelDistances.Add(Dist);
						}

						// Add info to UI.
						AddCameraObjects(Group, CameraInfo, AdjustedMipLevelDistances, Objects);
						AddCameraMipDistances(Group, CameraInfo, AdjustedMipLevelDistances);
					}
				}
				else
				{
					FDetailWidgetRow& NoCamerasRow = MipMapInfoCategory.AddCustomRow(FText::GetEmpty());
					NoCamerasRow.NameContent()
						[
							SNew(STextBlock)
								.Text(LOCTEXT("MipMapNoCameras", "No Cameras"))
						];
				}
			}
		}
	}
}

void FImgMediaSourceCustomization::AddMipMapObjects(IDetailCategoryBuilder& InCategory, const FImgMediaMipMapInfo* MipMapInfo)
{
	IDetailGroup& MainObjGroup = InCategory.AddGroup("MainObjects", LOCTEXT("MipMapObjects", "Objects"));

	// Loop over all objects.
	const TArray<FImgMediaMipMapObjectInfo*>& Objects = MipMapInfo->GetObjects();
	for (const FImgMediaMipMapObjectInfo* ObjectInfo : Objects)
	{
		AActor* Object = ObjectInfo->Object.Get();
		if (Object != nullptr)
		{
			IDetailGroup& ObjGroup = MainObjGroup.AddGroup(*Object->GetName(), FText::FromString(Object->GetName()));

			// Show auto width.
			FDetailWidgetRow& AutoWidthRow = ObjGroup.AddWidgetRow();
			AutoWidthRow.NameContent()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("MipMapAutoWidth", "AutoWidth"))
						.ToolTipText(LOCTEXT("MipMapAutoWidthToolTip", "This will be used for the width of the object if not set manually."))
				];
			AutoWidthRow.ValueContent()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%f"), FImgMediaMipMapInfo::GetObjectWidth(Object))))
						.ToolTipText(LOCTEXT("MipMapAutoWidthToolTip", "This will be used for the width of the object if not set manually."))
				];

			// Show width.
			FDetailWidgetRow& WidthRow = ObjGroup.AddWidgetRow();
			WidthRow.NameContent()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("MipMapWidth", "Width"))
						.ToolTipText(LOCTEXT("MipMapWidthToolTip", "Width of this object that is currently used for mipmap calculations."))
				];
			WidthRow.ValueContent()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%f"), ObjectInfo->Width)))
						.ToolTipText(LOCTEXT("MipMapWidthToolTip", "Width of this object that is currently used for mipmap calculations."))
				];
		}
	}
}


void FImgMediaSourceCustomization::AddCameraObjects(IDetailGroup& InCameraGroup, const FImgMediaMipMapCameraInfo& InCameraInfo, const TArray<float>& InMipLevelDistances, const TArray<FImgMediaMipMapObjectInfo*>& InObjects)
{
	IDetailGroup& Group = InCameraGroup.AddGroup("Objects", LOCTEXT("MipMapObjects", "Objects"));

	// Loop over all objects.
	for (const FImgMediaMipMapObjectInfo* ObjectInfo : InObjects)
	{
		AActor* Object = ObjectInfo->Object.Get();
		if (Object != nullptr)
		{
			IDetailGroup& ObjGroup = Group.AddGroup(*Object->GetName(), FText::FromString(Object->GetName()));

			// Show distance to camera.
			float DistToCamera = FImgMediaMipMapInfo::GetObjectDistToCamera(InCameraInfo.Location, Object->GetActorLocation());
			FDetailWidgetRow& DistToCameraRow = ObjGroup.AddWidgetRow();
			DistToCameraRow.NameContent()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("MipMapObjDistToCamera", "Distance To Camera"))
				];
			DistToCameraRow.ValueContent()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%f"), DistToCamera)))
				];

			// Show adjusted distance to camera.
			DistToCamera -= ObjectInfo->Width;
			DistToCamera *= ObjectInfo->DistAdjust; 
			FDetailWidgetRow& AdjustedDistToCameraRow = ObjGroup.AddWidgetRow();
			AdjustedDistToCameraRow.NameContent()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("MipMapObjAdjustedDistToCamera", "Adjusted Distance To Camera"))
				];
			AdjustedDistToCameraRow.ValueContent()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%f"), DistToCamera)))
				];

			// Show mip level.
			int MipLevel = FImgMediaMipMapInfo::GetMipLevelForDistance(DistToCamera, InMipLevelDistances);
			FDetailWidgetRow& MipLevelRow = ObjGroup.AddWidgetRow();
			MipLevelRow.NameContent()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("MipMapObjMipLevel", "Mip Level"))
				];
			MipLevelRow.ValueContent()
				[
					SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%d"), MipLevel)))
				];
		}
	}
}

void FImgMediaSourceCustomization::AddCameraMipDistances(IDetailGroup& InCameraGroup, const FImgMediaMipMapCameraInfo& InCameraInfo, const TArray<float>& InMipLevelDistances)
{
	IDetailGroup& Group = InCameraGroup.AddGroup("MipLevelDistances", LOCTEXT("MipMapLevelDistances", "MipLevelDistances"));

	for (int Index = 0; Index < InMipLevelDistances.Num(); ++Index)
	{
		float Distance = InMipLevelDistances[Index];
		FDetailWidgetRow& DistancesRow = Group.AddWidgetRow();
		DistancesRow.NameContent()
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%d"), Index)))
			];
		DistancesRow.ValueContent()
			[
				SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%f"), Distance)))
			];
	}
}

/* FImgMediaSourceCustomization implementation
 *****************************************************************************/

FString FImgMediaSourceCustomization::GetSequencePath() const
{
	FString FilePath;
	TSharedPtr<IPropertyHandle> SequencePathProperty = GetSequencePathPathProperty();
	if (SequencePathProperty.IsValid())
	{
		if (SequencePathProperty->GetValue(FilePath) != FPropertyAccess::Success)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("FImgMediaSourceCustomization could not get SequencePath."));
		}
	}

	return FilePath;
}

FString FImgMediaSourceCustomization::GetRelativePathRoot() const
{
	FString RelativeDir;
	bool bIsPathRelativeToRoot = IsPathRelativeToRoot();

	if (bIsPathRelativeToRoot)
	{
		RelativeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	}
	else
	{
		RelativeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	}
	return RelativeDir;
}


TSharedPtr<IPropertyHandle> FImgMediaSourceCustomization::GetSequencePathProperty() const
{
	TSharedPtr<IPropertyHandle> SequencePathProperty;

	if ((PropertyHandle.IsValid()) && (PropertyHandle->IsValidHandle()))
	{
		TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle();
		if (ParentHandle.IsValid())
		{
			SequencePathProperty = ParentHandle->GetChildHandle("SequencePath");
		}
	}

	return SequencePathProperty;
}

TSharedPtr<IPropertyHandle> FImgMediaSourceCustomization::GetSequencePathPathProperty() const
{
	TSharedPtr<IPropertyHandle> SequencePathPathProperty;

	TSharedPtr<IPropertyHandle> SequencePathProperty = GetSequencePathProperty();
	if (SequencePathProperty.IsValid())
	{
		SequencePathPathProperty = SequencePathProperty->GetChildHandle("Path");
	}

	return SequencePathPathProperty;
}


TSharedPtr<IPropertyHandle> FImgMediaSourceCustomization::GetPathRelativeToRootProperty() const
{
	TSharedPtr<IPropertyHandle> PathRelativeToRootProperty;

	if ((PropertyHandle.IsValid()) && (PropertyHandle->IsValidHandle()))
	{
		TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle();
		if (ParentHandle.IsValid())
		{
			PathRelativeToRootProperty = ParentHandle->GetChildHandle("IsPathRelativeToProjectRoot");
		}
	}

	return PathRelativeToRootProperty;
}


bool FImgMediaSourceCustomization::IsPathRelativeToRoot() const
{
	TSharedPtr<IPropertyHandle> PathRelativeToRootProperty = GetPathRelativeToRootProperty();

	bool bIsPathRelativeToRoot = false;
	if (PathRelativeToRootProperty.IsValid())
	{
		if (PathRelativeToRootProperty->GetValue(bIsPathRelativeToRoot) != FPropertyAccess::Success)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("FImgMediaSourceCustomization could not get IsPathRelativeToProjectRoot."));
		}
	}

	return bIsPathRelativeToRoot;
}

void FImgMediaSourceCustomization::SetPathRelativeToRoot(bool bIsPathRelativeToRoot)
{
	TSharedPtr<IPropertyHandle> PathRelativeToRootProperty = GetPathRelativeToRootProperty();

	if (PathRelativeToRootProperty.IsValid())
	{
		if (PathRelativeToRootProperty->SetValue(bIsPathRelativeToRoot) != FPropertyAccess::Success)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("FImgMediaSourceCustomization could not set IsPathRelativeToProjectRoot."));
		}
	}
}

/* FImgMediaSourceCustomization callbacks
 *****************************************************************************/

void FImgMediaSourceCustomization::HandleSequencePathPickerPathPicked(const FString& PickedPath)
{
	// fully expand path & strip optional file name
	const FString OldRelativeDir = GetRelativePathRoot();
	const FString FullPath = PickedPath.StartsWith(TEXT("./"))
		? FPaths::ConvertRelativePathToFull(OldRelativeDir, PickedPath.RightChop(2))
		: PickedPath;

	const FString FullDir = FPaths::ConvertRelativePathToFull(FPaths::FileExists(FullPath) ? FPaths::GetPath(FullPath) : FullPath);
	

	FString PickedDir = FullDir;
	const FString RelativeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	bool bIsRelativePath = PickedDir.StartsWith(RelativeDir);
	if (bIsRelativePath)
	{
		FPaths::MakePathRelativeTo(PickedDir, *RelativeDir);
		PickedDir = FString(TEXT("./")) + PickedDir;
	}
	
	// Update relative to root property.
	SetPathRelativeToRoot(bIsRelativePath);

	// update property
	TSharedPtr<IPropertyHandle> SequencePathPathProperty = GetSequencePathPathProperty();
	if (SequencePathPathProperty.IsValid())
	{
		if (SequencePathPathProperty->SetValue(PickedDir) != FPropertyAccess::Success)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("FImgMediaSourceCustomization could not set SequencePath."));
		}
	}
}


EVisibility FImgMediaSourceCustomization::HandleSequencePathWarningIconVisibility() const
{
	FString FilePath = GetSequencePath();

	if (FilePath.IsEmpty() || FilePath.Contains(TEXT("://")))
	{
		return EVisibility::Hidden;
	}

	const FString RelativeDir = GetRelativePathRoot();
	const FString FullMoviesPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Movies"));
	const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(FilePath) ? RelativeDir / FilePath : FilePath);

	if (FullPath.StartsWith(FullMoviesPath))
	{
		if (FPaths::DirectoryExists(FullPath))
		{
			return EVisibility::Hidden;
		}

		// file doesn't exist
		return EVisibility::Visible;
	}

	// file not inside Movies folder
	return EVisibility::Visible;
}


#undef LOCTEXT_NAMESPACE
