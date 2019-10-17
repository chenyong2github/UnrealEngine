// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSceneElementsImpl.h"

#include "DatasmithUtils.h"
#include "DatasmithSceneFactory.h"
#include "IDatasmithSceneElements.h"

bool IDatasmithShaderElement::bUseRealisticFresnel = true;
bool IDatasmithShaderElement::bDisableReflectionFresnel = false;

FDatasmithMeshElementImpl::FDatasmithMeshElementImpl(const TCHAR* InName)
	: FDatasmithElementImpl(InName, EDatasmithElementType::StaticMesh)
	, Area(0.f)
	, Width(0.f)
	, Height(0.f)
	, Depth(0.f)
	, LODCount(1)
	, LightmapCoordinateIndex(-1)
	, LightmapSourceUV(-1)
{
}

FMD5Hash FDatasmithMeshElementImpl::CalculateElementHash(bool bForce)
{
	if (ElementHash.IsValid() && !bForce)
	{
		return ElementHash;
	}
	FMD5 MD5;
	MD5.Update(FileHash.GetBytes(), FileHash.GetSize());
	MD5.Update(reinterpret_cast<const uint8*>(&LightmapSourceUV), sizeof(LightmapSourceUV));
	MD5.Update(reinterpret_cast<const uint8*>(&LightmapCoordinateIndex), sizeof(LightmapCoordinateIndex));

	int32 Id = 0;
	for (const TSharedPtr<IDatasmithMaterialIDElement>& MatID : MaterialSlots)
	{
		Id = MatID->GetId();
		MD5.Update(reinterpret_cast<const uint8*>(&Id), sizeof(Id));
		MD5.Update(reinterpret_cast<const uint8*>(MatID->GetName()), TCString<TCHAR>::Strlen(MatID->GetName()) * sizeof(TCHAR));
	}
	ElementHash.Set(MD5);
	return ElementHash;
}

void FDatasmithMeshElementImpl::SetMaterial(const TCHAR* MaterialPathName, int32 SlotId)
{
	for (const TSharedPtr<IDatasmithMaterialIDElement>& Slot : MaterialSlots)
	{
		if (Slot->GetId() == SlotId)
		{
			Slot->SetName(MaterialPathName);
			return;
		}
	}
	TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialPathName);
	MaterialIDElement->SetId(SlotId);
	MaterialSlots.Add(MoveTemp(MaterialIDElement));
}

const TCHAR* FDatasmithMeshElementImpl::GetMaterial(int32 SlotId) const
{
	for (const TSharedPtr<IDatasmithMaterialIDElement>& Slot : MaterialSlots)
	{
		if (Slot->GetId() == SlotId)
		{
			return Slot->GetName();
		}
	}
	return nullptr;
}

int32 FDatasmithMeshElementImpl::GetMaterialSlotCount() const
{
	return MaterialSlots.Num();
}


TSharedPtr<const IDatasmithMaterialIDElement> FDatasmithMeshElementImpl::GetMaterialSlotAt(int32 Index) const
{
	if (MaterialSlots.IsValidIndex(Index))
	{
		return MaterialSlots[Index];
	}
	const TSharedPtr<IDatasmithMaterialIDElement> InvalidMaterialID;
	return InvalidMaterialID;
}

TSharedPtr<IDatasmithMaterialIDElement> FDatasmithMeshElementImpl::GetMaterialSlotAt(int32 Index)
{
	if (MaterialSlots.IsValidIndex(Index))
	{
		return MaterialSlots[Index];
	}
	const TSharedPtr<IDatasmithMaterialIDElement> InvalidMaterialID;
	return InvalidMaterialID;
}

static TSharedPtr< IDatasmithKeyValueProperty > NullPropertyPtr;

FDatasmithKeyValuePropertyImpl::FDatasmithKeyValuePropertyImpl(const TCHAR* InName)
	: FDatasmithElementImpl(InName, EDatasmithElementType::KeyValueProperty)
	, PropertyType(EDatasmithKeyValuePropertyType::String)
{
}

void FDatasmithKeyValuePropertyImpl::SetPropertyType( EDatasmithKeyValuePropertyType InType )
{
	PropertyType = InType;
	FormatValue();
}

void FDatasmithKeyValuePropertyImpl::SetValue(const TCHAR* InValue)
{
	Value = InValue;
	FormatValue();
}

void FDatasmithKeyValuePropertyImpl::FormatValue()
{
	if ( Value.Len() > 0 && (
		GetPropertyType() == EDatasmithKeyValuePropertyType::Vector ||
		GetPropertyType() == EDatasmithKeyValuePropertyType::Color ) )
	{
		if ( Value[0] != TEXT('(') )
		{
			Value.InsertAt( 0, TEXT("(") );
		}

		if ( Value[ Value.Len() - 1 ] != TEXT(')') )
		{
			Value += TEXT(")");
		}

		Value.ReplaceInline( TEXT(" "), TEXT(",") ); // FVector::ToString separates the arguments with a " " rather than with a ","
	}
}

FDatasmithMaterialIDElementImpl::FDatasmithMaterialIDElementImpl(const TCHAR* InName)
	: FDatasmithElementImpl( InName, EDatasmithElementType::MaterialId )
	, Id( 0 )
{
}

FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::FDatasmithHierarchicalInstancedStaticMeshActorElementImpl(const TCHAR* InName)
	: FDatasmithMeshActorElementImpl< IDatasmithHierarchicalInstancedStaticMeshActorElement >(InName, EDatasmithElementType::HierarchicalInstanceStaticMesh)
{
}

FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::~FDatasmithHierarchicalInstancedStaticMeshActorElementImpl()
{
}

int32 FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::GetInstancesCount() const
{
	return Instances.Num();
}

void FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::ReserveSpaceForInstances(int32 NumIntances)
{
	Instances.Reserve(NumIntances);
}

int32 FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::AddInstance(const FTransform& Transform)
{
	Instances.Add(Transform);
	return Instances.Num() - 1;
}

FTransform FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::GetInstance(int32 InstanceIndex) const
{
	if (Instances.IsValidIndex(InstanceIndex))
	{
		return Instances[InstanceIndex];
	}
	return FTransform();
}

void FDatasmithHierarchicalInstancedStaticMeshActorElementImpl::RemoveInstance(int32 InstanceIndex)
{
	if (Instances.IsValidIndex(InstanceIndex))
	{
		Instances.RemoveAtSwap(InstanceIndex);
	}
}

FDatasmithPostProcessElementImpl::FDatasmithPostProcessElementImpl()
	: FDatasmithElementImpl( TEXT("unnamed"), EDatasmithElementType::PostProcess )
{
	Temperature = 6500.0f;
	Vignette = 0.0f;
	Dof = 0.0f;
	MotionBlur = 0.0f;
	Saturation = 1.0f;
	ColorFilter = FLinearColor(0.0f, 0.0f, 0.0f);
	CameraISO = -1.f; // Negative means don't override
	CameraShutterSpeed = -1.f;
	Fstop = -1.f;
}

FDatasmithPostProcessVolumeElementImpl::FDatasmithPostProcessVolumeElementImpl(const TCHAR* InName)
	: FDatasmithActorElementImpl( InName, EDatasmithElementType::PostProcessVolume )
	, Settings( MakeShared< FDatasmithPostProcessElementImpl >() )
	, bEnabled( true )
	, bUnbound( true )
{
	
}

FDatasmithCameraActorElementImpl::FDatasmithCameraActorElementImpl(const TCHAR* InName)
	: FDatasmithActorElementImpl(InName, EDatasmithElementType::Camera)
	, PostProcess( new FDatasmithPostProcessElementImpl() )
	, SensorWidth(36.0f)
	, SensorAspectRatio(1.7777777f)
	, bEnableDepthOfField(true)
	, FocusDistance(1000.0f)
	, FStop(5.6f)
	, FocalLength(35.0f)
	, ActorName()
	, bLookAtAllowRoll(false)
{
}

float FDatasmithCameraActorElementImpl::GetSensorWidth() const
{
	return SensorWidth;
}

void FDatasmithCameraActorElementImpl::SetSensorWidth(float InSensorWidth)
{
	SensorWidth = InSensorWidth;
}

float FDatasmithCameraActorElementImpl::GetSensorAspectRatio() const
{
	return SensorAspectRatio;
}

void FDatasmithCameraActorElementImpl::SetSensorAspectRatio(float InSensorAspectRatio)
{
	SensorAspectRatio = InSensorAspectRatio;
}

float FDatasmithCameraActorElementImpl::GetFocusDistance() const
{
	return FocusDistance;
}

void FDatasmithCameraActorElementImpl::SetFocusDistance(float InFocusDistance)
{
	FocusDistance = InFocusDistance;
}

float FDatasmithCameraActorElementImpl::GetFStop() const
{
	return FStop;
}

void FDatasmithCameraActorElementImpl::SetFStop(float InFStop)
{
	FStop = InFStop;
}

float FDatasmithCameraActorElementImpl::GetFocalLength() const
{
	return FocalLength;
}

void FDatasmithCameraActorElementImpl::SetFocalLength(float InFocalLength)
{
	FocalLength = InFocalLength;
}

TSharedPtr< IDatasmithPostProcessElement >& FDatasmithCameraActorElementImpl::GetPostProcess()
{
	return PostProcess;
}

const TSharedPtr< IDatasmithPostProcessElement >& FDatasmithCameraActorElementImpl::GetPostProcess() const
{
	return PostProcess;
}

void FDatasmithCameraActorElementImpl::SetPostProcess(const TSharedPtr< IDatasmithPostProcessElement >& InPostProcess)
{
	PostProcess = InPostProcess;
}

const TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithCustomActorElementImpl::GetPropertyByName( const TCHAR* InName ) const
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithCustomActorElementImpl::GetPropertyByName( const TCHAR* InName )
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

void FDatasmithCustomActorElementImpl::AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty )
{
	if ( !PropertyIndexMap.Contains( InProperty->GetName() ) )
	{
		PropertyIndexMap.Add( InProperty->GetName() ) = Properties.Add( InProperty );
	}
}

FDatasmithMaterialElementImpl::FDatasmithMaterialElementImpl(const TCHAR* InName)
	: FDatasmithBaseMaterialElementImpl(InName, EDatasmithElementType::Material)
{
}

bool FDatasmithMaterialElementImpl::IsSingleShaderMaterial() const
{
	return GetShadersCount() == 1;
}

bool FDatasmithMaterialElementImpl::IsClearCoatMaterial() const
{
	if(GetShadersCount() != 2)
	{
		return false;
	}

	if (GetShader(0)->GetBlendMode() != EDatasmithBlendMode::ClearCoat)
	{
		return false;
	}

	return true;
}

void FDatasmithMaterialElementImpl::AddShader( const TSharedPtr< IDatasmithShaderElement >& InShader )
{
	Shaders.Add(InShader);
}

int32 FDatasmithMaterialElementImpl::GetShadersCount() const
{
	return (int32)Shaders.Num();
}

TSharedPtr< IDatasmithShaderElement >& FDatasmithMaterialElementImpl::GetShader(int32 InIndex)
{
	return Shaders[InIndex];
}

const TSharedPtr< IDatasmithShaderElement >& FDatasmithMaterialElementImpl::GetShader(int32 InIndex) const
{
	return Shaders[InIndex];
}

FDatasmithMasterMaterialElementImpl::FDatasmithMasterMaterialElementImpl(const TCHAR* InName)
	: FDatasmithBaseMaterialElementImpl(InName, EDatasmithElementType::MasterMaterial)
	, MaterialType( EDatasmithMasterMaterialType::Auto )
	, Quality( EDatasmithMasterMaterialQuality::High )
{
}

const TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMasterMaterialElementImpl::GetProperty( int32 InIndex ) const
{
	if ( Properties.IsValidIndex( InIndex ) )
	{
		return Properties[InIndex];
	}

	return NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMasterMaterialElementImpl::GetProperty( int32 InIndex )
{
	if ( Properties.IsValidIndex( InIndex ) )
	{
		return Properties[InIndex];
	}

	return NullPropertyPtr;
}

const TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMasterMaterialElementImpl::GetPropertyByName( const TCHAR* InName ) const
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMasterMaterialElementImpl::GetPropertyByName( const TCHAR* InName )
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

void FDatasmithMasterMaterialElementImpl::AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty )
{
	if ( !PropertyIndexMap.Contains( InProperty->GetName() ) )
	{
		PropertyIndexMap.Add( InProperty->GetName() ) = Properties.Add( InProperty );
	}
}

FDatasmithEnvironmentElementImpl::FDatasmithEnvironmentElementImpl(const TCHAR* InName)
	: FDatasmithLightActorElementImpl(InName, EDatasmithElementType::EnvironmentLight)
	, EnvironmentComp( new FDatasmithCompositeTextureImpl() )
	, bIsIlluminationMap(false)
{
}

TSharedPtr<IDatasmithCompositeTexture>& FDatasmithEnvironmentElementImpl::GetEnvironmentComp()
{
	return EnvironmentComp;
}

const TSharedPtr<IDatasmithCompositeTexture>& FDatasmithEnvironmentElementImpl::GetEnvironmentComp() const
{
	return EnvironmentComp;
}

void FDatasmithEnvironmentElementImpl::SetEnvironmentComp(const TSharedPtr<IDatasmithCompositeTexture>& InEnvironmentComp)
{
	EnvironmentComp = InEnvironmentComp;
}

bool FDatasmithEnvironmentElementImpl::GetIsIlluminationMap() const
{
	return bIsIlluminationMap;
}

void FDatasmithEnvironmentElementImpl::SetIsIlluminationMap(bool bInIsIlluminationMap)
{
	bIsIlluminationMap = bInIsIlluminationMap;
}

FDatasmithTextureElementImpl::FDatasmithTextureElementImpl(const TCHAR* InName)
	: FDatasmithElementImpl( InName, EDatasmithElementType::Texture )
{
	TextureMode = EDatasmithTextureMode::Other;
	TextureFilter = EDatasmithTextureFilter::Default;
	TextureAddressX = EDatasmithTextureAddress::Wrap;
	TextureAddressY = EDatasmithTextureAddress::Wrap;
	bAllowResize = true; // only disabled for environment maps
	RGBCurve = -1.0;

	Data = nullptr;
	DataSize = 0; 
}

FMD5Hash FDatasmithTextureElementImpl::CalculateElementHash(bool bForce)
{
	if (ElementHash.IsValid() && !bForce)
	{
		return ElementHash;
	}
	FMD5 MD5;
	MD5.Update(FileHash.GetBytes(), FileHash.GetSize());
	MD5.Update(reinterpret_cast<uint8*>(&RGBCurve), sizeof(RGBCurve));
	MD5.Update(reinterpret_cast<uint8*>(&TextureMode), sizeof(TextureMode));
	MD5.Update(reinterpret_cast<uint8*>(&TextureFilter), sizeof(TextureFilter));
	MD5.Update(reinterpret_cast<uint8*>(&TextureAddressX), sizeof(TextureAddressX));
	MD5.Update(reinterpret_cast<uint8*>(&TextureAddressY), sizeof(TextureAddressY));
	ElementHash.Set(MD5);
	return ElementHash;
}

const TCHAR* FDatasmithTextureElementImpl::GetFile() const
{
	return *File;
}

void FDatasmithTextureElementImpl::SetFile(const TCHAR* InFile)
{
	File = InFile;
}

EDatasmithTextureMode FDatasmithTextureElementImpl::GetTextureMode() const
{
	return TextureMode;
}

void FDatasmithTextureElementImpl::SetData(const uint8* InData, uint32 InDataSize, EDatasmithTextureFormat InFormat) 
{
	Data = InData;
	DataSize = InDataSize;
	TextureFormat = InFormat;
}

const uint8* FDatasmithTextureElementImpl::GetData(uint32& OutDataSize, EDatasmithTextureFormat& OutFormat) const
{
	OutDataSize = DataSize;
	OutFormat = TextureFormat;
	return Data;
}

void FDatasmithTextureElementImpl::SetTextureMode(EDatasmithTextureMode InMode)
{
	TextureMode = InMode;
}

EDatasmithTextureFilter FDatasmithTextureElementImpl::GetTextureFilter() const
{
	return TextureFilter;
}

void FDatasmithTextureElementImpl::SetTextureFilter(EDatasmithTextureFilter InFilter)
{
	TextureFilter = InFilter;
}

EDatasmithTextureAddress FDatasmithTextureElementImpl::GetTextureAddressX() const
{
	return TextureAddressX;
}

void FDatasmithTextureElementImpl::SetTextureAddressX(EDatasmithTextureAddress InMode)
{
	TextureAddressX = InMode;
}

EDatasmithTextureAddress FDatasmithTextureElementImpl::GetTextureAddressY() const
{
	return TextureAddressY;
}

void FDatasmithTextureElementImpl::SetTextureAddressY(EDatasmithTextureAddress InMode)
{
	TextureAddressY = InMode;
}

bool FDatasmithTextureElementImpl::GetAllowResize() const
{
	return bAllowResize;
}

void FDatasmithTextureElementImpl::SetAllowResize(bool bInAllowResize)
{
	bAllowResize = bInAllowResize;
}

float FDatasmithTextureElementImpl::GetRGBCurve() const
{
	return RGBCurve;
}

void FDatasmithTextureElementImpl::SetRGBCurve(float InRGBCurve)
{
	RGBCurve = InRGBCurve;
}

FDatasmithShaderElementImpl::FDatasmithShaderElementImpl(const TCHAR* InName)
	: FDatasmithElementImpl( InName, EDatasmithElementType::Shader )
	, IOR(0.0)
	, IORk(0.0)
	, IORRefra(0.0)
	, BumpAmount(1.0)
	, bTwoSided(false)
	, DiffuseColor(FLinearColor(0.f, 0.f, 0.f))
	, DiffuseComp( new FDatasmithCompositeTextureImpl() )
	, ReflectanceColor(FLinearColor(0.f, 0.f, 0.f))
	, RefleComp( new FDatasmithCompositeTextureImpl() )
	, Roughness(0.01)
	, RoughnessComp( new FDatasmithCompositeTextureImpl() )
	, NormalComp( new FDatasmithCompositeTextureImpl() )
	, BumpComp( new FDatasmithCompositeTextureImpl() )
	, TransparencyColor(FLinearColor(0.f, 0.f, 0.f))
	, TransComp( new FDatasmithCompositeTextureImpl() )
	, MaskComp( new FDatasmithCompositeTextureImpl() )
	, Displace(0.0)
	, DisplaceSubDivision(0)
	, DisplaceComp( new FDatasmithCompositeTextureImpl() )
	, Metal(0.0)
	, MetalComp(new FDatasmithCompositeTextureImpl())
	, EmitColor(FLinearColor(0.f, 0.f, 0.f))
	, EmitTemperature(0)
	, EmitPower(0)
	, EmitComp( new FDatasmithCompositeTextureImpl() )
	, bLightOnly(false)
	, WeightColor(FLinearColor(0.f, 0.f, 0.f))
	, WeightComp(new FDatasmithCompositeTextureImpl())
	, WeightValue(1.0)
	, BlendMode(EDatasmithBlendMode::Alpha)
	, bIsStackedLayer(false)
	, ShaderUsage(EDatasmithShaderUsage::Surface)
	, bUseEmissiveForDynamicAreaLighting(false)
{
	GetDiffuseComp()->SetBaseNames(DATASMITH_DIFFUSETEXNAME, DATASMITH_DIFFUSECOLNAME, TEXT("unsupported"), DATASMITH_DIFFUSECOMPNAME);
	GetRefleComp()->SetBaseNames(DATASMITH_REFLETEXNAME, DATASMITH_REFLECOLNAME, TEXT("unsupported"), DATASMITH_REFLECOMPNAME);
	GetRoughnessComp()->SetBaseNames(DATASMITH_ROUGHNESSTEXNAME, TEXT("unsupported"), DATASMITH_ROUGHNESSVALUENAME, DATASMITH_ROUGHNESSCOMPNAME);
	GetNormalComp()->SetBaseNames(DATASMITH_NORMALTEXNAME, TEXT("unsupported"), DATASMITH_BUMPVALUENAME, DATASMITH_NORMALCOMPNAME);
	GetBumpComp()->SetBaseNames(DATASMITH_BUMPTEXNAME, TEXT("unsupported"), DATASMITH_BUMPVALUENAME, DATASMITH_BUMPCOMPNAME);
	GetTransComp()->SetBaseNames(DATASMITH_TRANSPTEXNAME, DATASMITH_TRANSPCOLNAME, TEXT("unsupported"), DATASMITH_TRANSPCOMPNAME);
	GetMaskComp()->SetBaseNames(DATASMITH_CLIPTEXNAME, TEXT("unsupported"), TEXT("unsupported"), DATASMITH_CLIPCOMPNAME);
	GetDisplaceComp()->SetBaseNames(DATASMITH_DISPLACETEXNAME, TEXT("unsupported"), TEXT("unsupported"), DATASMITH_DISPLACECOMPNAME);
	GetMetalComp()->SetBaseNames(DATASMITH_METALTEXNAME, TEXT("unsupported"), DATASMITH_METALVALUENAME, DATASMITH_METALCOMPNAME);
	GetEmitComp()->SetBaseNames(DATASMITH_EMITTEXNAME, DATASMITH_EMITCOLNAME, TEXT("unsupported"), DATASMITH_EMITCOMPNAME);
	GetWeightComp()->SetBaseNames(DATASMITH_WEIGHTTEXNAME, DATASMITH_WEIGHTCOLNAME, DATASMITH_WEIGHTVALUENAME, DATASMITH_WEIGHTCOMPNAME);
}

FDatasmithCompositeSurface::FDatasmithCompositeSurface(const TSharedPtr<IDatasmithCompositeTexture>& SubComp)
{
	ParamTextures = TEXT("");
	ParamSampler = FDatasmithTextureSampler();
	ParamSubComposite = SubComp;
	ParamColor = FLinearColor();
	bParamUseTexture = true;
}

FDatasmithCompositeSurface::FDatasmithCompositeSurface(const TCHAR* InTexture, FDatasmithTextureSampler InTexUV)
{
	ParamTextures = FDatasmithUtils::SanitizeObjectName(InTexture);
	ParamSampler = InTexUV;
	ParamSubComposite = FDatasmithSceneFactory::CreateCompositeTexture();
	ParamColor = FLinearColor();
	bParamUseTexture = true;
}

FDatasmithCompositeSurface::FDatasmithCompositeSurface(const FLinearColor& InColor)
{
	ParamTextures = TEXT("");
	ParamSampler = FDatasmithTextureSampler();
	ParamSubComposite = FDatasmithSceneFactory::CreateCompositeTexture();
	ParamColor = InColor;
	bParamUseTexture = false;
}

bool FDatasmithCompositeSurface::GetUseTexture() const
{
	return (bParamUseTexture == true && !ParamSubComposite->IsValid());
}

bool FDatasmithCompositeSurface::GetUseComposite() const
{
	return (bParamUseTexture == true && ParamSubComposite->IsValid());
}

bool FDatasmithCompositeSurface::GetUseColor() const
{
	return !bParamUseTexture;
}

FDatasmithTextureSampler& FDatasmithCompositeSurface::GetParamTextureSampler()
{
	return ParamSampler;
}

const TCHAR* FDatasmithCompositeSurface::GetParamTexture() const
{
	return *ParamTextures;
}

void FDatasmithCompositeSurface::SetParamTexture(const TCHAR* InTexture)
{
	ParamTextures = FDatasmithUtils::SanitizeObjectName(InTexture);
}

const FLinearColor& FDatasmithCompositeSurface::GetParamColor() const
{
	return ParamColor;
}

TSharedPtr<IDatasmithCompositeTexture>& FDatasmithCompositeSurface::GetParamSubComposite()
{
	return ParamSubComposite;
}

FDatasmithCompositeTextureImpl::FDatasmithCompositeTextureImpl()
{
	CompMode = EDatasmithCompMode::Regular;

	BaseTexName = DATASMITH_TEXTURENAME;
	BaseColName = DATASMITH_COLORNAME;
	BaseValName = DATASMITH_VALUE1NAME;
	BaseCompName = DATASMITH_TEXTURECOMPNAME;
}

bool FDatasmithCompositeTextureImpl::IsValid() const
{
	return ( ParamSurfaces.Num() != 0 || ParamVal1.Num() != 0 );
}

bool FDatasmithCompositeTextureImpl::GetUseTexture(int32 InIndex)
{
	ensure( ParamSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamSurfaces.IsValidIndex( InIndex ) )
	{
		return false;
	}

	return ParamSurfaces[InIndex].GetUseTexture();
}

const TCHAR* FDatasmithCompositeTextureImpl::GetParamTexture(int32 InIndex)
{
	ensure( ParamSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamSurfaces.IsValidIndex( InIndex ) )
	{
		return TEXT("");
	}

	return ParamSurfaces[InIndex].GetParamTexture();
}

void FDatasmithCompositeTextureImpl::SetParamTexture(int32 InIndex, const TCHAR* InTexture)
{
	if (ParamSurfaces.IsValidIndex(InIndex) )
	{
		ParamSurfaces[InIndex].SetParamTexture(InTexture);
	}
}

static FDatasmithTextureSampler DefaultTextureSampler;

FDatasmithTextureSampler& FDatasmithCompositeTextureImpl::GetParamTextureSampler(int32 InIndex)
{
	ensure(ParamSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamSurfaces.IsValidIndex( InIndex ) )
	{
		return DefaultTextureSampler;
	}

	return ParamSurfaces[InIndex].GetParamTextureSampler();
}

bool FDatasmithCompositeTextureImpl::GetUseColor(int32 InIndex)
{
	ensure(ParamSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamSurfaces.IsValidIndex( InIndex ) )
	{
		return true; // Fallback to using a color
	}

	return ParamSurfaces[InIndex].GetUseColor();
}

const FLinearColor& FDatasmithCompositeTextureImpl::GetParamColor(int32 InIndex)
{
	ensure(ParamSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamSurfaces.IsValidIndex( InIndex ) )
	{
		return FLinearColor::Black;
	}

	return ParamSurfaces[InIndex].GetParamColor();
}

bool FDatasmithCompositeTextureImpl::GetUseComposite(int32 InIndex)
{
	ensure(ParamSurfaces.IsValidIndex( InIndex ));
	if ( !ParamSurfaces.IsValidIndex( InIndex ))
	{
		return false;
	}

	return (ParamSurfaces[InIndex].GetUseComposite());
}

IDatasmithCompositeTexture::ParamVal FDatasmithCompositeTextureImpl::GetParamVal1(int32 InIndex) const
{
	ensure( ParamVal1.IsValidIndex( InIndex ) );
	if ( !ParamVal1.IsValidIndex( InIndex ) )
	{
		return ParamVal( 0, TEXT("") );
	}

	return ParamVal( ParamVal1[InIndex].Key, *ParamVal1[InIndex].Value );
}

IDatasmithCompositeTexture::ParamVal FDatasmithCompositeTextureImpl::GetParamVal2(int32 InIndex) const
{
	ensure( ParamVal2.IsValidIndex( InIndex ) );
	if ( !ParamVal2.IsValidIndex( InIndex ) )
	{
		return ParamVal( 0, TEXT("") );
	}

	return ParamVal( ParamVal2[InIndex].Key, *ParamVal2[InIndex].Value );
}

const TCHAR* FDatasmithCompositeTextureImpl::GetParamMask(int32 InIndex)
{
	ensure(ParamMaskSurfaces.IsValidIndex(InIndex));
	if (!ParamMaskSurfaces.IsValidIndex(InIndex))
	{
		return TEXT("");
	}

	return ParamMaskSurfaces[InIndex].GetParamTexture();
}

const FLinearColor& FDatasmithCompositeTextureImpl::GetParamMaskColor(int32 InIndex) const
{
	ensure(ParamMaskSurfaces.IsValidIndex(InIndex));
	if (!ParamMaskSurfaces.IsValidIndex(InIndex))
	{
		return FLinearColor::Black;
	}

	return ParamMaskSurfaces[InIndex].GetParamColor();
}

bool FDatasmithCompositeTextureImpl::GetMaskUseComposite(int32 InIndex) const
{
	ensure(ParamMaskSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamMaskSurfaces.IsValidIndex( InIndex ) )
	{
		return false;
	}

	return ParamMaskSurfaces[InIndex].GetUseComposite();
}

FDatasmithTextureSampler FDatasmithCompositeTextureImpl::GetParamMaskTextureSampler(int32 InIndex)
{
	ensure(ParamMaskSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamMaskSurfaces.IsValidIndex( InIndex ) )
	{
		return FDatasmithTextureSampler();
	}

	return ParamMaskSurfaces[InIndex].GetParamTextureSampler();
}

static TSharedPtr<IDatasmithCompositeTexture> InvalidCompositeTexture;

TSharedPtr<IDatasmithCompositeTexture>& FDatasmithCompositeTextureImpl::GetParamSubComposite(int32 InIndex)
{
	ensure(ParamSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamSurfaces.IsValidIndex( InIndex ) )
	{
		return InvalidCompositeTexture;
	}

	return ParamSurfaces[InIndex].GetParamSubComposite();
}

TSharedPtr<IDatasmithCompositeTexture>& FDatasmithCompositeTextureImpl::GetParamMaskSubComposite(int32 InIndex)
{
	ensure(ParamMaskSurfaces.IsValidIndex( InIndex ) );
	if ( !ParamMaskSurfaces.IsValidIndex( InIndex ) )
	{
		return InvalidCompositeTexture;
	}

	return ParamMaskSurfaces[InIndex].GetParamSubComposite();
}

void FDatasmithCompositeTextureImpl::SetBaseNames(const TCHAR* InTextureName, const TCHAR* InColorName, const TCHAR* InValueName, const TCHAR* InCompName)
{
	BaseTexName = InTextureName;
	BaseColName = InColorName;
	BaseValName = InValueName;
	BaseCompName = InCompName;
}

FDatasmithMetaDataElementImpl::FDatasmithMetaDataElementImpl(const TCHAR* InName)
	: FDatasmithElementImpl(InName, EDatasmithElementType::MetaData)
{
}

const TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMetaDataElementImpl::GetPropertyByName( const TCHAR* InName ) const
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMetaDataElementImpl::GetPropertyByName( const TCHAR* InName )
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

void FDatasmithMetaDataElementImpl::AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty )
{
	if ( !PropertyIndexMap.Contains( InProperty->GetName() ) )
	{
		PropertyIndexMap.Add( InProperty->GetName() ) = Properties.Add( InProperty );
	}
}

FDatasmithSceneImpl::FDatasmithSceneImpl(const TCHAR * InName)
	: FDatasmithElementImpl(InName, EDatasmithElementType::Scene)
{
	Reset();
}

void FDatasmithSceneImpl::Reset()
{
	Meshes.Empty();
	Actors.Empty();
	Materials.Empty();
	MetaData.Empty();
	ElementToMetaDataMap.Empty();

	PostProcess.Reset();
	bUseSky = false;

	Hostname.Empty();
	Vendor.Empty();
	ProductName.Empty();
	ProductVersion.Empty();
	UserID.Empty();
	UserOS.Empty();
	ExportDuration = 0;

	ExporterVersion = FDatasmithUtils::GetDatasmithFormatVersionAsString();
	ExporterSDKVersion = FDatasmithUtils::GetEnterpriseVersionAsString();
}

static const TSharedPtr< IDatasmithMeshElement > InvalidMeshElement;

TSharedPtr< IDatasmithMeshElement > FDatasmithSceneImpl::GetMesh(int32 InIndex)
{
	if ( Meshes.IsValidIndex( InIndex ) )
	{
		return Meshes[InIndex];
	}
	else
	{
		return TSharedPtr< IDatasmithMeshElement >();
	}
}

const TSharedPtr< IDatasmithMeshElement >& FDatasmithSceneImpl::GetMesh(int32 InIndex) const
{
	if ( Meshes.IsValidIndex( InIndex ) )
	{
		return Meshes[InIndex];
	}
	else
	{
		return InvalidMeshElement;
	}
}

static const TSharedPtr< IDatasmithMetaDataElement > InvalidMetaData;

TSharedPtr< IDatasmithMetaDataElement > FDatasmithSceneImpl::GetMetaData(int32 InIndex)
{
	if ( MetaData.IsValidIndex( InIndex ) )
	{
		return MetaData[ InIndex ];
	}
	else
	{
		return TSharedPtr< IDatasmithMetaDataElement >();
	}
}

const TSharedPtr< IDatasmithMetaDataElement >& FDatasmithSceneImpl::GetMetaData(int32 InIndex) const
{
	if ( MetaData.IsValidIndex( InIndex ) )
	{
		return MetaData[ InIndex ];
	}
	else
	{
		return InvalidMetaData;
	}
}

TSharedPtr< IDatasmithMetaDataElement > FDatasmithSceneImpl::GetMetaData(const TSharedPtr<IDatasmithElement>& Element)
{
	if (TSharedPtr< IDatasmithMetaDataElement >* MetaDataElement = ElementToMetaDataMap.Find(Element))
	{
		return *MetaDataElement;
	}
	else
	{
		return TSharedPtr< IDatasmithMetaDataElement >();
	}
}

const TSharedPtr< IDatasmithMetaDataElement >& FDatasmithSceneImpl::GetMetaData(const TSharedPtr<IDatasmithElement>& Element) const
{
	if (const TSharedPtr< IDatasmithMetaDataElement >* MetaDataElement = ElementToMetaDataMap.Find(Element))
	{
		return *MetaDataElement;
	}
	else
	{
		return InvalidMetaData;
	}
}

namespace DatasmithSceneImplInternal
{
	template<typename ContainerType, typename SharedPtrElementType>
	void RemoveActor(FDatasmithSceneImpl* SceneImpl, ContainerType& ActorContainer, SharedPtrElementType& InActor, EDatasmithActorRemovalRule RemoveRule)
	{
		FDatasmithSceneUtils::TActorHierarchy FoundHierarchy;
		bool bFound = FDatasmithSceneUtils::FindActorHierarchy(SceneImpl, InActor, FoundHierarchy);
		if (bFound)
		{
			// If Actor is found, it is always added to FoundHierarchy
			// And if it is at the root, it will be the only item in FoundHierarchy
			if (FoundHierarchy.Num() == 1)
			{
				// The actor lives at the root
				if (RemoveRule == EDatasmithActorRemovalRule::KeepChildrenAndKeepRelativeTransform)
				{
					for (int32 ChildIndex = InActor->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
					{
						const TSharedPtr< IDatasmithActorElement > Child = InActor->GetChild(ChildIndex);
						InActor->RemoveChild(Child);
						SceneImpl->AddActor(Child);
					}
				}
				else
				{
					check(RemoveRule == EDatasmithActorRemovalRule::RemoveChildren);
				}

				ActorContainer.Remove(InActor);
			}
			else
			{
				// The actor lives as a child of another actor
				if (RemoveRule == EDatasmithActorRemovalRule::KeepChildrenAndKeepRelativeTransform)
				{
					for (int32 ChildIndex = InActor->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
					{
						const TSharedPtr< IDatasmithActorElement > Child = InActor->GetChild(ChildIndex);
						InActor->RemoveChild(Child);
						FoundHierarchy.Last()->AddChild(Child);
					}
				}
				else
				{
					check(RemoveRule == EDatasmithActorRemovalRule::RemoveChildren);
				}

				FoundHierarchy.Last()->RemoveChild(InActor);
			}
		}
	}
}

void FDatasmithSceneImpl::RemoveActor(const TSharedPtr< IDatasmithActorElement >& InActor, EDatasmithActorRemovalRule RemoveRule)
{
	DatasmithSceneImplInternal::RemoveActor(this, Actors, InActor, RemoveRule);
}

namespace DatasmithSceneImplInternal
{
	void AttachActorToSceneRoot(FDatasmithSceneImpl* SceneImpl, const TSharedPtr< IDatasmithActorElement >& Child, EDatasmithActorAttachmentRule AttachmentRule, const FDatasmithSceneUtils::TActorHierarchy& FoundChildHierarchy)
	{
		// The child is already to the root?
		if (FoundChildHierarchy.Num() != 0)
		{
			if ( AttachmentRule == EDatasmithActorAttachmentRule::KeepRelativeTransform )
			{
				TSharedPtr< IDatasmithActorElement > DirectParent = FoundChildHierarchy.Last();

				FTransform ChildWorldTransform( Child->GetRotation(), Child->GetTranslation(), Child->GetScale() );
				FTransform ParentWorldTransform( DirectParent->GetRotation(), DirectParent->GetTranslation(), DirectParent->GetScale() );

				FTransform ChildRelativeTransform = ChildWorldTransform.GetRelativeTransform( ParentWorldTransform );

				Child->SetRotation( ChildRelativeTransform.GetRotation() );
				Child->SetTranslation( ChildRelativeTransform.GetTranslation() );
				Child->SetScale( ChildRelativeTransform.GetScale3D() );
			}

			FoundChildHierarchy.Last()->RemoveChild(Child);
			SceneImpl->AddActor(Child);
		}
	}
}

void FDatasmithSceneImpl::AttachActor(const TSharedPtr< IDatasmithActorElement >& NewParent, const TSharedPtr< IDatasmithActorElement >& Child, EDatasmithActorAttachmentRule AttachmentRule)
{
	FDatasmithSceneUtils::TActorHierarchy FoundParentHierarchy;
	bool bNewParentFound = FDatasmithSceneUtils::FindActorHierarchy(this, NewParent, FoundParentHierarchy);
	FDatasmithSceneUtils::TActorHierarchy FoundChildHierarchy;
	bool bChildFound = FDatasmithSceneUtils::FindActorHierarchy(this, Child, FoundChildHierarchy);

	if (!bNewParentFound)
	{
		if (bChildFound)
		{
			// If the parent doesn't exist, move it at the root
			DatasmithSceneImplInternal::AttachActorToSceneRoot(this, Child, AttachmentRule, FoundChildHierarchy);
		}
		return;
	}

	if(!bChildFound)
	{
		// No one to attach
		return;
	}

	if (AttachmentRule == EDatasmithActorAttachmentRule::KeepRelativeTransform)
	{
		// Convert Child transform from world to relative, so that we end up at the same position relatively to NewParent
		if ( FoundChildHierarchy.Num() > 0 )
		{
			TSharedPtr< IDatasmithActorElement > DirectParent = FoundChildHierarchy.Last();

			FTransform ChildWorldTransform( Child->GetRotation(), Child->GetTranslation(), Child->GetScale() );
			FTransform ParentWorldTransform( DirectParent->GetRotation(), DirectParent->GetTranslation(), DirectParent->GetScale() );

			FTransform ChildRelativeTransform = ChildWorldTransform.GetRelativeTransform( ParentWorldTransform );;

			Child->SetRotation( ChildRelativeTransform.GetRotation() );
			Child->SetTranslation( ChildRelativeTransform.GetTranslation() );
			Child->SetScale( ChildRelativeTransform.GetScale3D() );
		}
	}

	if (FoundChildHierarchy.Num() == 0)
	{
		RemoveActor(Child, EDatasmithActorRemovalRule::RemoveChildren);
	}
	else
	{
		FoundChildHierarchy.Last()->RemoveChild(Child);
	}

	NewParent->AddChild(Child, AttachmentRule);
}

void FDatasmithSceneImpl::AttachActorToSceneRoot(const TSharedPtr< IDatasmithActorElement >& Child, EDatasmithActorAttachmentRule AttachmentRule)
{
	FDatasmithSceneUtils::TActorHierarchy FoundChildHierarchy;
	bool bChildFound = FDatasmithSceneUtils::FindActorHierarchy(this, Child, FoundChildHierarchy);

	if (bChildFound)
	{
		DatasmithSceneImplInternal::AttachActorToSceneRoot(this, Child, AttachmentRule, FoundChildHierarchy);
	}
}
