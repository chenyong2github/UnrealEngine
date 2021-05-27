// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraAnimToTemplateSequenceConverter.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraAnim.h"
#include "CameraAnimationSequence.h"
#include "ContentBrowserMenuContexts.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogatedPropertyInstantiator.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Platform.h"
#include "IAssetRegistry.h"
#include "IAssetTools.h"
#include "LevelEditorViewport.h"
#include "Matinee/InterpData.h"
#include "Matinee/InterpGroupCamera.h"
#include "Matinee/MatineeActorCameraAnim.h"
#include "MatineeConverter.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CoreMiscDefines.h"
#include "Misc/MessageDialog.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "ToolMenuContext.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneByteSection.h"
#include "Sections/MovieSceneEnumSection.h"
#include "Sections/MovieSceneIntegerSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneByteTrack.h"
#include "Tracks/MovieSceneEnumTrack.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Widgets/Notifications/SNotificationList.h"
PRAGMA_DISABLE_OPTIMIZATION
#define LOCTEXT_NAMESPACE "CameraAnimToTemplateSequenceConverter"

namespace UE
{
namespace MovieScene
{

template<typename TrackType, typename ValueType>
void SetupPPSettingPropertyTrack(TrackType* Track)
{
}

template<>
void SetupPPSettingPropertyTrack<UMovieSceneVectorTrack, FVector2D>(UMovieSceneVectorTrack* Track)
{
	Track->SetNumChannelsUsed(2);
}

template<>
void SetupPPSettingPropertyTrack<UMovieSceneVectorTrack, FVector>(UMovieSceneVectorTrack* Track)
{
	Track->SetNumChannelsUsed(3);
}

template<>
void SetupPPSettingPropertyTrack<UMovieSceneVectorTrack, FVector4>(UMovieSceneVectorTrack* Track)
{
	Track->SetNumChannelsUsed(4);
}

template<typename ValueType, bool bIsEnum=false>
ValueType GetPPSettingPropertyValue(const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	const ValueType* ValuePtr = Property.ContainerPtrToValuePtr<ValueType>(const_cast<FPostProcessSettings*>(&PPSettings));
	check(ValuePtr);
	return *ValuePtr;
}

template<>
bool GetPPSettingPropertyValue<bool, false>(const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	return CastFieldChecked<FBoolProperty>(&Property)->GetPropertyValue_InContainer(const_cast<FPostProcessSettings*>(&PPSettings));
}

template<>
uint8 GetPPSettingPropertyValue<uint8, false>(const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	return CastFieldChecked<FByteProperty>(&Property)->GetPropertyValue_InContainer(const_cast<FPostProcessSettings*>(&PPSettings));
}

template<>
uint8 GetPPSettingPropertyValue<uint8, true>(const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(&Property);
	const FByteProperty* UnderlyingByteProperty = CastFieldChecked<FByteProperty>(EnumProperty->GetUnderlyingProperty());
	return UnderlyingByteProperty->GetPropertyValue_InContainer(const_cast<FPostProcessSettings*>(&PPSettings));
}

template<>
int32 GetPPSettingPropertyValue<int32, false>(const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	return CastFieldChecked<FIntProperty>(&Property)->GetPropertyValue_InContainer(const_cast<FPostProcessSettings*>(&PPSettings));
}

template<>
float GetPPSettingPropertyValue<float, false>(const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	return CastFieldChecked<FFloatProperty>(&Property)->GetPropertyValue_InContainer(const_cast<FPostProcessSettings*>(&PPSettings));
}

template<typename SectionType, typename ValueType>
void SetDefaultPPSettingPropertyChannels(SectionType* Section, const ValueType Value);

// All single-channel properties (bool, byte, enum, integer, float).
template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneBoolSection, bool>(UMovieSceneBoolSection* Section, const bool Value)
{
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneBoolChannel>(0)->SetDefault(Value);
}

template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneByteSection, uint8>(UMovieSceneByteSection* Section, const uint8 Value)
{
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneByteChannel>(0)->SetDefault(Value);
}

template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneEnumSection, uint8>(UMovieSceneEnumSection* Section, const uint8 Value)
{
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneByteChannel>(0)->SetDefault(Value);
}

template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneIntegerSection, int32>(UMovieSceneIntegerSection* Section, const int32 Value)
{
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneIntegerChannel>(0)->SetDefault(Value);
}

template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneFloatSection, float>(UMovieSceneFloatSection* Section, const float Value)
{
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(Value);
}

// Vector properties.
template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneVectorSection, FVector2D>(UMovieSceneVectorSection* Section, const FVector2D Value)
{
	Section->SetChannelsUsed(2);

	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(Value.X);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(1)->SetDefault(Value.Y);
}

template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneVectorSection, FVector>(UMovieSceneVectorSection* Section, const FVector Value)
{
	Section->SetChannelsUsed(3);

	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(Value.X);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(1)->SetDefault(Value.Y);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(2)->SetDefault(Value.Z);
}

template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneVectorSection, FVector4>(UMovieSceneVectorSection* Section, const FVector4 Value)
{
	Section->SetChannelsUsed(4);

	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(Value.X);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(1)->SetDefault(Value.Y);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(2)->SetDefault(Value.Z);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(3)->SetDefault(Value.W);
}

// Color properties.
// Only FLinearColor exists in PPSettings (no FColor and certainly no FSlateColor).
template<>
void SetDefaultPPSettingPropertyChannels<UMovieSceneColorSection, FLinearColor>(UMovieSceneColorSection* Section, const FLinearColor Value)
{
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(Value.R);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(1)->SetDefault(Value.G);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(2)->SetDefault(Value.B);
	ChannelProxy.GetChannel<FMovieSceneFloatChannel>(3)->SetDefault(Value.A);
}

template<typename TrackType, typename SectionType, typename ValueType, bool bIsEnum=false>
void CreateDefaultPPSettingPropertyValueTrack(UMovieScene& MovieScene, const FGuid& ObjectBindingID, const FPostProcessSettings& PPSettings, const FProperty& Property)
{
	ensure(ObjectBindingID.IsValid());

	TStringBuilder<256> PropertyPathBuilder;
	PropertyPathBuilder.Append(GET_MEMBER_NAME_STRING_CHECKED(UCameraComponent, PostProcessSettings));
	PropertyPathBuilder.Append(TEXT("."));
	PropertyPathBuilder.Append(Property.GetName());

	const FName PropertyName = Property.GetFName();
	const FString PropertyPath = PropertyPathBuilder.ToString();
	const FName PropertyPathName(*PropertyPath);

	// Property tracks use the property path for their unique name.
	TrackType* PropertyTrack = MovieScene.FindTrack<TrackType>(ObjectBindingID, PropertyPathName);
	if (PropertyTrack != nullptr)
	{
		// This property is already animated, leave it alone.
		return;
	}

	PropertyTrack = MovieScene.AddTrack<TrackType>(ObjectBindingID);
	PropertyTrack->SetPropertyNameAndPath(PropertyName, PropertyPath);
	SetupPPSettingPropertyTrack<TrackType, ValueType>(PropertyTrack);

	// Make a new infinite section.
	UMovieSceneSection* PropertySection = PropertyTrack->CreateNewSection();
	PropertySection->SetRange(TRange<FFrameNumber>::All());

	SectionType* TypedPropertySection = CastChecked<SectionType>(PropertySection);
	const ValueType Value = GetPPSettingPropertyValue<ValueType, bIsEnum>(PPSettings, Property);
	SetDefaultPPSettingPropertyChannels<SectionType, ValueType>(TypedPropertySection, Value);

	PropertyTrack->AddSection(*PropertySection);
}

}
}

FCameraAnimToTemplateSequenceConverter::FCameraAnimToTemplateSequenceConverter(const FMatineeConverter* InMatineeConverter)
	: MatineeConverter(InMatineeConverter)
	, PreviewMatineeActor(nullptr)
{
}

void FCameraAnimToTemplateSequenceConverter::ConvertCameraAnim(const FToolMenuContext& MenuContext)
{
	using namespace UE::MovieScene;

	UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
	if (Context == nullptr)
	{
		return;
	}

	// Get the assets to convert.
	TArray<UCameraAnim*> CameraAnimsToConvert;
	for (TWeakObjectPtr<UObject> SelectedObject : Context->SelectedObjects)
	{
		if (UCameraAnim* CameraAnimToConvert = CastChecked<UCameraAnim>(SelectedObject.Get(), ECastCheckedType::NullAllowed))
		{
			CameraAnimsToConvert.Add(CameraAnimToConvert);
		}
	}
	if (CameraAnimsToConvert.Num() == 0)
	{
		return;
	}

	// Find the factory class.
	UFactory* CameraAnimationSequenceFactoryNew = FindFactoryForClass(UCameraAnimationSequence::StaticClass());
	ensure(CameraAnimationSequenceFactoryNew != nullptr);

	// Convert all selected camera anims.
	int32 NumWarnings = 0;
	TOptional<bool> bAutoReuseExistingAsset;
	bool bAssetCreated = false;
	bool bConvertSuccess = false;
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	for (UCameraAnim* CameraAnimToConvert : CameraAnimsToConvert)
	{
		bConvertSuccess = ConvertSingleCameraAnimToTemplateSequence(
				CameraAnimToConvert, AssetTools, AssetRegistry, CameraAnimationSequenceFactoryNew, true, bAutoReuseExistingAsset, NumWarnings, bAssetCreated)
			|| bConvertSuccess;
	}

	if (bConvertSuccess)
	{
		FText NotificationText = FText::Format(
				LOCTEXT("CameraAnim_ConvertToSequence_Notification", "Converted {0} assets with {1} warnings"),
				FText::AsNumber(CameraAnimsToConvert.Num()), FText::AsNumber(NumWarnings));
		FNotificationInfo NotificationInfo(NotificationText);
		NotificationInfo.ExpireDuration = 5.f;
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); });
		NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
}

UObject* FCameraAnimToTemplateSequenceConverter::ConvertCameraAnim(IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, UCameraAnim* CameraAnim, TOptional<bool>& bAutoReuseExistingAsset, int32& NumWarnings, bool& bAssetCreated)
{
	using namespace UE::MovieScene;

	if (!ensure(CameraAnim))
	{
		return nullptr;
	}

	ensure(CameraAnimationSequenceFactoryNew != nullptr);
	UObject* NewAsset = ConvertSingleCameraAnimToTemplateSequence(
			CameraAnim, AssetTools, AssetRegistry, CameraAnimationSequenceFactoryNew, false, bAutoReuseExistingAsset, NumWarnings, bAssetCreated);
	return NewAsset;
}

UObject* FCameraAnimToTemplateSequenceConverter::ConvertSingleCameraAnimToTemplateSequence(UCameraAnim* CameraAnimToConvert, IAssetTools& AssetTools, IAssetRegistry& AssetRegistry, UFactory* CameraAnimationSequenceFactoryNew, bool bPromptCreateAsset, TOptional<bool>& bAutoReuseExistingAsset, int32& NumWarnings, bool& bAssetCreated)
{
	UPackage* AssetPackage = CameraAnimToConvert->GetOutermost();

	// See if the converted asset already exists.
	UObject* ExistingAsset = nullptr;
	TArray<FAssetData> ExistingCameraAnimSequences;
	FString NewCameraAnimSequencePackageName = AssetPackage->GetName() + FString("Sequence");
	AssetRegistry.GetAssetsByPackageName(FName(NewCameraAnimSequencePackageName), ExistingCameraAnimSequences, false);
	if (ExistingCameraAnimSequences.Num() > 0)
	{
		// Find if there's any camera animation sequence in there. If there is, we can be somewhat confident
		// that it's going to be what we want.
		for (const FAssetData& ExistingCameraAnimSequence : ExistingCameraAnimSequences)
		{
			if (ExistingCameraAnimSequence.AssetClass == UCameraAnimationSequence::StaticClass()->GetFName())
			{
				ExistingAsset = ExistingCameraAnimSequence.GetAsset();
				if (ExistingAsset != nullptr)
				{
					break;
				}
			}
		}
	}
	if (ExistingAsset != nullptr)
	{
		bool bUseExistingAsset = true;

		if (!bAutoReuseExistingAsset.IsSet())
		{
			// Ask whether we should reuse the existing asset or make a new one.
			const EAppReturnType::Type DialogAnswer = FMessageDialog::Open(
					EAppMsgType::YesNoYesAllNoAll, 
					FText::Format(LOCTEXT("AskReuseExistingAsset", "Found camera animation sequence {0} while trying to convert Matinee camera anim {1}. Do you want to re-use the existing camera animation sequence and skip the conversion process?"), FText::FromName(ExistingAsset->GetPackage()->GetFName()), FText::FromName(CameraAnimToConvert->GetPackage()->GetFName())));
			switch (DialogAnswer)
			{
				case EAppReturnType::YesAll:
				default:
					bAutoReuseExistingAsset = true;
					bUseExistingAsset = true;
					break;
				case EAppReturnType::Yes:
					bUseExistingAsset = true;
					break;
				case EAppReturnType::NoAll:
					bAutoReuseExistingAsset = false;
					bUseExistingAsset = false;
					break;
				case EAppReturnType::No:
					bUseExistingAsset = false;
					break;
			}
		}

		if (bUseExistingAsset)
		{
			bAssetCreated = false;
			return ExistingAsset;
		}
	}

	// No existing asset, let's create our own.
	UObject* NewAsset = nullptr;
	FString NewCameraAnimSequenceName = CameraAnimToConvert->GetName() + FString("Sequence");
	FString AssetPackagePath = FPaths::GetPath(AssetPackage->GetPathName());
	if (bPromptCreateAsset)
	{
		NewAsset = AssetTools.CreateAssetWithDialog(NewCameraAnimSequenceName, AssetPackagePath, UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
	}
	else
	{
		NewAsset = AssetTools.CreateAsset(NewCameraAnimSequenceName, AssetPackagePath, UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
	}
	if (NewAsset == nullptr)
	{
		return nullptr;
	}

	// Create the new sequence.
	UCameraAnimationSequence* NewSequence = Cast<UCameraAnimationSequence>(NewAsset);
	NewSequence->BoundActorClass = ACameraActor::StaticClass();

	UMovieScene* NewMovieScene = NewSequence->GetMovieScene();

	// Add the spawnable for the camera.
	ACameraActor* CameraTemplate = NewObject<ACameraActor>(NewMovieScene, ACameraActor::StaticClass());
	FGuid SpawnableGuid = NewMovieScene->AddSpawnable("CameraActor", *CameraTemplate);
	
	// Set sequence length.
	const int32 LengthInFrames = (CameraAnimToConvert->AnimLength * NewMovieScene->GetTickResolution()).FrameNumber.Value;
	NewMovieScene->SetPlaybackRange(FFrameNumber(0), LengthInFrames + 1);

	// Add spawning track for the camera.
	UMovieSceneSpawnTrack* NewSpawnTrack = NewMovieScene->AddTrack<UMovieSceneSpawnTrack>(SpawnableGuid);
	UMovieSceneSpawnSection* NewSpawnSection = CastChecked<UMovieSceneSpawnSection>(NewSpawnTrack->CreateNewSection());
	NewSpawnSection->GetChannel().SetDefault(true);
	NewSpawnSection->SetStartFrame(TRangeBound<FFrameNumber>());
	NewSpawnSection->SetEndFrame(TRangeBound<FFrameNumber>());
	NewSpawnTrack->AddSection(*NewSpawnSection);

	FGuid CameraComponentBindingID;
	
	// Add camera animation data.
	if (CameraAnimToConvert->CameraInterpGroup != nullptr)
	{
		// Construct a temporary matinee actor
		CreateMatineeActorForCameraAnim(CameraAnimToConvert);

		// Changed the actor type, but don't want to lose any properties from previous
		// so duplicate from old, but with new class
		check(CameraAnimToConvert->CameraInterpGroup);
		if (!CameraAnimToConvert->CameraInterpGroup->IsA(UInterpGroupCamera::StaticClass()))
		{
			CameraAnimToConvert->CameraInterpGroup = CastChecked<UInterpGroupCamera>(StaticDuplicateObject(CameraAnimToConvert->CameraInterpGroup, CameraAnimToConvert, TEXT("CameraAnimation"), RF_NoFlags, UInterpGroupCamera::StaticClass()));
		}

		UInterpGroupCamera* NewInterpGroup = CastChecked<UInterpGroupCamera>(CameraAnimToConvert->CameraInterpGroup);
		check(NewInterpGroup);

		if (PreviewMatineeActor.Get()->MatineeData)
		{
			PreviewMatineeActor.Get()->MatineeData->SetFlags(RF_Transient);
			PreviewMatineeActor.Get()->MatineeData->InterpLength = CameraAnimToConvert->AnimLength;

			if (NewInterpGroup)
			{
				PreviewMatineeActor.Get()->MatineeData->InterpGroups.Add(NewInterpGroup);
			}
		}

		// Create a CameraActor and connect it to the Interp. will create this at the perspective viewport's location and rotation
		CreateCameraActorForCameraAnim(CameraAnimToConvert);

		// Set up the group actor
		PreviewMatineeActor.Get()->InitGroupActorForGroup(NewInterpGroup, PreviewCamera.Get());

		// This will create the instances for everything
		PreviewMatineeActor.Get()->InitInterp();

		// Actually do the conversion!
		MatineeConverter->ConvertInterpGroup(
				CameraAnimToConvert->CameraInterpGroup, SpawnableGuid, PreviewCamera.Get(), 
				NewSequence, NewMovieScene, NumWarnings);

		// Clean up all the temp stuff Matinee forced us to create.
		CleanUpActors();

		// Find the binding for the camera component.
		const FMovieScenePossessable* CameraComponentPossessable = NewMovieScene->FindPossessable(
				[](FMovieScenePossessable& InPossessable) { return InPossessable.GetName() == TEXT("CameraComponent"); });
		CameraComponentBindingID = (CameraComponentPossessable ? CameraComponentPossessable->GetGuid() : FGuid());

		// Fix-up the transform blend type for camera anims that have a "relative to initial transform" option enabled.
		if (CameraAnimToConvert->bRelativeToInitialTransform)
		{
			// We find the transform track on the spawnable itself (the camera actor) because the scene component is treated 
			// in a bit of a special way.
			UMovieScene3DTransformTrack* TransformTrack = NewMovieScene->FindTrack<UMovieScene3DTransformTrack>(SpawnableGuid);
			if (TransformTrack)
			{
				if (ensureMsgf(TransformTrack->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::AdditiveFromBase),
							TEXT("Camera animation with relative transform has a transform track that doesn't support AdditiveFromBase blend type"))
				   )
				{
					for (UMovieSceneSection* TransformSection : TransformTrack->GetAllSections())
					{
						TransformSection->SetBlendType(EMovieSceneBlendType::AdditiveFromBase);
					}
				}
			}
		}

		// Camera animations do some kind of relative animation for FOVs regardless of whether "relative to initial FOV"
		// is set or not... the only difference is that they will get their initial FOV value from evaluating the FOV track vs.
		// from the "initial FOV" property value.
		// We are going to make our FOV track "additive from base" either way, but report a warning if the values don't match.
		// This warning can probably be ignored because the previous asset was probably wrong in that case, unless it was 
		// support to make the FOV "pop" on purpose on the first frame.
		if (CameraComponentPossessable)
		{
			UMovieSceneFloatTrack* FOVTrack = NewMovieScene->FindTrack<UMovieSceneFloatTrack>(CameraComponentBindingID, GET_MEMBER_NAME_CHECKED(UCameraComponent, FieldOfView));
			if (
					FOVTrack &&
					ensureMsgf(FOVTrack->GetSupportedBlendTypes().Contains(EMovieSceneBlendType::AdditiveFromBase),
						TEXT("Camera animation has an FOV track that doesn't support AdditiveFromBase blend type"))
					)
			{
				for (UMovieSceneSection* FOVSection : FOVTrack->GetAllSections())
				{
					FOVSection->SetBlendType(EMovieSceneBlendType::AdditiveFromBase);
				}
			}

			if (!CameraAnimToConvert->bRelativeToInitialFOV)
			{
				using namespace UE::MovieScene;

				FSystemInterrogator Interrogator;
				TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &Interrogator.GetLinker()->EntityManager);

				Interrogator.ImportTrack(FOVTrack, FInterrogationChannel::Default());
				Interrogator.AddInterrogation(NewMovieScene->GetPlaybackRange().HasLowerBound() ?
						NewMovieScene->GetPlaybackRange().GetLowerBoundValue() : FFrameNumber(0));
				Interrogator.Update();

				TArray<float> AnimatedFOVs;
				FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();
				Interrogator.QueryPropertyValues(TracksComponents->Float, AnimatedFOVs);

				if (ensure(AnimatedFOVs.Num() == 1))
				{
					if (!FMath::IsNearlyEqual(AnimatedFOVs[0], CameraAnimToConvert->BaseFOV))
					{
						UE_LOG(LogMatineeToLevelSequence, Warning, 
								TEXT("Camera animation '%s' has an FOV track that starts with a value of '%f' but the base FOV value is '%f'. The converted animation will use 'AdditiveFromBase' and animate values relative to '%f'."), 
								*CameraAnimToConvert->GetName(), AnimatedFOVs[0], CameraAnimToConvert->BaseFOV, AnimatedFOVs[0]);
						++NumWarnings;
					}
				}
			}
		}
	}

	auto EnsureCameraComponentBinding = [&CameraComponentBindingID, CameraTemplate, SpawnableGuid, NewMovieScene, NewSequence]()
	{
		if (!CameraComponentBindingID.IsValid())
		{
			// We need to create an object binding for the camera component.
			UCameraComponent* CameraComponent = CameraTemplate->GetCameraComponent();
			CameraComponentBindingID = NewMovieScene->AddPossessable(CameraComponent->GetName(), CameraComponent->GetClass());
			FMovieScenePossessable* CameraComponentPossessable = NewMovieScene->FindPossessable(CameraComponentBindingID);
			CameraComponentPossessable->SetParent(SpawnableGuid);
			NewSequence->BindPossessableObject(CameraComponentBindingID, *CameraComponent, nullptr);
		}
	};

	// Add a track for the post FX blend weight if it wasn't animated.
	if (CameraAnimToConvert->BasePostProcessBlendWeight != 1.f)
	{
		const FName PostProcessBlendWeightPropertyName = GET_MEMBER_NAME_CHECKED(UCameraComponent, PostProcessBlendWeight);

		EnsureCameraComponentBinding();

		UMovieSceneFloatTrack* PPBlendWeightTrack = NewMovieScene->FindTrack<UMovieSceneFloatTrack>(CameraComponentBindingID, PostProcessBlendWeightPropertyName);
		if (!PPBlendWeightTrack)
		{
			PPBlendWeightTrack = NewMovieScene->AddTrack<UMovieSceneFloatTrack>(CameraComponentBindingID);
			PPBlendWeightTrack->SetPropertyNameAndPath(PostProcessBlendWeightPropertyName, PostProcessBlendWeightPropertyName.ToString()); 
			
			UMovieSceneFloatSection* PPBlendWeightSection = CastChecked<UMovieSceneFloatSection>(PPBlendWeightTrack->CreateNewSection());
			PPBlendWeightSection->SetRange(TRange<FFrameNumber>::All());

			FMovieSceneChannelProxy& ChannelProxy = PPBlendWeightSection->GetChannelProxy();
			ChannelProxy.GetChannel<FMovieSceneFloatChannel>(0)->SetDefault(CameraAnimToConvert->BasePostProcessBlendWeight);
			
			PPBlendWeightTrack->AddSection(*PPBlendWeightSection);
		}
	}

	// Add tracks for any default post FX settings that weren't animated.
	using namespace UE::MovieScene;
	const FPostProcessSettings& BasePPSettings = CameraAnimToConvert->BasePostProcessSettings;
	const UStruct* PPSettingsStruct = FPostProcessSettings::StaticStruct();
	const FProperty* ItProperty = PPSettingsStruct->PropertyLink;
	while (ItProperty)
	{
		// Move the iterator to the next property right away so we can easily early-continue in the following code.
		const FProperty* OverridenProperty = ItProperty;
		ItProperty = ItProperty->PropertyLinkNext;

		// We're going to look for all the properties with an EditCondition, and see if that EditCondition is toggled.
		// If it is, it either has a non-default value, or is animated. It's animated if we find a track for it, in which
		// case we leave it alone. If we do NOT find a track for it, then, we will create one with a default value.
		const FText EditConditionPropertyName = OverridenProperty->GetMetaDataText("editcondition");
		if (EditConditionPropertyName.IsEmpty())
		{
			// This is not an override toggle property.
			continue;
		}

		const FProperty* EditConditionProperty = PPSettingsStruct->FindPropertyByName(FName(*EditConditionPropertyName.ToString()));
		if (!ensure(EditConditionProperty))
		{
			// Error: can't find the property that the toggle is associated with.
			continue;
		}

		const FBoolProperty* EditConditionBoolProperty = CastField<FBoolProperty>(EditConditionProperty);
		if (!ensure(EditConditionBoolProperty))
		{
			continue;
		}

		const bool ToggleValue = EditConditionBoolProperty->GetPropertyValue_InContainer(&BasePPSettings);
		if (!ToggleValue)
		{
			// This override toggle property isn't checked.
			continue;
		}

		EnsureCameraComponentBinding();

		const FName OverridenPropertyClassName = OverridenProperty->GetClass()->GetFName();
		if (OverridenProperty->IsA<FBoolProperty>())
		{
			CreateDefaultPPSettingPropertyValueTrack<UMovieSceneBoolTrack, UMovieSceneBoolSection, bool>(
					*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenProperty);
		}
		else if (OverridenProperty->IsA<FByteProperty>())
		{
			CreateDefaultPPSettingPropertyValueTrack<UMovieSceneByteTrack, UMovieSceneByteSection, uint8>(
					*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenProperty);
		}
		else if (OverridenProperty->IsA<FEnumProperty>())
		{
			CreateDefaultPPSettingPropertyValueTrack<UMovieSceneEnumTrack, UMovieSceneEnumSection, uint8, true>(
					*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenProperty);
		}
		else if (OverridenProperty->IsA<FIntProperty>())
		{
			CreateDefaultPPSettingPropertyValueTrack<UMovieSceneIntegerTrack, UMovieSceneIntegerSection, int32>(
					*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenProperty);
		}
		else if (OverridenProperty->IsA<FFloatProperty>())
		{
			CreateDefaultPPSettingPropertyValueTrack<UMovieSceneFloatTrack, UMovieSceneFloatSection, float>(
					*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenProperty);
		}
		else if (OverridenProperty->IsA<FStructProperty>())
		{
			const FStructProperty* OverridenFieldProperty = CastField<FStructProperty>(OverridenProperty);
			if (OverridenFieldProperty->Struct == TBaseStructure<FVector2D>::Get())
			{
				CreateDefaultPPSettingPropertyValueTrack<UMovieSceneVectorTrack, UMovieSceneVectorSection, FVector2D>(
						*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenFieldProperty);
			}
			else if (OverridenFieldProperty->Struct == TBaseStructure<FVector>::Get())
			{
				CreateDefaultPPSettingPropertyValueTrack<UMovieSceneVectorTrack, UMovieSceneVectorSection, FVector>(
						*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenFieldProperty);
			}
			else if (OverridenFieldProperty->Struct == TBaseStructure<FVector4>::Get())
			{
				CreateDefaultPPSettingPropertyValueTrack<UMovieSceneVectorTrack, UMovieSceneVectorSection, FVector4>(
						*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenFieldProperty);
			}
			else if (OverridenFieldProperty->Struct == TBaseStructure<FLinearColor>::Get())
			{
				CreateDefaultPPSettingPropertyValueTrack<UMovieSceneColorTrack, UMovieSceneColorSection, FLinearColor>(
						*NewMovieScene, CameraComponentBindingID, BasePPSettings, *OverridenFieldProperty);
			}
			else
			{
				ensureAlwaysMsgf(false, TEXT("Unsupported struct property type: %s"), *OverridenPropertyClassName.ToString());
			}
		}
		else
		{
			ensureAlwaysMsgf(false, TEXT("Unknown property type: %s"), *OverridenPropertyClassName.ToString());
		}
	}

	bAssetCreated = true;
	return NewAsset;
}

void FCameraAnimToTemplateSequenceConverter::CreateMatineeActorForCameraAnim(UCameraAnim* InCameraAnim)
{
	check(InCameraAnim);

	FActorSpawnParameters ActorSpawnParameters;
	ActorSpawnParameters.Name = InCameraAnim->GetFName();
	PreviewMatineeActor = GEditor->GetEditorWorldContext().World()->SpawnActor<AMatineeActorCameraAnim>(ActorSpawnParameters);
	check(PreviewMatineeActor.IsValid());
	UInterpData* NewData = NewObject<UInterpData>(GetTransientPackage(), NAME_None, RF_Transactional);
	PreviewMatineeActor.Get()->MatineeData = NewData;
	PreviewMatineeActor.Get()->CameraAnim = InCameraAnim;
}

void FCameraAnimToTemplateSequenceConverter::CreateCameraActorForCameraAnim(UCameraAnim* InCameraAnim)
{
	check(InCameraAnim);

	FVector ViewportCamLocation(FVector::ZeroVector);
	FRotator ViewportCamRotation(FRotator::ZeroRotator);

	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient != NULL && ViewportClient->ViewportType == LVT_Perspective)
		{
			ViewportCamLocation = ViewportClient->ViewTransformPerspective.GetLocation();
			ViewportCamRotation = ViewportClient->ViewTransformPerspective.GetRotation();
			break;
		}
	}

	PreviewCamera = GEditor->GetEditorWorldContext().World()->SpawnActor<ACameraActor>(ViewportCamLocation, ViewportCamRotation);
	check(PreviewCamera.IsValid());
	PreviewCamera.Get()->SetFlags(RF_Transient);
	PreviewCamera.Get()->SetActorLabel(FText::Format(LOCTEXT("CamerAnimPreviewCameraName", "Preview Camera - {0}"), FText::FromName(InCameraAnim->GetFName())).ToString());

	// copy data from the CamAnim to the CameraActor
	check(PreviewCamera.Get()->GetCameraComponent());
	PreviewCamera.Get()->PreviewedCameraAnim = InCameraAnim;
	PreviewCamera.Get()->GetCameraComponent()->FieldOfView = InCameraAnim->BaseFOV;
	PreviewCamera.Get()->GetCameraComponent()->PostProcessSettings = InCameraAnim->BasePostProcessSettings;
}

void FCameraAnimToTemplateSequenceConverter::CleanUpActors()
{
	// clean up our preview actors if they are still present
	if(PreviewCamera.IsValid())
	{
		GEditor->GetEditorWorldContext().World()->DestroyActor(PreviewCamera.Get(), false, false);
		PreviewCamera.Reset();
	}

	if (PreviewMatineeActor.IsValid())
	{
		GEditor->GetEditorWorldContext().World()->DestroyActor(PreviewMatineeActor.Get(), false, false);
		PreviewMatineeActor.Reset();
	}
}
PRAGMA_ENABLE_OPTIMIZATION
#undef LOCTEXT_NAMESPACE
