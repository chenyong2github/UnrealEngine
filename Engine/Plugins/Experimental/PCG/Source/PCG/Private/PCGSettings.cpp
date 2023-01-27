// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSettings.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGCustomVersion.h"
#include "PCGGraph.h"
#include "PCGHelpers.h"
#include "PCGModule.h"
#include "PCGPin.h"
#include "PCGSubsystem.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"

#include "Serialization/ArchiveObjectCrc32.h"
#include "UObject/ObjectSaveContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSettings)

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "PCGSettings"

/** Custom Crc computation that ignores properties that will not affect the computed result of a node. */
class FPCGSettingsObjectCrc32 : public FArchiveObjectCrc32
{
public:
#if WITH_EDITOR
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
		const bool bSkip = InProperty && (
			InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, DebugSettings)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, DeterminismSettings)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, bDebug)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, Category)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, Description)
			|| InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, bExposeToLibrary)
			);
		return bSkip;
	}
#endif // WITH_EDITOR
};

bool UPCGSettingsInterface::IsInstance() const
{
	return this != GetSettings();
}

void UPCGSettingsInterface::SetEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
#if WITH_EDITOR
		if (UPCGSettings* Settings = GetSettings())
		{
			const bool bIsStructuralChange = Settings->IsStructuralProperty(GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bEnabled));
			OnSettingsChangedDelegate.Broadcast(Settings, (bIsStructuralChange ? EPCGChangeType::Structural : EPCGChangeType::None) | EPCGChangeType::Settings);
		}
#endif
	}
}

bool UPCGSettings::operator==(const UPCGSettings& Other) const
{
	if (this == &Other)
	{
		return true;
	}
	else
	{
		FPCGSettingsObjectCrc32 Ar;
		uint32 ThisCrc = Ar.Crc32(const_cast<UPCGSettings*>(this));
		uint32 OtherCrc = Ar.Crc32(const_cast<UPCGSettings*>(&Other));
		return ThisCrc == OtherCrc;
	}
}

uint32 UPCGSettings::GetCrc32() const
{
	FPCGSettingsObjectCrc32 Ar;
	return Ar.Crc32(const_cast<UPCGSettings*>(this));
}

#if WITH_EDITOR
void UPCGSettings::ApplyDeprecation(UPCGNode* InOutNode)
{
	DataVersion = FPCGCustomVersion::LatestVersion;
}

void UPCGSettings::PostEditUndo()
{
	// CachedOverridableParams was reset to previous value
	// Therefore we need to rebuild the properties array since it is transient.
	InitializeCachedOverridableParams();

	Super::PostEditUndo();
}
#endif // WITH_EDITOR

void UPCGSettings::PostLoad()
{
	Super::PostLoad();

	InitializeCachedOverridableParams();

#if WITH_EDITOR
	if (ExecutionMode_DEPRECATED != EPCGSettingsExecutionMode::Enabled)
	{
		bEnabled = ExecutionMode_DEPRECATED != EPCGSettingsExecutionMode::Disabled;
		bDebug = ExecutionMode_DEPRECATED == EPCGSettingsExecutionMode::Debug;
		ExecutionMode_DEPRECATED = EPCGSettingsExecutionMode::Enabled;
	}
#endif
}

void UPCGSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	InitializeCachedOverridableParams();
#endif //WITH_EDITOR
}

void UPCGSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FPCGCustomVersion::GUID);

#if WITH_EDITOR
	if (Ar.IsLoading())
	{
		// Some data migration must happen after the graph is fully initialized, such as manipulating node connections, so we
		// store off the loaded version number to be used later.
		DataVersion = Ar.CustomVer(FPCGCustomVersion::GUID);
	}
#endif // WITH_EDITOR

	// An additional custom version number that can be driven by external system users to track system modifications. To use a custom
	// version in user settings objects, override the GetUserCustomVersionGuid() method.
	const FGuid UserDataGuid = GetUserCustomVersionGuid();
	const bool bUsingCustomUserVersion = UserDataGuid != FGuid();

	if (bUsingCustomUserVersion)
	{
		Ar.UsingCustomVersion(UserDataGuid);

#if WITH_EDITOR
		if (Ar.IsLoading())
		{
			// Some data migration must happen after the graph is fully initialized, such as manipulating node connections, so we
			// store off the loaded version number to be used later.
			UserDataVersion = Ar.CustomVer(UserDataGuid);
		}
#endif // WITH_EDITOR
	}
}

void UPCGSettings::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);

#if WITH_EDITOR
	// This will get called when an external settings gets saved;
	// This is to trigger generation on save, if we've called changed properties from a blueprint
	OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Structural);
#endif
}

#if WITH_EDITOR
UObject* UPCGSettings::GetJumpTargetForDoubleClick() const
{
	return const_cast<UObject*>(Cast<UObject>(this));
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	// This is not true for everything, use a virtual call?
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Spatial);

	return PinProperties;
}

void UPCGSettings::FillOverridableParamsPins(TArray<FPCGPinProperties>& OutPins) const
{
	if (!HasOverridableParams())
	{
		return;
	}

	// Validating that we are not clashing with existing pins
	TMap<FName, EPCGDataType> InputPinsLabelsAndTypes;

	for (const FPCGPinProperties& InputPinProperties : OutPins)
	{
		InputPinsLabelsAndTypes.Emplace(InputPinProperties.Label, InputPinProperties.AllowedTypes);
	}

	// For debugging
	FString GraphName;
	FString NodeName;

	if (const UPCGNode* Node = Cast<UPCGNode>(GetOuter()))
	{
		NodeName = ((Node->NodeTitle != NAME_None) ? Node->NodeTitle : Node->GetFName()).ToString();

		if (Node->GetGraph())
		{
			GraphName = Node->GetGraph()->GetName();
		}
	}
	else
	{
		NodeName = GetName();
	}

	// Adding the multi-pin connection for params.
	// If it already exists (and is the correct type), we can keep it.
	const EPCGDataType* PinType = InputPinsLabelsAndTypes.Find(PCGPinConstants::DefaultParamsLabel);
	if (PinType && (*PinType != EPCGDataType::Param))
	{
		const FString ParamsName = PCGPinConstants::DefaultParamsLabel.ToString();
		UE_LOG(LogPCG, Error, TEXT("[%s-%s] While adding %s pin, we found another %s pin with not the same allowed type (Param). "
			"Please rename this pin if you want to take advantage of automatic override. Until then it will probably break your graph."), *GraphName, *NodeName, *ParamsName, *ParamsName);
	}
	else
	{
		FPCGPinProperties& ParamPin = OutPins.Emplace_GetRef(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections=*/ true, /*bAllowMultipleData=*/ true);
		ParamPin.bAdvancedPin = false;

#if WITH_EDITOR
		ParamPin.Tooltip = LOCTEXT("GlobalParamPinTooltip", "Can bundle multiple param data to override multiple parameters at the same time. Names need to match perfectly.");
#endif // WITH_EDITOR
	}

	InputPinsLabelsAndTypes.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param);

	for (const FPCGSettingsOverridableParam& OverridableParam : OverridableParams())
	{
		if (InputPinsLabelsAndTypes.Contains(OverridableParam.Label))
		{
			const FString ParamsName = OverridableParam.Label.ToString();
			UE_LOG(LogPCG, Warning, TEXT("[%s-%s] While automatically adding overriable param pins, we found a %s pin. "
				"Please rename this pin if you want to take advantage of automatic override. Until then, we will not add a %s pin."), *GraphName, *NodeName, *ParamsName, *ParamsName);
			continue;
		}

		InputPinsLabelsAndTypes.Emplace(OverridableParam.Label, EPCGDataType::Param);

		FPCGPinProperties& ParamPin = OutPins.Emplace_GetRef(OverridableParam.Label, EPCGDataType::Param, /*bInAllowMultipleConnections=*/ false, /*bAllowMultipleData=*/ false);
		ParamPin.bAdvancedPin = true;
#if WITH_EDITOR
		const FProperty* Property = OverridableParam.Properties.Last();
		check(Property);
		static FName TooltipMetadata("Tooltip");
		FString Tooltip;
		if (const FString* TooltipPtr = Property->FindMetaData(TooltipMetadata))
		{
			Tooltip = *TooltipPtr + TEXT("\n");
		}

		ParamPin.Tooltip = FText::Format(LOCTEXT("OverridableParamPinTooltip", "{0}Param type is \"{1}\" and its exact name is \"{2}\""),
			FText::FromString(Tooltip),
			FText::FromString(Property->GetCPPType()),
			FText::FromName(Property->GetFName()));
#endif // WITH_EDITOR
	}
}

TArray<FPCGPinProperties> UPCGSettings::AllInputPinProperties() const
{
	TArray<FPCGPinProperties> InputPins = InputPinProperties();
	FillOverridableParamsPins(InputPins);
	return InputPins;
}

TArray<FPCGPinProperties> UPCGSettings::AllOutputPinProperties() const
{
	return OutputPinProperties();
}

TArray<FPCGPinProperties> UPCGSettings::DefaultInputPinProperties() const
{
	return InputPinProperties();
}

TArray<FPCGPinProperties> UPCGSettings::DefaultOutputPinProperties() const
{
	return OutputPinProperties();
}

FPCGElementPtr UPCGSettings::GetElement() const
{
	if (!CachedElement)
	{
		CacheLock.Lock();

		if (!CachedElement)
		{
			CachedElement = CreateElement();
		}

		CacheLock.Unlock();
	}

	return CachedElement;
}

UPCGNode* UPCGSettings::CreateNode() const
{
	return NewObject<UPCGNode>();
}

int UPCGSettings::GetSeed(const UPCGComponent* InSourceComponent) const
{
	return !bUseSeed ? 42 : (InSourceComponent ? PCGHelpers::ComputeSeed(Seed, InSourceComponent->Seed) : Seed);
}

#if WITH_EDITOR
void UPCGSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, CachedOverridableParams)))
	{
		// Need to rebuild properties, if it ever changes.
		InitializeCachedOverridableParams();
	}

	if (PropertyChangedEvent.GetPropertyName() != GET_MEMBER_NAME_CHECKED(UPCGSettings, DeterminismSettings))
	{
		OnSettingsChangedDelegate.Broadcast(this, IsStructuralProperty(PropertyChangedEvent.GetPropertyName()) ? EPCGChangeType::Structural : EPCGChangeType::Settings);
	}
}

void UPCGSettings::DirtyCache()
{
	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		UPCGSubsystem* PCGSubsystem = World ? World->GetSubsystem<UPCGSubsystem>() : nullptr;
		if (PCGSubsystem)
		{
			PCGSubsystem->CleanFromCache(GetElement().Get(), this);
		}
	}
}

bool UPCGSettings::CanEditChange(const FProperty* InProperty) const
{
	if (!InProperty || !Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (!InProperty->HasMetaData(PCGObjectMetadata::Overridable))
	{
		return true;
	}

	if (const UPCGNode* Node = Cast<UPCGNode>(GetOuter()))
	{
		const FPCGSettingsOverridableParam* Param = OverridableParams().FindByPredicate([InProperty](const FPCGSettingsOverridableParam& ParamToCheck)
		{
			// In OverridableParam, the array of properties is the chain of properties from the Settings class to the wanted param.
			// Therefore the property we are editing would match the latest property of the array.
			return ParamToCheck.Properties.Last() == InProperty;
		});

		if (Param)
		{
			if (const UPCGPin* Pin = Node->GetInputPin(Param->Label))
			{
				return !Pin->IsConnected();
			}
		}
	}

	return true;
}

void UPCGSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	check(InOutNode);

	if (DataVersion < FPCGCustomVersion::AddParamPinToOverridableNodes)
	{
		PCGSettingsHelpers::DeprecationBreakOutParamsToNewPin(InOutNode, InputPins, OutputPins);
	}
}
#endif // WITH_EDITOR

namespace PCGSettings
{
#if WITH_EDITOR
	template <typename ClassType>
	TArray<FPCGSettingsOverridableParam> GetAllParams(const ClassType* Class, bool bCheckMetadata, bool bUseSeed)
	{
		// TODO: Was not a concern until now, and we didn't have a solution, but this function
		// only worked if we don't have names clashes in overriable parameters.
		// The previous override solution was flatenning structs, and only override use the struct member name,
		// not prefixed by the struct name or anything else.
		// We cannot prefix it now, because it will break existing node that were assuming the flatenning.
		// We'll keep this behavior for now, as it might be solved by passing structs instead of param data,
		// but we'll still at least raise a warning if there is a clash.
		TSet<FName> LabelCache;

		TArray<FPCGSettingsOverridableParam> Res;

		for (TFieldIterator<FProperty> InputIt(Class, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated); InputIt; ++InputIt)
		{
			const FProperty* Property = *InputIt;
			if (Property && (!bCheckMetadata || Property->HasMetaData(PCGObjectMetadata::Overridable)))
			{
				// Don't allow to override the seed if the settings doesn't use the seed.
				if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGSettings, Seed) && !bUseSeed)
				{
					continue;
				}

				// Validating that the property can be overriden by params
				if (PCGAttributeAccessorHelpers::IsPropertyAccessorSupported(Property))
				{
					FName Label = *Property->GetDisplayNameText().ToString();
					if (LabelCache.Contains(Label))
					{
						UE_LOG(LogPCG, Warning, TEXT("%s property clashes with another property already found. It is a limitation at the moment and this property will be ignored (ie. will not be overridable)"), *Label.ToString());
						continue;
					}

					LabelCache.Add(Label);

					FPCGSettingsOverridableParam& Param = Res.Emplace_GetRef();
					Param.Label = Label;
					Param.PropertiesNames.Add(Property->GetFName());
				}
				else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					FString PropertyName = Property->GetDisplayNameText().ToString();
					for (const FPCGSettingsOverridableParam& ChildParam : GetAllParams(StructProperty->Struct, /*bCheckMetadata=*/false, /*bUseSeed=*/true))
					{
						FName Label = ChildParam.Label;
						if (LabelCache.Contains(Label))
						{
							UE_LOG(LogPCG, Warning, TEXT("%s property clashes with another property already found. It is a limitation at the moment and this property will be ignored (ie. will not be overridable)"), *Label.ToString());
							continue;
						}

						LabelCache.Add(Label);

						FPCGSettingsOverridableParam& Param = Res.Emplace_GetRef();
						Param.Label = Label;
						Param.PropertiesNames.Add(Property->GetFName());
						Param.PropertiesNames.Append(ChildParam.PropertiesNames);
					}
				}
			}
		}

		return Res;
	}
#endif // WITH_EDITOR
}

void UPCGSettings::InitializeCachedOverridableParams()
{
	// Don't do it for default object
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGSettings::InitializeCachedOverridableParams);

#if WITH_EDITOR
	if (CachedOverridableParams.IsEmpty())
	{
		CachedOverridableParams = PCGSettings::GetAllParams(GetClass(), true, bUseSeed);
	}
#endif // WITH_EDITOR

	for (FPCGSettingsOverridableParam& Param : CachedOverridableParams)
	{
		check(!Param.PropertiesNames.IsEmpty());
		Param.Properties.Reset(Param.PropertiesNames.Num());

		// Some properties might not be available at runtime. Ignore them.
		if (const FProperty* Property = GetClass()->FindPropertyByName(Param.PropertiesNames[0]))
		{
			Param.Properties.Add(Property);

			for (int32 i = 1; i < Param.PropertiesNames.Num(); ++i)
			{
				// If we have multiple depth properties, it should be Struct properties by construction
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				if (ensure(StructProperty))
				{
					Property = StructProperty->Struct->FindPropertyByName(Param.PropertiesNames[i]);
					check(Property);
					Param.Properties.Add(Property);
				}
			}
		}
	}
}

TArray<FPCGPinProperties> UPCGSettings::DefaultPointOutputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return Properties;
}

void UPCGSettingsInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.AddUObject(this, &UPCGSettingsInstance::OnSettingsChanged);
		Settings->ConditionalPostLoad();
	}
#endif

#if WITH_EDITOR
	OriginalSettings = Settings;
#endif
}

void UPCGSettingsInstance::BeginDestroy()
{
#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

void UPCGSettingsInstance::SetSettings(UPCGSettings* InSettings)
{
#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.RemoveAll(this);
	}
#endif

	Settings = InSettings;
#if WITH_EDITOR
	OriginalSettings = Settings;
#endif

#if WITH_EDITOR
	if (Settings)
	{
		Settings->OnSettingsChangedDelegate.AddUObject(this, &UPCGSettingsInstance::OnSettingsChanged);
	}
#endif
}

#if WITH_EDITOR
void UPCGSettingsInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Some setting in the instance has changed. We don't have a flag for that yet (to add if needed)
	// However, we can make it behave like a standard change
	OnSettingsChangedDelegate.Broadcast(Settings, EPCGChangeType::Settings);
}

void UPCGSettingsInstance::OnSettingsChanged(UPCGSettings* InSettings, EPCGChangeType ChangeType)
{
	if (InSettings == Settings)
	{
		OnSettingsChangedDelegate.Broadcast(InSettings, ChangeType);
	}
}
#endif

FPCGElementPtr UPCGTrivialSettings::CreateElement() const
{
	return MakeShared<FPCGTrivialElement>();
}

bool FPCGTrivialElement::ExecuteInternal(FPCGContext* Context) const
{
	// Pass-through
	Context->OutputData = Context->InputData;
	return true;
}

#undef LOCTEXT_NAMESPACE