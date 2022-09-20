// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/PerlinNoiseChannelInterface.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "Channels/MovieSceneDoublePerlinNoiseChannel.h"
#include "Channels/MovieSceneFloatPerlinNoiseChannel.h"
#include "IStructureDetailsView.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "PerlinNoiseChannelInterface"

FPerlinNoiseChannelSectionMenuExtension::FPerlinNoiseChannelSectionMenuExtension(TArrayView<const FMovieSceneChannelHandle> InChannelHandles, TArrayView<UMovieSceneSection* const> InSections)
	: ChannelHandles(InChannelHandles)
	, Sections(InSections)
{
}

void FPerlinNoiseChannelSectionMenuExtension::ExtendMenu(FMenuBuilder& MenuBuilder)
{
	TSharedRef<FPerlinNoiseChannelSectionMenuExtension> SharedThis = this->AsShared();

	if (ChannelHandles.Num() > 1)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("PerlinNoiseChannelsMenu", "Perlin Noise Channels"),
			LOCTEXT("PerlinNoiseChannelsMenuToolTip", "Edit parameters for Perlin Noise channels"),
			FNewMenuDelegate::CreateLambda([SharedThis](FMenuBuilder& InnerMenuBuilder) { SharedThis->BuildChannelsMenu(InnerMenuBuilder); })
		);
	}
	else if (ChannelHandles.Num() == 1)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("PerlinNoiseChannelsMenu", "Perlin Noise Channels"),
			LOCTEXT("PerlinNoiseChannelsMenuToolTip", "Edit parameters for Perlin Noise channels"),
			FNewMenuDelegate::CreateLambda([SharedThis](FMenuBuilder& InnerMenuBuilder) { SharedThis->BuildParametersMenu(InnerMenuBuilder, 0); })
		);
	}
}

void FPerlinNoiseChannelSectionMenuExtension::BuildChannelsMenu(FMenuBuilder& MenuBuilder)
{
	TArray<FMovieSceneChannelProxy*> ChannelProxies;
	for (UMovieSceneSection* Section : Sections)
	{
		ChannelProxies.Add(&Section->GetChannelProxy());
	}

	TArray<int32> ChannelHandleSectionIndexes;
	for (const FMovieSceneChannelHandle& ChannelHandle : ChannelHandles)
	{
		int32 SectionIndex = ChannelProxies.Find(ChannelHandle.GetChannelProxy());
		ChannelHandleSectionIndexes.Add(SectionIndex);
	}

	const bool bMultipleSections = Sections.Num() > 1;
	TSharedRef<FPerlinNoiseChannelSectionMenuExtension> SharedThis = this->AsShared();

	for (int32 Index = 0; Index < ChannelHandles.Num(); ++Index)
	{
		const int32 SectionIndex(ChannelHandleSectionIndexes[Index]);
		const FMovieSceneChannelHandle ChannelHandle(ChannelHandles[Index]);

		if (bMultipleSections)
		{
			MenuBuilder.AddSubMenu(
				FText::Format(LOCTEXT("PerlinNoiseChannelAndSectionSelectMenu", "Section{0}.{1}"), SectionIndex + 1, FText::FromName(ChannelHandle.GetMetaData()->Name)),
				LOCTEXT("PerlinNoiseChannelAndSectionSelectMenuToolTip", "Edit parameters for this Perlin Noise channel"),
				FNewMenuDelegate::CreateLambda([SharedThis, Index](FMenuBuilder& InnerMenuBuilder) { SharedThis->BuildParametersMenu(InnerMenuBuilder, Index); })
			);
		}
		else
		{
			MenuBuilder.AddSubMenu(
				FText::FromName(ChannelHandle.GetMetaData()->Name),
				LOCTEXT("PerlinNoiseChannelSelectMenuToolTip", "Edit parameters for this Perlin Noise channel"),
				FNewMenuDelegate::CreateLambda([SharedThis, Index](FMenuBuilder& InnerMenuBuilder) { SharedThis->BuildParametersMenu(InnerMenuBuilder, Index); })
			);
		}
	}
}

void FPerlinNoiseChannelSectionMenuExtension::BuildParametersMenu(FMenuBuilder& MenuBuilder, int32 ChannelHandleIndex)
{
	if (!ensure(ChannelHandles.IsValidIndex(ChannelHandleIndex)))
	{
		return;
	}

	FPerlinNoiseParams* PerlinNoiseParams = nullptr;
	FMovieSceneChannelHandle ChannelHandle(ChannelHandles[ChannelHandleIndex]);
	if (ChannelHandle.GetChannelTypeName() == FMovieSceneFloatPerlinNoiseChannel::StaticStruct()->GetFName())
	{
		FMovieSceneFloatPerlinNoiseChannel* FloatChannel = ChannelHandle.Cast<FMovieSceneFloatPerlinNoiseChannel>().Get();
		PerlinNoiseParams = &FloatChannel->PerlinNoiseParams;
	}
	else if (ChannelHandle.GetChannelTypeName() == FMovieSceneDoublePerlinNoiseChannel::StaticStruct()->GetFName())
	{
		FMovieSceneDoublePerlinNoiseChannel* DoubleChannel = ChannelHandle.Cast<FMovieSceneDoublePerlinNoiseChannel>().Get();
		PerlinNoiseParams = &DoubleChannel->PerlinNoiseParams;
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown perlin noise channel type: %s"), *ChannelHandle.GetChannelTypeName().ToString());
		return;
	}

	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bShowScrollBar = false;

	FStructureDetailsViewArgs StructureDetailsViewArgs;
	StructureDetailsViewArgs.bShowObjects = true;
	StructureDetailsViewArgs.bShowAssets = true;
	StructureDetailsViewArgs.bShowClasses = true;
	StructureDetailsViewArgs.bShowInterfaces = true;

	TSharedRef<FStructOnScope> StructData = MakeShareable(new FStructOnScope(FPerlinNoiseParams::StaticStruct(), (uint8*)PerlinNoiseParams));

	TSharedRef<IStructureDetailsView> DetailsView = EditModule.CreateStructureDetailView(DetailsViewArgs, StructureDetailsViewArgs, StructData);

	MenuBuilder.AddWidget(DetailsView->GetWidget().ToSharedRef(), FText(), true, false);
}

#undef LOCTEXT_NAMESPACE

