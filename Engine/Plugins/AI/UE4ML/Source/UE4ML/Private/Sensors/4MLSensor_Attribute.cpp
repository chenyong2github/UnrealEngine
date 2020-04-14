// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sensors/4MLSensor_Attribute.h"
#include "AttributeSet.h"


U4MLSensor_Attribute::U4MLSensor_Attribute(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TickPolicy = E4MLTickPolicy::EveryTick;
}

bool U4MLSensor_Attribute::ConfigureForAgent(U4MLAgent& Agent)
{
	return false;
}

void U4MLSensor_Attribute::Configure(const TMap<FName, FString>& Params)
{
	const FName NAME_Attributes = TEXT("attributes");
	
	Super::Configure(Params);

	for (auto KeyValue : Params)
	{
		if (KeyValue.Key == NAME_Attributes)
		{
			TArray<FString> Tokens;
			KeyValue.Value.ParseIntoArrayWS(Tokens, TEXT(","));
			SetAttributes(Tokens);
		}
	}
}

void U4MLSensor_Attribute::SenseImpl(const float DeltaTime)
{
	Values.Reset(AttributeNames.Num());
	if (AttributeSet == nullptr)
	{	
		Values.AddZeroed(AttributeNames.Num());
		return;
	}
	
	int Index = 0;
	for (const FGameplayAttributeData* Attribute : Attributes)
	{
		Values.Add(Attribute ? Attribute->GetCurrentValue() :0.f);
	}
}

void U4MLSensor_Attribute::OnAvatarSet(AActor* Avatar)
{
	Super::OnAvatarSet(Avatar);

	if (Avatar)
	{
		BindAttributes(*Avatar);
	}
	else
	{
		AttributeSet = nullptr;
		Attributes.Reset(AttributeNames.Num());
	}
}

void U4MLSensor_Attribute::GetObservations(F4MLMemoryWriter& Ar)
{
	FScopeLock Lock(&ObservationCS);
	
	F4ML::FSpaceSerializeGuard SerializeGuard(SpaceDef, Ar);
	Ar.Serialize(Values.GetData(), Values.Num() * sizeof(float));
}

TSharedPtr<F4ML::FSpace> U4MLSensor_Attribute::ConstructSpaceDef() const
{
	return MakeShareable(new F4ML::FSpace_Box({ uint32(AttributeNames.Num()) }));
}

void U4MLSensor_Attribute::UpdateSpaceDef()
{
	Super::UpdateSpaceDef();

	Values.Reset(AttributeNames.Num());
	Values.AddZeroed(AttributeNames.Num());
}

void U4MLSensor_Attribute::SetAttributes(TArray<FString>& InAttributeNames)
{
	AttributeNames.Reset(InAttributeNames.Num());
	for (const FString& StringName : InAttributeNames)
	{
		AttributeNames.Add(FName(StringName));
	}
	
	AActor* Avatar = GetAvatar();
	if (Avatar)
	{
		BindAttributes(*Avatar);
	}

	UpdateSpaceDef();
}

void U4MLSensor_Attribute::BindAttributes(AActor& Actor)
{
	TArray<UAttributeSet*> AttributeSetsFound;

	// 1. find UAttributeSet-typed property in Actor
	// 2. parse through found property instance looking for Attributes
	for (TFieldIterator<FObjectProperty> It(Actor.GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FObjectProperty* Prop = *It;
		if (Prop->PropertyClass->IsChildOf(UAttributeSet::StaticClass()))
		{
			AttributeSetsFound.Add(Cast<UAttributeSet>(Prop->GetObjectPropertyValue_InContainer(&Actor)));
		}
	}

	Attributes.Reset(AttributeNames.Num());
	if (AttributeSetsFound.Num() > 0)
	{
		ensureMsgf(AttributeSetsFound.Num() == 1, TEXT("Found %s attribute sets, using the first one"), AttributeSetsFound.Num());
		AttributeSet = AttributeSetsFound[0];
		UClass* AttributeSetClass = AttributeSet->GetClass();
		for (const FName& Name : AttributeNames)
		{
			FStructProperty* Prop = FindFProperty<FStructProperty>(AttributeSetClass, Name);
			if (Prop)
			{
				Attributes.Add(Prop->ContainerPtrToValuePtr<FGameplayAttributeData>(AttributeSet));
			}
			else
			{
				Attributes.Add(nullptr);
			}
		}
	}
	else
	{
		// not found
		// @todo log
		AttributeSet = nullptr;
	}
}
