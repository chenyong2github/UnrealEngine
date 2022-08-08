// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPageManager.h"
#include "RenderPagesUtils.h"
#include "IRenderPagesModule.h"
#include "LevelSequence.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MovieScene.h"
#include "UObject/ObjectSaveContext.h"


URenderPage::URenderPage()
	: Id(FGuid::NewGuid())
	, WaitFramesBeforeRendering(0)
	, Sequence(nullptr)
	, bOverrideStartFrame(false)
	, CustomStartFrame(0)
	, bOverrideEndFrame(false)
	, CustomEndFrame(0)
	, bOverrideResolution(false)
	, CustomResolution(FIntPoint(3840, 2160))
	, bIsEnabled(true)
	, RenderPreset(nullptr)
{}

TOptional<int32> URenderPage::GetSequenceStartFrame() const
{
	if (!IsValid(Sequence))
	{
		return TOptional<int32>();
	}

	const UMovieScene* MovieScene = Sequence->MovieScene;
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();

	if (bOverrideStartFrame || (IsValid(Settings) && Settings->bUseCustomPlaybackRange))
	{
		int32 Frame = (bOverrideStartFrame ? CustomStartFrame : Settings->CustomStartFrame);
		if (IsValid(Settings) && Settings->bUseCustomFrameRate)
		{
			return FMath::FloorToInt(Frame / (Settings->OutputFrameRate / DisplayRate).AsDecimal());
		}
		return Frame;
	}

	const int32 StartFrameNumber = MovieScene->GetPlaybackRange().GetLowerBoundValue().Value;
	return FMath::FloorToInt(StartFrameNumber / (TickResolution / DisplayRate).AsDecimal());
}

TOptional<int32> URenderPage::GetSequenceEndFrame() const
{
	if (!IsValid(Sequence))
	{
		return TOptional<int32>();
	}

	const UMovieScene* MovieScene = Sequence->MovieScene;
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();

	if (bOverrideEndFrame || (IsValid(Settings) && Settings->bUseCustomPlaybackRange))
	{
		int32 Frame = (bOverrideEndFrame ? CustomEndFrame : Settings->CustomEndFrame);
		if (IsValid(Settings) && Settings->bUseCustomFrameRate)
		{
			return FMath::FloorToInt(Frame / (Settings->OutputFrameRate / DisplayRate).AsDecimal());
		}
		return Frame;
	}

	const int32 EndFrameNumber = MovieScene->GetPlaybackRange().GetUpperBoundValue().Value;
	return FMath::FloorToInt(EndFrameNumber / (TickResolution / DisplayRate).AsDecimal());
}

bool URenderPage::SetSequenceStartFrame(const int32 NewCustomStartFrame)
{
	int32 StartFrame = GetStartFrame().Get(0);
	SetIsUsingCustomStartFrame(true);
	SetCustomStartFrame(StartFrame);

	TOptional<int32> SequenceStartFrame = GetSequenceStartFrame();
	while (SequenceStartFrame.IsSet() && (SequenceStartFrame.Get(0) > NewCustomStartFrame))
	{
		StartFrame--;
		if (StartFrame <= INT32_MIN)
		{
			return false;
		}
		SetCustomStartFrame(StartFrame);
		SequenceStartFrame = GetSequenceStartFrame();
	}
	while (SequenceStartFrame.IsSet() && (SequenceStartFrame.Get(0) < NewCustomStartFrame))
	{
		StartFrame++;
		if (StartFrame >= INT32_MAX)
		{
			return false;
		}
		SetCustomStartFrame(StartFrame);
		SequenceStartFrame = GetSequenceStartFrame();
	}
	return (SequenceStartFrame.IsSet() && (SequenceStartFrame.Get(0) == NewCustomStartFrame));
}

bool URenderPage::SetSequenceEndFrame(const int32 NewCustomStartFrame)
{
	int32 EndFrame = GetEndFrame().Get(0);
	SetIsUsingCustomEndFrame(true);
	SetCustomEndFrame(EndFrame);

	TOptional<int32> SequenceEndFrame = GetSequenceEndFrame();
	while (SequenceEndFrame.IsSet() && (SequenceEndFrame.Get(0) > NewCustomStartFrame))
	{
		EndFrame--;
		if (EndFrame <= INT32_MIN)
		{
			return false;
		}
		SetCustomEndFrame(EndFrame);
		SequenceEndFrame = GetSequenceEndFrame();
	}
	while (SequenceEndFrame.IsSet() && (SequenceEndFrame.Get(0) < NewCustomStartFrame))
	{
		EndFrame++;
		if (EndFrame >= INT32_MAX)
		{
			return false;
		}
		SetCustomEndFrame(EndFrame);
		SequenceEndFrame = GetSequenceEndFrame();
	}
	return (SequenceEndFrame.IsSet() && (SequenceEndFrame.Get(0) == NewCustomStartFrame));
}

TOptional<int32> URenderPage::GetStartFrame() const
{
	if (bOverrideStartFrame)
	{
		return CustomStartFrame;
	}

	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();
	if (IsValid(Settings) && Settings->bUseCustomPlaybackRange)
	{
		return Settings->CustomStartFrame;
	}

	if (!IsValid(Sequence))
	{
		return TOptional<int32>();
	}

	const UMovieScene* MovieScene = Sequence->MovieScene;
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (IsValid(Settings) && Settings->bUseCustomFrameRate)
	{
		DisplayRate = Settings->OutputFrameRate;
	}
	const int32 StartFrameNumber = MovieScene->GetPlaybackRange().GetLowerBoundValue().Value;
	return FMath::FloorToInt(StartFrameNumber / (TickResolution / DisplayRate).AsDecimal());
}

TOptional<int32> URenderPage::GetEndFrame() const
{
	if (bOverrideEndFrame)
	{
		return CustomEndFrame;
	}

	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();
	if (IsValid(Settings) && Settings->bUseCustomPlaybackRange)
	{
		return Settings->CustomEndFrame;
	}

	if (!IsValid(Sequence))
	{
		return TOptional<int32>();
	}

	const UMovieScene* MovieScene = Sequence->MovieScene;
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (IsValid(Settings) && Settings->bUseCustomFrameRate)
	{
		DisplayRate = Settings->OutputFrameRate;
	}
	const int32 EndFrameNumber = MovieScene->GetPlaybackRange().GetUpperBoundValue().Value;
	return FMath::FloorToInt(EndFrameNumber / (TickResolution / DisplayRate).AsDecimal());
}

TOptional<double> URenderPage::GetStartTime() const
{
	if (!IsValid(Sequence))
	{
		return TOptional<double>();
	}

	const TOptional<int32> StartFrame = GetStartFrame();
	if (!StartFrame.IsSet())
	{
		return TOptional<double>();
	}

	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();

	const UMovieScene* MovieScene = Sequence->MovieScene;
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (IsValid(Settings) && Settings->bUseCustomFrameRate)
	{
		DisplayRate = Settings->OutputFrameRate;
	}
	return StartFrame.Get(0) / DisplayRate.AsDecimal();
}

TOptional<double> URenderPage::GetEndTime() const
{
	if (!IsValid(Sequence))
	{
		return TOptional<double>();
	}

	const TOptional<int32> EndFrame = GetEndFrame();
	if (!EndFrame.IsSet())
	{
		return TOptional<double>();
	}

	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();

	const UMovieScene* MovieScene = Sequence->MovieScene;
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (IsValid(Settings) && Settings->bUseCustomFrameRate)
	{
		DisplayRate = Settings->OutputFrameRate;
	}
	return EndFrame.Get(0) / DisplayRate.AsDecimal();
}

TOptional<double> URenderPage::GetDurationInSeconds() const
{
	if (!IsValid(Sequence))
	{
		return TOptional<double>();
	}

	const TOptional<int32> StartFrame = GetStartFrame();
	const TOptional<int32> EndFrame = GetEndFrame();
	if (!StartFrame.IsSet() || !EndFrame.IsSet() || (StartFrame.Get(0) > EndFrame.Get(0)))
	{
		return TOptional<double>();
	}

	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();

	const UMovieScene* MovieScene = Sequence->MovieScene;
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (IsValid(Settings) && Settings->bUseCustomFrameRate)
	{
		DisplayRate = Settings->OutputFrameRate;
	}
	return (EndFrame.Get(0) - StartFrame.Get(0)) / DisplayRate.AsDecimal();
}

double URenderPage::GetOutputAspectRatio() const
{
	const UMoviePipelineOutputSetting* Settings = GetRenderPresetOutputSettings();
	if (IsValid(Settings))
	{
		return static_cast<double>(Settings->OutputResolution.X) / static_cast<double>(Settings->OutputResolution.Y);
	}

	const UMoviePipelineOutputSetting* DefaultSettings = GetDefault<UMoviePipelineOutputSetting>();
	return static_cast<double>(DefaultSettings->OutputResolution.X) / static_cast<double>(DefaultSettings->OutputResolution.Y);
}

bool URenderPage::MatchesSearchTerm(const FString& SearchTerm) const
{
	if (SearchTerm.TrimStartAndEnd().Len() <= 0)
	{
		return true;
	}
	TArray<FString> Parts;
	SearchTerm.ParseIntoArray(Parts, TEXT(" "), true);
	for (const FString& Part : Parts)
	{
		FString TrimmedPart = Part.TrimStartAndEnd();
		if (TrimmedPart.Len() <= 0)
		{
			continue;
		}
		if ((PageId.Find(TrimmedPart) != INDEX_NONE) || (PageName.Find(TrimmedPart) != INDEX_NONE) || (OutputDirectory.Find(TrimmedPart) != INDEX_NONE))
		{
			continue;
		}
		if (IsValid(RenderPreset) && (RenderPreset.GetPath().Find(TrimmedPart) != INDEX_NONE))
		{
			continue;
		}
		return false;
	}
	return true;
}

FString URenderPage::PurgePageIdOrReturnEmptyString(const FString& NewPageId)
{
	static FString ValidCharacters = TEXT("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");

	FString Result;
	for (TCHAR NewPageIdChar : NewPageId)
	{
		int32 Index;
		if (ValidCharacters.FindChar(NewPageIdChar, Index))
		{
			Result += NewPageIdChar;
		}
	}
	return Result;
}

FString URenderPage::PurgePageId(const FString& NewPageId)
{
	FString Result = PurgePageIdOrReturnEmptyString(NewPageId);
	if (Result.IsEmpty())
	{
		return TEXT("0");
	}
	return Result;
}

FString URenderPage::PurgePageIdOrGenerateUniqueId(URenderPageCollection* PageCollection, const FString& NewPageId)
{
	FString Result = PurgePageIdOrReturnEmptyString(NewPageId);
	if (Result.IsEmpty())
	{
		return PageCollection->GenerateNextPageId();
	}
	return Result;
}

FString URenderPage::PurgePageName(const FString& NewPageName)
{
	return NewPageName.TrimStartAndEnd();
}

FString URenderPage::PurgeOutputDirectory(const FString& NewOutputDirectory)
{
	return UE::RenderPages::Private::FRenderPagesUtils::NormalizeOutputDirectory(FPaths::ConvertRelativePathToFull(NewOutputDirectory))
		.Replace(*UE::RenderPages::Private::FRenderPagesUtils::NormalizeOutputDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())), TEXT("{project_dir}/"));
}

FString URenderPage::GetOutputDirectory() const
{
	return UE::RenderPages::Private::FRenderPagesUtils::NormalizeOutputDirectory(FPaths::ConvertRelativePathToFull(OutputDirectory.Replace(TEXT("{project_dir}"), *FPaths::ProjectDir())));
}

UMoviePipelineOutputSetting* URenderPage::GetRenderPresetOutputSettings() const
{
	if (!IsValid(RenderPreset))
	{
		return nullptr;
	}
	for (UMoviePipelineSetting* Settings : RenderPreset->FindSettingsByClass(UMoviePipelineOutputSetting::StaticClass(), false))
	{
		if (!IsValid(Settings))
		{
			continue;
		}
		if (UMoviePipelineOutputSetting* OutputSettings = Cast<UMoviePipelineOutputSetting>(Settings))
		{
			if (!OutputSettings->IsEnabled())
			{
				continue;
			}
			return OutputSettings;
		}
	}
	return nullptr;
}

bool URenderPage::HasRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity) const
{
	if (!RemoteControlEntity.IsValid())
	{
		return false;
	}

	const FString Key = RemoteControlEntity->GetId().ToString();
	return !!RemoteControlValues.Find(Key);
}

bool URenderPage::ConstGetRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray) const
{
	if (!RemoteControlEntity.IsValid())
	{
		return false;
	}

	const FString Key = RemoteControlEntity->GetId().ToString();
	if (const FRenderPageRemoteControlPropertyData* DataPtr = RemoteControlValues.Find(Key))
	{
		OutBinaryArray.Append((*DataPtr).Bytes);
		return true;
	}
	return false;
}

bool URenderPage::GetRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray)
{
	if (!RemoteControlEntity.IsValid())
	{
		return false;
	}

	const FString Key = RemoteControlEntity->GetId().ToString();
	if (FRenderPageRemoteControlPropertyData* DataPtr = RemoteControlValues.Find(Key))
	{
		OutBinaryArray.Append((*DataPtr).Bytes);
		return true;
	}

	if (!URenderPagePropRemoteControl::GetValueOfEntity(RemoteControlEntity, OutBinaryArray))
	{
		return false;
	}
	RemoteControlValues.Add(Key, FRenderPageRemoteControlPropertyData(TArray(OutBinaryArray)));
	return true;
}

bool URenderPage::SetRemoteControlValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, const TArray<uint8>& BinaryArray)
{
	if (!RemoteControlEntity.IsValid())
	{
		return false;
	}
	const FString Key = RemoteControlEntity->GetId().ToString();
	RemoteControlValues.Add(Key, FRenderPageRemoteControlPropertyData(TArray(BinaryArray)));
	return true;
}


URenderPageCollection::URenderPageCollection()
	: Id(FGuid::NewGuid())
	, PropsSourceType(ERenderPagePropsSourceType::Local)
	, PropsSourceOrigin_RemoteControl(nullptr)
	, bExecutingPreRender(false)
	, bExecutingPostRender(false)
	, CachedPropsSource(nullptr)
	, CachedPropsSourceType(ERenderPagePropsSourceType::Local)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		LoadValuesFromCDO();
		OnPreSaveCDO().AddUObject(this, &URenderPageCollection::SaveValuesToCDO);
	}
}

UWorld* URenderPageCollection::GetWorld() const
{
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		// If we are a CDO, we must return nullptr instead of calling Outer->GetWorld() to fool UObject::ImplementsGetWorld.
		return nullptr;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			return Context.World();
		}
	}

	if (IsValid(GWorld))
	{
		return GWorld;
	}

	if (UWorld* CachedWorld = CachedWorldWeakPtr.Get(); IsValid(CachedWorld))
	{
		return CachedWorld;
	}
	UObject* Outer = GetOuter();// Could be a GameInstance, could be World, could also be a WidgetTree, so we're just going to follow the outer chain to find the world we're in.
	while (Outer)
	{
		UWorld* World = Outer->GetWorld();
		if (IsValid(World))
		{
			CachedWorldWeakPtr = World;
			return World;
		}
		Outer = Outer->GetOuter();
	}
	return nullptr;
}

void URenderPageCollection::PreSave(FObjectPreSaveContext SaveContext)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		OnPreSaveCDO().Broadcast();
	}
	Super::PreSave(SaveContext);
	SaveValuesToCDO();
}

void URenderPageCollection::PostLoad()
{
	Super::PostLoad();
	LoadValuesFromCDO();
}

void URenderPageCollection::PreRender(URenderPage* Page)
{
	bExecutingPreRender = true;
	ReceivePreRender(Page);
	bExecutingPreRender = false;
}

void URenderPageCollection::PostRender(URenderPage* Page)
{
	bExecutingPostRender = true;
	ReceivePostRender(Page);
	bExecutingPostRender = false;
}

void URenderPageCollection::CopyValuesToOrFromCDO(const bool bToCDO)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	URenderPageCollection* CDO = GetCDO();
	if (!IsValid(CDO))
	{
		return;
	}

	for (FProperty* Property = GetClass()->PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		if (!Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))// if not [Transient]
		{
			void* Data = Property->ContainerPtrToValuePtr<void>(this);
			void* DataCDO = Property->ContainerPtrToValuePtr<void>(CDO);
			if (bToCDO)
			{
				Property->CopyCompleteValue(DataCDO, Data);
			}
			else
			{
				Property->CopyCompleteValue(Data, DataCDO);
			}
		}
	}

	if (bToCDO)
	{
		CDO->RenderPages = TArray<TObjectPtr<URenderPage>>();
		for (URenderPage* Page : RenderPages)
		{
			if (!IsValid(Page))
			{
				continue;
			}
			if (URenderPage* DuplicatePage = DuplicateObject(Page, CDO); IsValid(DuplicatePage))
			{
				CDO->RenderPages.Add(DuplicatePage);
			}
		}
	}
}

void URenderPageCollection::SetPropsSource(ERenderPagePropsSourceType InPropsSourceType, UObject* InPropsSourceOrigin)
{
	if (InPropsSourceType == ERenderPagePropsSourceType::RemoteControl)
	{
		if (URemoteControlPreset* InPropsSourceOrigin_RemoteControl = Cast<URemoteControlPreset>(InPropsSourceOrigin))
		{
			PropsSourceType = InPropsSourceType;
			PropsSourceOrigin_RemoteControl = InPropsSourceOrigin_RemoteControl;
			return;
		}
	}
	PropsSourceType = ERenderPagePropsSourceType::Local;
}

URenderPagePropsSourceBase* URenderPageCollection::GetPropsSource() const
{
	UObject* PropsSourceOrigin = GetPropsSourceOrigin();
	if (!IsValid(CachedPropsSource) || (CachedPropsSourceType != PropsSourceType) || (CachedPropsSourceOriginWeakPtr.Get() != PropsSourceOrigin))
	{
		CachedPropsSourceType = PropsSourceType;
		CachedPropsSourceOriginWeakPtr = PropsSourceOrigin;
		CachedPropsSource = UE::RenderPages::IRenderPagesModule::Get().CreatePropsSource(const_cast<URenderPageCollection*>(this), PropsSourceType, PropsSourceOrigin);
	}
	return CachedPropsSource;
}

UObject* URenderPageCollection::GetPropsSourceOrigin() const
{
	if (PropsSourceType == ERenderPagePropsSourceType::RemoteControl)
	{
		return PropsSourceOrigin_RemoteControl;
	}
	return nullptr;
}

void URenderPageCollection::AddRenderPage(URenderPage* RenderPage)
{
	if (IsValid(RenderPage))
	{
		RenderPages.Add(RenderPage);
	}
}

void URenderPageCollection::RemoveRenderPage(URenderPage* RenderPage)
{
	RenderPages.Remove(RenderPage);
}

void URenderPageCollection::InsertRenderPage(URenderPage* RenderPage, int32 Index)
{
	if (IsValid(RenderPage))
	{
		RenderPages.Insert(RenderPage, Index);
	}
}

bool URenderPageCollection::HasRenderPage(URenderPage* RenderPage) const
{
	return (RenderPages.Find(RenderPage) != INDEX_NONE);
}

int32 URenderPageCollection::GetIndexOfRenderPage(URenderPage* RenderPage) const
{
	return RenderPages.Find(RenderPage);
}

TArray<URenderPage*> URenderPageCollection::GetRenderPages() const
{
	TArray<URenderPage*> Result;
	for (URenderPage* Page : RenderPages)
	{
		if (!IsValid(Page))
		{
			continue;
		}
		Result.Add(Page);
	}
	return Result;
}

TArray<URenderPage*> URenderPageCollection::GetEnabledRenderPages() const
{
	TArray<URenderPage*> Result;
	for (URenderPage* Page : RenderPages)
	{
		if (!IsValid(Page))
		{
			continue;
		}
		if (Page->GetIsEnabled())
		{
			Result.Add(Page);
		}
	}
	return Result;
}

TArray<URenderPage*> URenderPageCollection::GetDisabledRenderPages() const
{
	TArray<URenderPage*> Result;
	for (URenderPage* Page : RenderPages)
	{
		if (!IsValid(Page))
		{
			continue;
		}
		if (!Page->GetIsEnabled())
		{
			Result.Add(Page);
		}
	}
	return Result;
}

void URenderPageCollection::InsertRenderPageBefore(URenderPage* RenderPage, URenderPage* BeforeRenderPage)
{
	if (IsValid(RenderPage))
	{
		const TArray<TObjectPtr<URenderPage>>::SizeType Index = RenderPages.Find(BeforeRenderPage);
		if (Index == INDEX_NONE)
		{
			RenderPages.Add(RenderPage);
		}
		else
		{
			RenderPages.Insert(RenderPage, Index);
		}
	}
}

void URenderPageCollection::InsertRenderPageAfter(URenderPage* RenderPage, URenderPage* AfterRenderPage)
{
	if (IsValid(RenderPage))
	{
		const TArray<TObjectPtr<URenderPage>>::SizeType Index = RenderPages.Find(AfterRenderPage);
		if (Index == INDEX_NONE)
		{
			RenderPages.Add(RenderPage);
		}
		else
		{
			RenderPages.Insert(RenderPage, Index + 1);
		}
	}
}

FString URenderPageCollection::GenerateNextPageId()
{
	int32 Max = 0;
	for (URenderPage* Page : RenderPages)
	{
		if (!IsValid(Page))
		{
			continue;
		}
		int32 Value = FCString::Atoi(*Page->GetPageId());
		if (Value > Max)
		{
			Max = Value;
		}
	}
	FString Result = FString::FromInt(Max + 1);
	while (Result.Len() < UE::RenderPages::FRenderPageManager::GeneratedIdCharacterLength)
	{
		Result = TEXT("0") + Result;
	}
	return Result;
}

bool URenderPageCollection::DoesPageIdExist(const FString& PageId)
{
	const FString PageIdToLower = PageId.ToLower();
	for (URenderPage* Page : RenderPages)
	{
		if (!IsValid(Page))
		{
			continue;
		}
		if (PageIdToLower == Page->GetPageId().ToLower())
		{
			return true;
		}
	}
	return false;
}

URenderPage* URenderPageCollection::CreateAndAddNewRenderPage()
{
	URenderPage* Page = NewObject<URenderPage>(this);
	Page->SetPageId(GenerateNextPageId());
	Page->SetPageName(TEXT("New"));
	Page->SetOutputDirectory(FPaths::ProjectDir() / TEXT("Saved/MovieRenders/"));

	if (URenderPagePropsSourceRemoteControl* PropsSource = GetPropsSource<URenderPagePropsSourceRemoteControl>())
	{
		TArray<uint8> BinaryArray;
		for (URenderPagePropRemoteControl* Field : PropsSource->GetProps()->GetAllCasted())
		{
			if (Field->GetValue(BinaryArray))
			{
				Page->SetRemoteControlValue(Field->GetRemoteControlEntity(), BinaryArray);
			}
		}
	}

	AddRenderPage(Page);
	return Page;
}

URenderPage* URenderPageCollection::DuplicateAndAddRenderPage(URenderPage* Page)
{
	if (!IsValid(Page))
	{
		return nullptr;
	}

	if (URenderPage* DuplicateRenderPage = DuplicateObject(Page, this); IsValid(DuplicateRenderPage))
	{
		DuplicateRenderPage->GenerateNewId();
		DuplicateRenderPage->SetPageId(GenerateNextPageId());
		InsertRenderPageAfter(DuplicateRenderPage, Page);
		return DuplicateRenderPage;
	}
	return nullptr;
}

bool URenderPageCollection::ReorderRenderPage(URenderPage* Page, URenderPage* DroppedOnPage, const bool bAfter)
{
	if (!IsValid(Page) || !IsValid(DroppedOnPage) || !HasRenderPage(Page) || !HasRenderPage(DroppedOnPage))
	{
		return false;
	}

	RemoveRenderPage(Page);
	if (bAfter)
	{
		InsertRenderPageAfter(Page, DroppedOnPage);
	}
	else
	{
		InsertRenderPageBefore(Page, DroppedOnPage);
	}
	return true;
}
