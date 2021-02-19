// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaSource.h"
#include "ImgMediaMipMapInfo.h"
#include "ImgMediaMipMapInfoManager.h"
#include "ImgMediaPrivate.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"


/* UImgMediaSource structors
 *****************************************************************************/

UImgMediaSource::UImgMediaSource()
	: IsPathRelativeToProjectRoot(false)
	, FrameRateOverride(0, 0)
	, MipMapInfo(MakeShared<FImgMediaMipMapInfo, ESPMode::ThreadSafe>())
{
}


/* UImgMediaSource interface
 *****************************************************************************/

void UImgMediaSource::GetProxies(TArray<FString>& OutProxies) const
{
	IFileManager::Get().FindFiles(OutProxies, *FPaths::Combine(GetFullPath(), TEXT("*")), false, true);
}


void UImgMediaSource::SetSequencePath(const FString& Path)
{
	const FString SanitizedPath = FPaths::GetPath(Path);

	IsPathRelativeToProjectRoot = false;
	if (SanitizedPath.IsEmpty() || SanitizedPath.StartsWith(TEXT(".")))
	{
		SequencePath.Path = SanitizedPath;
	}
	else
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(SanitizedPath);
		const FString FullGameContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

		if (FullPath.StartsWith(FullGameContentDir))
		{
			FPaths::MakePathRelativeTo(FullPath, *FullGameContentDir);
			FullPath = FString(TEXT("./")) + FullPath;
		}

		SequencePath.Path = FullPath;
	}
}


void UImgMediaSource::AddGlobalCamera(AActor* InActor)
{
	FImgMediaMipMapInfoManager::Get().AddCamera(InActor);
}


void UImgMediaSource::RemoveGlobalCamera(AActor* InActor)
{
	FImgMediaMipMapInfoManager::Get().RemoveCamera(InActor);
}


void UImgMediaSource::AddTargetObject(AActor* InActor, float Width)
{
	MipMapInfo->AddObject(InActor, Width, 0.0f);
}


void UImgMediaSource::RemoveTargetObject(AActor* InActor)
{
	MipMapInfo->RemoveObject(InActor);
}


void UImgMediaSource::SetMipLevelDistance(float Distance)
{
	MipMapInfo->SetMipLevelDistance(Distance);
}


/* IMediaOptions interface
 *****************************************************************************/

int64 UImgMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == ImgMedia::FrameRateOverrideDenonimatorOption)
	{
		return FrameRateOverride.Denominator;
	}

	if (Key == ImgMedia::FrameRateOverrideNumeratorOption)
	{
		return FrameRateOverride.Numerator;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}


FString UImgMediaSource::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	if (Key == ImgMedia::ProxyOverrideOption)
	{
		return ProxyOverride;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}

TSharedPtr<IMediaOptions::FDataContainer, ESPMode::ThreadSafe> UImgMediaSource::GetMediaOption(const FName& Key, const TSharedPtr<FDataContainer, ESPMode::ThreadSafe>& DefaultValue) const
{
	if (Key == ImgMedia::MipMapInfoOption)
	{
		return MipMapInfo;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}

bool UImgMediaSource::HasMediaOption(const FName& Key) const
{
	if ((Key == ImgMedia::FrameRateOverrideDenonimatorOption) ||
		(Key == ImgMedia::FrameRateOverrideNumeratorOption) ||
		(Key == ImgMedia::ProxyOverrideOption) ||
		(Key == ImgMedia::MipMapInfoOption))
	{
		return true;
	}

	return Super::HasMediaOption(Key);
}


/* UMediaSource interface
 *****************************************************************************/

FString UImgMediaSource::GetUrl() const
{
	return FString(TEXT("img://")) + GetFullPath();
}


bool UImgMediaSource::Validate() const
{
	return FPaths::DirectoryExists(GetFullPath());
}


/* UFileMediaSource implementation
 *****************************************************************************/

FString UImgMediaSource::GetFullPath() const
{
	if (!FPaths::IsRelative(SequencePath.Path))
	{
		return SequencePath.Path;
	}

	if (SequencePath.Path.StartsWith(TEXT("./")))
	{
		FString RelativeDir;
		if (IsPathRelativeToProjectRoot)
		{
			RelativeDir = FPaths::ProjectDir();
		}
		else
		{
			RelativeDir = FPaths::ProjectContentDir();
		}
		return FPaths::ConvertRelativePathToFull(RelativeDir, SequencePath.Path.RightChop(2));
	}

	return FPaths::ConvertRelativePathToFull(SequencePath.Path);
}
