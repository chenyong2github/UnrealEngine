// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXImport.h"
#include "DMXImportGDTF.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EDMXImportGDTFType : uint8
{
    Multiply,
    Override
};


UENUM(BlueprintType)
enum class EDMXImportGDTFSnap : uint8
{
    Yes,
    No,
    On,
    Off
};

UENUM(BlueprintType)
enum class EDMXImportGDTFMaster : uint8
{
    None,
    Grand,
    Group
};

UENUM(BlueprintType)
enum class EDMXImportGDTFDMXInvert : uint8
{
    Yes,
    No
};

UENUM(BlueprintType)
enum class EDMXImportGDTFLampType : uint8
{
    Discharge,
    Tungsten,
    Halogen,
    LED
};

UENUM(BlueprintType)
enum class EDMXImportGDTFBeamType : uint8
{
    Wash,
    Spot,
    None
};

UENUM(BlueprintType)
enum class EDMXImportGDTFPrimitiveType : uint8
{
    Undefined,
    Cube,
    Cylinder,
    Sphere,
    Base,
    Yoke,
    Head,
    Scanner,
    Conventional,
    Pigtail
};

UENUM(BlueprintType)
enum class EDMXImportGDTFPhysicalUnit : uint8
{
    None,
    Percent,
    Length,
    Mass,
    Time,
    Temperature,
    LuminousIntensity,
    Angle,
    Force,
    Frequency,
    Current,
    Voltage,
    Power,
    Energy,
    Area,
    Volume,
    Speed,
    Acceleration,
    AngularSpeed,
    AngularAccc,
    WaveLength,
    ColorComponent
};

UENUM(BlueprintType)
enum class EDMXImportGDTFMode : uint8
{
    Custom,
    sRGB,
    ProPhoto,
    ANSI
};

UENUM(BlueprintType)
enum class EDMXImportGDTFInterpolationTo : uint8
{
    Linear,
    Step,
    Log
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXImportGDTFActivationGroup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXImportGDTFFeature
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXImportGDTFFeatureGroup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Pretty;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFFeature> Features;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXImportGDTFAttribute
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Pretty;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFActivationGroup ActivationGroup;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFeature Feature;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString MainAttribute;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFPhysicalUnit PhysicalUnit = EDMXImportGDTFPhysicalUnit::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Color;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFFilter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Color;
};


USTRUCT(BlueprintType)
struct FDMXImportGDTFWheelSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Color;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFilter Filter;

    UPROPERTY(VisibleAnywhere, Category = "Fixture Type")
    UTexture2D* MediaFileName = nullptr;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFWheel
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFWheelSlot> Slots;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFMeasurementPoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float WaveLength = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Energy = 0.f;
};


USTRUCT(BlueprintType)
struct FDMXImportGDTFMeasurement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Physical = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float LuminousIntensity = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Transmission = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFInterpolationTo InterpolationTo = EDMXImportGDTFInterpolationTo::Linear;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFMeasurementPoint> MeasurementPoints;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFEmitter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Color;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float DominantWaveLength = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString DiodePart;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFMeasurement Measurement;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFColorSpace
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFMode Mode = EDMXImportGDTFMode::sRGB;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Red;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Green;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE Blue;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXColorCIE WhitePoint;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFDMXProfiles
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFCRIs
{
    GENERATED_BODY()
};


USTRUCT(BlueprintType)
struct FDMXImportGDTFModel
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Length = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Width = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Height = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFPrimitiveType PrimitiveType = EDMXImportGDTFPrimitiveType::Undefined;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Model;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FMatrix Position;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFBeam
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFLampType LampType = EDMXImportGDTFLampType::Discharge;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PowerConsumption = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float LuminousFlux = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ColorTemperature = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float BeamAngle = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float FieldAngle = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float BeamRadius = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFBeamType BeamType = EDMXImportGDTFBeamType::Wash;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    uint8 ColorRenderingIndex = 0;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFTypeAxis
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFBeam> Beams;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFGeneralAxis
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFTypeAxis> Axis;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFTypeGeometry
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFFilterBeam
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFFilterColor
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFFilterGobo
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFFilterShaper
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFBreak
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 DMXOffset = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    uint8 DMXBreak = 0;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFGeometryReference
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFBreak> Breaks;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFGeneralGeometry
    : public FDMXImportGDTFGeometryBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFGeneralAxis Axis;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFTypeGeometry Geometry;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFilterBeam FilterBeam;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFilterColor FilterColor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFilterGobo FilterGobo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFilterShaper FilterShaper;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFGeometryReference GeometryReference;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXImportGDTFDMXValue
{
    GENERATED_BODY()

    FDMXImportGDTFDMXValue()
        : Value(0)
        , ValueSize(1)
    {
    }

    FDMXImportGDTFDMXValue(const FString& InDMXValueStr);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    int32 Value;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    uint8 ValueSize;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFChannelSet
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFDMXValue DMXFrom;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PhysicalFrom = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PhysicalTo = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 WheelSlotIndex = 0;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFChannelFunction
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFAttribute Attribute;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString OriginalAttribute;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFDMXValue DMXFrom;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFDMXValue DMXValue;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PhysicalFrom = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float PhysicalTo = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float RealFade = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFWheel Wheel;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFEmitter Emitter;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFFilter Filter;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFDMXInvert DMXInvert = EDMXImportGDTFDMXInvert::No;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString ModeMaster;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFDMXValue ModeFrom;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFDMXValue ModeTo;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFChannelSet> ChannelSets;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFLogicalChannel
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFAttribute Attribute;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFSnap Snap = EDMXImportGDTFSnap::No;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    EDMXImportGDTFMaster Master = EDMXImportGDTFMaster::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MibFade = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float DMXChangeTimeLimit = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFChannelFunction ChannelFunction;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXImportGDTFDMXChannel
{
    GENERATED_BODY()

    void ParseOffset(const FString& InOffsetStr);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    int32 DMXBreak = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    TArray<int32> Offset;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FDMXImportGDTFDMXValue Default;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FDMXImportGDTFDMXValue Highlight;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FName Geometry;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FDMXImportGDTFLogicalChannel LogicalChannel;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFRelation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FString Master;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FString Follower;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    EDMXImportGDTFType Type = EDMXImportGDTFType::Multiply;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFFTMacro
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FName Name;
};

USTRUCT(BlueprintType)
struct FDMXImportGDTFDMXMode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    FName Geometry;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    TArray<FDMXImportGDTFDMXChannel> DMXChannels;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    TArray<FDMXImportGDTFRelation> Relations;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, BlueprintReadOnly)
    TArray<FDMXImportGDTFFTMacro> FTMacros;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFFixtureType
    : public UDMXImportFixtureType
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FName Name;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FString ShortName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FString LongName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FString Manufacturer;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FString FixtureTypeID;

    UPROPERTY(VisibleAnywhere, Category = "Fixture Type")
    UTexture2D* Thumbnail = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type")
    FString RefFT;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFAttributeDefinitions
    : public UDMXImportAttributeDefinitions
{
    GENERATED_BODY()

public:
    bool FindFeature(const FString& InQuery, FDMXImportGDTFFeature& OutFeature) const;

    bool FindAtributeByName(const FName& InName, FDMXImportGDTFAttribute& OutAttribute) const;

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFActivationGroup> ActivationGroups;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFFeatureGroup> FeatureGroups;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFAttribute> Attributes;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFWheels
    : public UDMXImportWheels
{
    GENERATED_BODY()

public:
    bool FindWeelByName(const FName& InName, FDMXImportGDTFWheel& OutWheel) const;

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFWheel> Wheels;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFPhysicalDescriptions
    : public UDMXImportPhysicalDescriptions
{
    GENERATED_BODY()

public:
    bool FindEmitterByName(const FName& InName, FDMXImportGDTFEmitter& OutEmitter) const;

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFEmitter> Emitters;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFColorSpace ColorSpace;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFDMXProfiles DMXProfiles;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FDMXImportGDTFCRIs CRIs;
};


UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFModels
    : public UDMXImportModels
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFModel> Models;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFGeometries
    : public UDMXImportGeometries
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFGeneralGeometry> GeneralGeometry;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFDMXModes
    : public UDMXImportDMXModes
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FDMXImportGDTFDMXMode> DMXModes;

public:
	UFUNCTION(BlueprintPure, Category = "DMXGDTF|Import Data")
	TArray<FDMXImportGDTFChannelFunction> GetDMXChannelFunctions(const FDMXImportGDTFDMXMode& InMode);
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTFProtocols
    : public UDMXImportProtocols
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FName> Protocols;
};

UCLASS(BlueprintType, Blueprintable)
class DMXRUNTIME_API UDMXImportGDTF
: public UDMXImport
{
    GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "DMXGDTF|Import Data")
	UDMXImportGDTFDMXModes* GetDMXModes()
	{
		return Cast<UDMXImportGDTFDMXModes>(DMXModes);
	};

public:
	/** The filename of the dmx we were created from. This may not always exist on disk, as we may have previously loaded and cached the font data inside this asset. */
	UPROPERTY()
	FString SourceFilename;
};