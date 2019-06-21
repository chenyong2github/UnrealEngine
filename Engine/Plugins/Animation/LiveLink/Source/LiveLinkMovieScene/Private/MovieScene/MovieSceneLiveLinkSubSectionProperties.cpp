// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieScene/MovieSceneLiveLinkSubSectionProperties.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "MovieScene/MovieSceneLiveLinkBufferData.h"

#include "LiveLinkMovieScenePrivate.h"

#define LOCTEXT_NAMESPACE "MovieSceneLiveLinkSubSectionProperties"


namespace MovieSceneLiveLinkPropertiesUtil
{
	static const TArray<FString> TransformStrings =
	{
		"Translation.X",
		"Translation.Y",
		"Translation.Z",
		"Rotation.X",
		"Rotation.Y",
		"Rotation.Z",
		"Scale.X",
		"Scale.Y",
		"Scale.Z"
	};

	static const TArray<FString> VectorStrings =
	{
		"Vector.X",
		"Vector.Y",
		"Vector.Z",
	};

	static const TArray<FString> ColorStrings =
	{
		"Color.R",
		"Color.G",
		"Color.B",
		"Color.A",
	};
};



UMovieSceneLiveLinkSubSectionProperties::UMovieSceneLiveLinkSubSectionProperties(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMovieSceneLiveLinkSubSectionProperties::Initialize(TSubclassOf<ULiveLinkRole> InSubjectRole, const TSharedPtr<FLiveLinkStaticDataStruct>& InStaticData)
{
	Super::Initialize(InSubjectRole, InStaticData);

	UScriptStruct* ScriptStruct = SubjectRole.GetDefaultObject()->GetFrameDataStruct();
	CreatePropertiesChannel(ScriptStruct);
}

int32 UMovieSceneLiveLinkSubSectionProperties::CreateChannelProxy(int32 InChannelIndex, TArray<bool>& OutChannelMask, FMovieSceneChannelProxyData& OutChannelData)
{
	int32 StartIndex = InChannelIndex;
	InChannelIndex = 0;

	for (FLiveLinkPropertyData& Data : SubSectionData.Properties)
	{
		if(UProperty* PropertyPtr = FindField<UProperty>(SubjectRole.GetDefaultObject()->GetFrameDataStruct(), Data.PropertyName))
		{
			const FText PropertyName = FText::FromName(Data.PropertyName);
 			if (PropertyPtr->IsA(UFloatProperty::StaticClass()))
			{
#if WITH_EDITOR
				MovieSceneLiveLinkSectionUtils::CreateChannelEditor(PropertyName, Data.FloatChannel[0], StartIndex + InChannelIndex, TMovieSceneExternalValue<float>(), OutChannelMask, OutChannelData);
#else
				OutChannelData.Add(Data.FloatChannel[0]);
#endif //#WITH_EDITOR
				++InChannelIndex;
			}
			else if (PropertyPtr->IsA(UIntProperty::StaticClass()))
			{
#if WITH_EDITOR
				MovieSceneLiveLinkSectionUtils::CreateChannelEditor(PropertyName, Data.IntegerChannel[0], StartIndex + InChannelIndex, TMovieSceneExternalValue<int32>(), OutChannelMask, OutChannelData);
#else
				OutChannelData.Add(Data.IntegerChannel[0]);
#endif //#WITH_EDITOR
				++InChannelIndex;
			}
			else if (PropertyPtr->IsA(UBoolProperty::StaticClass()))
			{
#if WITH_EDITOR
				MovieSceneLiveLinkSectionUtils::CreateChannelEditor(PropertyName, Data.BoolChannel[0], StartIndex + InChannelIndex, TMovieSceneExternalValue<bool>(), OutChannelMask, OutChannelData);
#else
				OutChannelData.Add(Data.BoolChannel[0]);
#endif //#WITH_EDITOR
				++InChannelIndex;
			}
			else if (PropertyPtr->IsA(UStrProperty::StaticClass()))
			{
#if WITH_EDITOR
				MovieSceneLiveLinkSectionUtils::CreateChannelEditor(PropertyName, Data.StringChannel[0], StartIndex + InChannelIndex, TMovieSceneExternalValue<FString>(), OutChannelMask, OutChannelData);
#else
				OutChannelData.Add(Data.StringChannel[0]);
#endif //#WITH_EDITOR
				++InChannelIndex;
			}
			else if (PropertyPtr->IsA(UByteProperty::StaticClass()))
			{
#if WITH_EDITOR
				MovieSceneLiveLinkSectionUtils::CreateChannelEditor(PropertyName, Data.ByteChannel[0], StartIndex + InChannelIndex, TMovieSceneExternalValue<uint8>(), OutChannelMask, OutChannelData);
#else
				OutChannelData.Add(Data.ByteChannel[0]);
#endif //#WITH_EDITOR
				++InChannelIndex;
			}
			else if (PropertyPtr->IsA(UEnumProperty::StaticClass()))
			{
#if WITH_EDITOR
				MovieSceneLiveLinkSectionUtils::CreateChannelEditor(PropertyName, Data.ByteChannel[0], StartIndex + InChannelIndex, TMovieSceneExternalValue<uint8>(), OutChannelMask, OutChannelData);
#else
				OutChannelData.Add(Data.ByteChannel[0]);
#endif //#WITH_EDITOR
				++InChannelIndex;
			}
			else if (PropertyPtr->IsA(UStructProperty::StaticClass()))
			{
				UStructProperty* StructProperty = Cast<UStructProperty>(PropertyPtr);
				if (StructProperty->Struct->GetFName() == NAME_Transform)
				{
					for (int32 i = 0; i < MovieSceneLiveLinkPropertiesUtil::TransformStrings.Num(); ++i)
					{
						const FText DisplayName = FText::Format(LOCTEXT("LinkLinkFormat", "{0} : {1}"), PropertyName, FText::FromString(MovieSceneLiveLinkPropertiesUtil::TransformStrings[i]));
#if WITH_EDITOR
						MovieSceneLiveLinkSectionUtils::CreateChannelEditor(DisplayName, Data.FloatChannel[i], StartIndex + InChannelIndex, TMovieSceneExternalValue<float>(), OutChannelMask, OutChannelData);
#else
						OutChannelData.Add(Data.FloatChannel[i]);
#endif //#WITH_EDITOR
						++InChannelIndex;
					}
				}
				else if (StructProperty->Struct->GetFName() == NAME_Vector)
				{
					for (int32 i = 0; i < MovieSceneLiveLinkPropertiesUtil::VectorStrings.Num(); ++i)
					{
						const FText DisplayName = FText::Format(LOCTEXT("LinkLinkFormat", "{0} : {1}"), PropertyName, FText::FromString(MovieSceneLiveLinkPropertiesUtil::VectorStrings[i]));
#if WITH_EDITOR
						MovieSceneLiveLinkSectionUtils::CreateChannelEditor(DisplayName, Data.FloatChannel[i], StartIndex + InChannelIndex, TMovieSceneExternalValue<float>(), OutChannelMask, OutChannelData);
#else
						OutChannelData.Add(Data.FloatChannel[i]);
#endif //#WITH_EDITOR
						++InChannelIndex;
					}
				}
				else if (StructProperty->Struct->GetFName() == NAME_Color)
				{
					for (int32 i = 0; i < MovieSceneLiveLinkPropertiesUtil::ColorStrings.Num(); ++i)
					{
						const FText DisplayName = FText::Format(LOCTEXT("LinkLinkFormat", "{0} : {1}"), PropertyName, FText::FromString(MovieSceneLiveLinkPropertiesUtil::ColorStrings[i]));
#if WITH_EDITOR
						MovieSceneLiveLinkSectionUtils::CreateChannelEditor(DisplayName, Data.ByteChannel[i], StartIndex + InChannelIndex, TMovieSceneExternalValue<uint8>(), OutChannelMask, OutChannelData);
#else
						OutChannelData.Add(Data.ByteChannel[i]);
#endif //#WITH_EDITOR
						++InChannelIndex;
					}
				}
				else
				{
					//This should not happen. Supported properties should be filtered in UMovieSceneLiveLinkSubSectionProperties::IsPropertyTypeSupported with this code updated accordingly.
					UE_LOG(LogLiveLinkMovieScene, Warning, TEXT("Trying to create a proxy channel for subject role '%s' for an unsupported structure type '%s'"), *SubjectRole.GetDefaultObject()->GetDisplayName().ToString(), *StructProperty->Struct->GetFName().ToString());
				}
			}
			else
			{
				//This should not happen. Supported properties should be filtered in UMovieSceneLiveLinkSubSectionProperties::IsPropertyTypeSupported with this code updated accordingly.
				UE_LOG(LogLiveLinkMovieScene, Warning, TEXT("Trying to create a proxy channel for subject role '%s' for an unsupported property type '%s'"), *SubjectRole.GetDefaultObject()->GetDisplayName().ToString(), *PropertyPtr->GetFName().ToString());
			}
		}
	}
	
	return InChannelIndex;
}

void UMovieSceneLiveLinkSubSectionProperties::CreatePropertiesChannel(UScriptStruct* InScriptStruct)
{
	for (TFieldIterator<UProperty> It(InScriptStruct, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::ExcludeInterfaces); It; ++It)
	{
		const bool bIsInterpField = It->HasAllPropertyFlags(CPF_Interp);
		if (!bIsInterpField)
		{
			continue;
		}

		if (!IsPropertyTypeSupported(*It))
		{
			continue;
		}

		FLiveLinkPropertyData& NewProperty = SubSectionData.Properties.AddDefaulted_GetRef();
		NewProperty.PropertyName = It->GetFName();
	}

	//Create the handler in a second pass so its reference to the property data is valid
	const UScriptStruct* Container = SubjectRole.GetDefaultObject()->GetFrameDataStruct();
	for (FLiveLinkPropertyData& Data : SubSectionData.Properties)
	{
		const int32 ElementCount = 1;
		TSharedPtr<IMovieSceneLiveLinkPropertyHandler> PropertyHandler = LiveLinkPropertiesUtils::CreatePropertyHandler(*Container, &Data);
		PropertyHandler->CreateChannels(*Container, ElementCount);
		PropertyHandlers.Add(PropertyHandler);
	}
}

void UMovieSceneLiveLinkSubSectionProperties::RecordFrame(FFrameNumber InFrameNumber, const FLiveLinkFrameDataStruct& InFrameData)
{
	for (TSharedPtr<IMovieSceneLiveLinkPropertyHandler>& PropertyHandler : PropertyHandlers)
	{
		PropertyHandler->RecordFrame(InFrameNumber, *SubjectRole.GetDefaultObject()->GetFrameDataStruct(), InFrameData.GetBaseData());
	}
}

void UMovieSceneLiveLinkSubSectionProperties::FinalizeSection(bool bInReduceKeys, const FKeyDataOptimizationParams& InOptimizationParams)
{
	for (TSharedPtr<IMovieSceneLiveLinkPropertyHandler>& PropertyHandler : PropertyHandlers)
	{
		PropertyHandler->Finalize(bInReduceKeys, InOptimizationParams);
	}
}

bool UMovieSceneLiveLinkSubSectionProperties::IsPropertyTypeSupported(const UProperty* InProperty) const
{
	if (InProperty == nullptr)
	{
		return false;
	}

	//Arrays are not supported because we can't know the number of elements to create for it. If arrays are desired, a sub section will have to manage it 
	//See Animation subsection with transforms
	if (InProperty->IsA<UFloatProperty>()
		|| InProperty->IsA<UIntProperty>()
		|| InProperty->IsA<UStrProperty>()
		|| InProperty->IsA<UByteProperty>()
		|| InProperty->IsA<UBoolProperty>()
		|| InProperty->IsA<UEnumProperty>())
	{
		return true;
	}
	else if (const UStructProperty* StructProperty = Cast<UStructProperty>(InProperty))
	{
		if (StructProperty->Struct->GetFName() == NAME_Transform
			|| StructProperty->Struct->GetFName() == NAME_Vector
			|| StructProperty->Struct->GetFName() == NAME_Color)
		{
			return true;
		}
	}

	return false;
}

bool UMovieSceneLiveLinkSubSectionProperties::IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) const
{
	return true;
}

#undef LOCTEXT_NAMESPACE // MovieSceneLiveLinkSubSectionProperties
