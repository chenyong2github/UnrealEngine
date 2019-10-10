// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ContentBrowserModule.h"
#include "ContentBrowserLog.h"
#include "ContentBrowserSingleton.h"
#include "MRUFavoritesList.h"
#include "Settings/ContentBrowserSettings.h"

IMPLEMENT_MODULE( FContentBrowserModule, ContentBrowser );
DEFINE_LOG_CATEGORY(LogContentBrowser);
const FName FContentBrowserModule::NumberOfRecentAssetsName(TEXT("NumObjectsInRecentList"));

void FContentBrowserModule::StartupModule()
{
	ContentBrowserSingleton = new FContentBrowserSingleton();
	
	RecentlyOpenedAssets = MakeUnique<FMainMRUFavoritesList>(TEXT("ContentBrowserRecent"), GetDefault<UContentBrowserSettings>()->NumObjectsInRecentList);
	RecentlyOpenedAssets->ReadFromINI();

	UContentBrowserSettings::OnSettingChanged().AddRaw(this, &FContentBrowserModule::ResizeRecentAssetList);
}

void FContentBrowserModule::ShutdownModule()
{	
	if ( ContentBrowserSingleton )
	{
		delete ContentBrowserSingleton;
		ContentBrowserSingleton = NULL;
	}
	UContentBrowserSettings::OnSettingChanged().RemoveAll(this);
	RecentlyOpenedAssets.Reset();
}

IContentBrowserSingleton& FContentBrowserModule::Get() const
{
	check(ContentBrowserSingleton);
	return *ContentBrowserSingleton;
}

FDelegateHandle FContentBrowserModule::AddAssetViewExtraStateGenerator(const FAssetViewExtraStateGenerator& Generator)
{
	AssetViewExtraStateGenerators.Add(Generator);
	return Generator.Handle;
}

void FContentBrowserModule::RemoveAssetViewExtraStateGenerator(const FDelegateHandle& GeneratorHandle)
{
	AssetViewExtraStateGenerators.RemoveAll([&GeneratorHandle](const FAssetViewExtraStateGenerator& Generator) { return Generator.Handle == GeneratorHandle; });
}

void FContentBrowserModule::ResizeRecentAssetList(FName InName)
{
	if (InName == NumberOfRecentAssetsName)
	{
		RecentlyOpenedAssets->WriteToINI();
		RecentlyOpenedAssets = MakeUnique<FMainMRUFavoritesList>(TEXT("ContentBrowserRecent"), GetDefault<UContentBrowserSettings>()->NumObjectsInRecentList);
		RecentlyOpenedAssets->ReadFromINI();
	}
}
