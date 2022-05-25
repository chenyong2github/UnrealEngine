// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSourceCustomization.h"

#include "IMediaModule.h"
#include "ImgMediaEditorModule.h"
#include "ImgMediaSource.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"

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
							.ToolTipText(GetSequencePathProperty(PropertyHandle)->GetToolTipText())
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
					.BrowseButtonImage(FAppStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
					.BrowseButtonStyle(FAppStyle::Get(), "HoverHintOnly")
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

/* FImgMediaSourceCustomization implementation
 *****************************************************************************/

FString FImgMediaSourceCustomization::GetSequencePathFromChildProperty(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
{
	FString FilePath;
	TSharedPtr<IPropertyHandle> SequencePathProperty = GetSequencePathPathProperty(InPropertyHandle);
	if (SequencePathProperty.IsValid())
	{
		if (SequencePathProperty->GetValue(FilePath) != FPropertyAccess::Success)
		{
			UE_LOG(LogImgMediaEditor, Error, TEXT("FImgMediaSourceCustomization could not get SequencePath."));
		}
	}

	return FilePath;
}

FString FImgMediaSourceCustomization::GetSequencePath() const
{
	FString FilePath = GetSequencePathFromChildProperty(PropertyHandle);

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


TSharedPtr<IPropertyHandle> FImgMediaSourceCustomization::GetSequencePathProperty(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
{
	TSharedPtr<IPropertyHandle> SequencePathProperty;

	if ((InPropertyHandle.IsValid()) && (InPropertyHandle->IsValidHandle()))
	{
		TSharedPtr<IPropertyHandle> ParentHandle = InPropertyHandle->GetParentHandle();
		if (ParentHandle.IsValid())
		{
			SequencePathProperty = ParentHandle->GetChildHandle("SequencePath");
		}
	}

	return SequencePathProperty;
}

TSharedPtr<IPropertyHandle> FImgMediaSourceCustomization::GetSequencePathPathProperty(const TSharedPtr<IPropertyHandle>& InPropertyHandle)
{
	TSharedPtr<IPropertyHandle> SequencePathPathProperty;

	TSharedPtr<IPropertyHandle> SequencePathProperty = GetSequencePathProperty(InPropertyHandle);
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
	TSharedPtr<IPropertyHandle> SequencePathPathProperty = GetSequencePathPathProperty(PropertyHandle);
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
