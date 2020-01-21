// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IDatasmithSceneElements.h"
#include "DatasmithDefinitions.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/SharedPointer.h"

template< typename InterfaceType >
class FDatasmithElementImpl : public InterfaceType
{
public:
	FDatasmithElementImpl(const TCHAR* InName, EDatasmithElementType InType, uint64 InSubType = 0);
	virtual ~FDatasmithElementImpl() {}

	virtual bool IsA( EDatasmithElementType InType ) const override { return ( uint64(Type) & uint64(InType) ) != 0; }
	virtual bool IsSubType( uint64 InSubType ) const override { return ( InSubType & SubType ) != 0; }

	virtual const TCHAR* GetName() const override { return *Name; }
	virtual void SetName(const TCHAR* InName) override { Name = FDatasmithUtils::SanitizeObjectName(InName); }

	virtual const TCHAR* GetLabel() const override { return Label.IsEmpty() ? GetName() : *Label; }
	virtual void SetLabel(const TCHAR* InLabel) override { Label = FDatasmithUtils::SanitizeObjectName(InLabel); }

	virtual FMD5Hash CalculateElementHash(bool) override { return ElementHash; }

protected:
	FString Name;
	FString Label;
	FMD5Hash ElementHash;

	EDatasmithElementType Type;
	uint64 SubType;
};

template< typename InterfaceType >
inline FDatasmithElementImpl< InterfaceType >::FDatasmithElementImpl(const TCHAR* InName, EDatasmithElementType InType, uint64 InSubType)
	: Name(FDatasmithUtils::SanitizeObjectName(InName))
	, Type(InType)
	, SubType(InSubType)
{
}

class FDatasmithKeyValuePropertyImpl : public FDatasmithElementImpl< IDatasmithKeyValueProperty >
{
public:
	FDatasmithKeyValuePropertyImpl(const TCHAR* InName);

	EDatasmithKeyValuePropertyType GetPropertyType() const override { return PropertyType; }
	void SetPropertyType( EDatasmithKeyValuePropertyType InType ) override;

	const TCHAR* GetValue() const override { return *Value; }
	void SetValue( const TCHAR* InValue ) override;

protected:
	void FormatValue();

private:
	EDatasmithKeyValuePropertyType PropertyType;
	FString Value;
};

template< typename InterfaceType >
class FDatasmithActorElementImpl : public FDatasmithElementImpl< InterfaceType >, public TSharedFromThis< FDatasmithActorElementImpl< InterfaceType > >
{
public:
	FDatasmithActorElementImpl(const TCHAR* InName, EDatasmithElementType InType);

	virtual FVector GetTranslation() const override { return Translation; }
	virtual void SetTranslation(float InX, float InY, float InZ) override { SetTranslation( FVector( InX, InY, InZ ) ); }
	virtual void SetTranslation(const FVector& Value) override { ConvertChildsToRelative(); Translation = Value; ConvertChildsToWorld(); }

	virtual FVector GetScale() const override { return Scale; }
	virtual void SetScale(float InX, float InY, float InZ) override { SetScale( FVector( InX, InY, InZ ) ); }
	virtual void SetScale(const FVector& Value) override { ConvertChildsToRelative(); Scale = Value; ConvertChildsToWorld(); }

	virtual FQuat GetRotation() const override { return Rotation; }
	virtual void SetRotation(float InX, float InY, float InZ, float InW) override { SetRotation( FQuat( InX, InY, InZ, InW ) ); }
	virtual void SetRotation(const FQuat& Value) override { ConvertChildsToRelative(); Rotation = Value; ConvertChildsToWorld(); }

	virtual void SetUseParentTransform(bool bInUseParentTransform) override { bUseParentTransform = bInUseParentTransform;}
	virtual FTransform GetRelativeTransform() const override;

	virtual const TCHAR* GetLayer() const override { return *Layer; }
	virtual void SetLayer(const TCHAR* InLayer) override { Layer = InLayer; }

	virtual void AddTag(const TCHAR* InTag) override { Tags.Add(InTag); }
	virtual void ResetTags() override { Tags.Reset(); }
	virtual int32 GetTagsCount() const { return Tags.Num(); }
	virtual const TCHAR* GetTag(int32 TagIndex) const override { return Tags.IsValidIndex(TagIndex) ? *Tags[TagIndex] : nullptr; }

	virtual void AddChild(const TSharedPtr< IDatasmithActorElement >& InChild, EDatasmithActorAttachmentRule AttachementRule = EDatasmithActorAttachmentRule::KeepWorldTransform) override;
	virtual int32 GetChildrenCount() const override { return Children.Num(); }
	/** Get the 'InIndex'th child of the actor  */
	virtual TSharedPtr< IDatasmithActorElement > GetChild(int32 InIndex) override { return Children.IsValidIndex(InIndex) ? Children[InIndex] : NullActorPtr; };
	virtual const TSharedPtr< IDatasmithActorElement >& GetChild(int32 InIndex) const override { return Children.IsValidIndex(InIndex) ? Children[InIndex] : NullActorPtr; };
	virtual void RemoveChild(const TSharedPtr< IDatasmithActorElement >& InChild) override { Children.Remove(InChild); static_cast< FDatasmithActorElementImpl* >( InChild.Get() )->Parent.Reset(); }

	virtual void SetIsAComponent(bool Value) { bIsAComponent = Value; }
	virtual bool IsAComponent() const override { return bIsAComponent; }

	virtual void SetAsSelector(bool bInAsSelector) override { bIsASelector = bInAsSelector; }
	virtual bool IsASelector() const override { return bIsASelector; }

	/** Set the index of the child which is active in a selector  */
	virtual void SetSelectionIndex(int32 InSelectionIdx) override { SelectionIdx = InSelectionIdx; }

	/** Get the index of the child which is active in a selector. Default is -1.  */
	virtual int32 GetSelectionIndex() const override { return SelectionIdx; }

	virtual void SetVisibility(bool bInVisibility) override { bVisibility = bInVisibility; }
	virtual bool GetVisibility() const override { return bVisibility; }

protected:
	/** Converts all childs transforms to relative */
	void ConvertChildsToRelative();

	/** Converts all childs transforms to world */
	void ConvertChildsToWorld();

private:
	static TSharedPtr<IDatasmithActorElement> NullActorPtr;

	FVector Translation;
	FVector Scale;
	FQuat Rotation;

	FString Layer;

	TArray< FString > Tags;

	TArray< TSharedPtr< IDatasmithActorElement > > Children;
	TSharedPtr< IDatasmithActorElement > Parent;

	bool bIsAComponent;
	bool bIsASelector;
	bool bVisibility;
	bool bUseParentTransform;
	int32 SelectionIdx;
};

template< typename InterfaceType >
TSharedPtr<IDatasmithActorElement> FDatasmithActorElementImpl< InterfaceType >::NullActorPtr;

template< typename T >
inline FDatasmithActorElementImpl<T>::FDatasmithActorElementImpl(const TCHAR* InName, EDatasmithElementType ChildType)
	: FDatasmithElementImpl<T>(InName, EDatasmithElementType::Actor | ChildType)
	, Translation(FVector::ZeroVector)
	, Scale(FVector::OneVector)
	, Rotation(FQuat::Identity)
	, bIsAComponent(false)
	, bIsASelector(false)
	, bVisibility(true)
	, bUseParentTransform(true)
	, SelectionIdx(-1)
{
}

template< typename T >
inline FTransform FDatasmithActorElementImpl<T>::GetRelativeTransform() const
{
	FTransform ActorTransform( GetRotation(), GetTranslation(), GetScale() );

	if ( Parent.IsValid() && bUseParentTransform )
	{
		FTransform ParentTransform( Parent->GetRotation(), Parent->GetTranslation(), Parent->GetScale() );

		return ActorTransform.GetRelativeTransform( ParentTransform );
	}

	return ActorTransform;
}

template< typename T >
inline void FDatasmithActorElementImpl<T>::AddChild(const TSharedPtr< IDatasmithActorElement >& InChild, EDatasmithActorAttachmentRule AttachementRule)
{
	if ( AttachementRule == EDatasmithActorAttachmentRule::KeepRelativeTransform )
	{
		FTransform RelativeTransform( InChild->GetRotation(), InChild->GetTranslation(), InChild->GetScale() );
		FTransform ParentTransform( GetRotation(), GetTranslation(), GetScale() );

		FTransform WorldTransform = RelativeTransform * ParentTransform;

		InChild->SetRotation( WorldTransform.GetRotation() );
		InChild->SetTranslation( WorldTransform.GetTranslation() );
		InChild->SetScale( WorldTransform.GetScale3D() );
	}

	Children.Add(InChild);
	static_cast< FDatasmithActorElementImpl* >( InChild.Get() )->Parent = this->AsShared();
}

template< typename T >
inline void FDatasmithActorElementImpl<T>::ConvertChildsToRelative()
{
	FTransform ThisWorldTransform( GetRotation(), GetTranslation(), GetScale() );

	for ( TSharedPtr< IDatasmithActorElement >& Child : Children )
	{
		if ( !Child.IsValid() )
		{
			continue;
		}

		FDatasmithActorElementImpl* ChildImpl = static_cast< FDatasmithActorElementImpl* >( Child.Get() );
		ChildImpl->ConvertChildsToRelative(); // Depth first while we're still in world space

		FTransform ChildWorldTransform( Child->GetRotation(), Child->GetTranslation(), Child->GetScale() );

		FTransform ChildRelativeTransform = ChildWorldTransform.GetRelativeTransform( ThisWorldTransform );
		ChildImpl->Rotation = ChildRelativeTransform.GetRotation();
		ChildImpl->Translation = ChildRelativeTransform.GetTranslation();
		ChildImpl->Scale = ChildRelativeTransform.GetScale3D();
	}
}

template< typename T >
inline void FDatasmithActorElementImpl<T>::ConvertChildsToWorld()
{
	FTransform ThisWorldTransform( GetRotation(), GetTranslation(), GetScale() );

	for ( TSharedPtr< IDatasmithActorElement >& Child : Children )
	{
		if ( !Child.IsValid() )
		{
			continue;
		}

		FDatasmithActorElementImpl* ChildImpl = static_cast< FDatasmithActorElementImpl* >( Child.Get() );

		FTransform ChildRelativeTransform( Child->GetRotation(), Child->GetTranslation(), Child->GetScale() );

		FTransform ChildWorldTransform = ChildRelativeTransform * ThisWorldTransform;
		ChildImpl->Rotation = ChildWorldTransform.GetRotation();
		ChildImpl->Translation = ChildWorldTransform.GetTranslation();
		ChildImpl->Scale = ChildWorldTransform.GetScale3D();

		ChildImpl->ConvertChildsToWorld(); // Depth last now that we're in world space
	}
}

class FDatasmithMeshElementImpl : public FDatasmithElementImpl< IDatasmithMeshElement >
{
public:
	explicit FDatasmithMeshElementImpl(const TCHAR* InName);

	virtual FMD5Hash CalculateElementHash(bool bForce) override;

	virtual const TCHAR* GetFile() const override { return *File; }
	virtual void SetFile(const TCHAR* InFile) override { File = InFile; };

	virtual FMD5Hash GetFileHash() const override { return FileHash; }
	virtual void SetFileHash(FMD5Hash Hash) override { FileHash = Hash; }

	virtual void SetDimensions(const float InArea, const float InWidth, const float InHeight, const float InDepth) override { Area = InArea; Width = InWidth; Height = InHeight; Depth = InDepth;};
	virtual FVector GetDimensions() const override { return FVector{ Width, Height, Depth }; }

	virtual float GetArea() const override { return Area; }
	virtual float GetWidth() const override { return Width; }
	virtual float GetHeight() const override { return Height; }
	virtual float GetDepth() const override { return Depth; }

	virtual int32 GetLightmapCoordinateIndex() const { return LightmapCoordinateIndex; }
	virtual void SetLightmapCoordinateIndex(int32 UVChannel) { LightmapCoordinateIndex = UVChannel;  }

	virtual int32 GetLightmapSourceUV() const override { return LightmapSourceUV; }
	virtual void SetLightmapSourceUV( int32 UVChannel ) override { LightmapSourceUV = UVChannel; }

	virtual void SetMaterial(const TCHAR* MaterialPathName, int32 SlotId) override;
	virtual const TCHAR* GetMaterial(int32 SlotId) const override;

	virtual int32 GetMaterialSlotCount() const override;
	virtual TSharedPtr<const IDatasmithMaterialIDElement> GetMaterialSlotAt(int32 Index) const override;
	virtual TSharedPtr<IDatasmithMaterialIDElement> GetMaterialSlotAt(int32 Index) override;

protected:
	virtual int32 GetLODCount() const override { return LODCount; }
	virtual void SetLODCount(int32 Count) override { LODCount = Count; }

private:
	FString File;
	FMD5Hash FileHash;
	float Area;
	float Width;
	float Height;
	float Depth;
	int32 LODCount;
	int32 LightmapCoordinateIndex;
	int32 LightmapSourceUV;
	TArray<TSharedPtr<IDatasmithMaterialIDElement>> MaterialSlots;
};

class FDatasmithMaterialIDElementImpl : public FDatasmithElementImpl< IDatasmithMaterialIDElement >
{
public:
	explicit FDatasmithMaterialIDElementImpl(const TCHAR* InName);

	virtual int32 GetId() const override { return Id; }
	virtual void SetId(int32 InId) override { Id = InId; }

private:
	int32 Id;
};

template< typename InterfaceType = IDatasmithMeshActorElement >
class FDatasmithMeshActorElementImpl : public FDatasmithActorElementImpl< InterfaceType >
{
public:
	explicit FDatasmithMeshActorElementImpl(const TCHAR* InName);

	virtual void AddMaterialOverride(const TCHAR* InMaterialName, int32 Id) override;
	virtual void AddMaterialOverride(const TSharedPtr< IDatasmithMaterialIDElement >& Material) override;

	virtual int32 GetMaterialOverridesCount() const override;
	virtual TSharedPtr<IDatasmithMaterialIDElement> GetMaterialOverride(int32 i) override;
	virtual TSharedPtr<const IDatasmithMaterialIDElement> GetMaterialOverride(int32 i) const override;
	virtual void RemoveMaterialOverride(const TSharedPtr< IDatasmithMaterialIDElement >& Material) override;

	virtual const TCHAR* GetStaticMeshPathName() const override;
	virtual void SetStaticMeshPathName(const TCHAR* InStaticMeshName) override;

protected:
	explicit FDatasmithMeshActorElementImpl(const TCHAR* InName, EDatasmithElementType ElementType);

private:
	FString StaticMeshPathName;
	TArray< TSharedPtr< IDatasmithMaterialIDElement > > Materials;
};

template < typename InterfaceType >
FDatasmithMeshActorElementImpl< InterfaceType >::FDatasmithMeshActorElementImpl(const TCHAR* InName)
	: FDatasmithActorElementImpl< InterfaceType >(InName, EDatasmithElementType::StaticMeshActor)
{
}

template < typename InterfaceType >
FDatasmithMeshActorElementImpl< InterfaceType >::FDatasmithMeshActorElementImpl(const TCHAR* InName, EDatasmithElementType ElementType)
	: FDatasmithActorElementImpl< InterfaceType >(InName, EDatasmithElementType::StaticMeshActor | ElementType)
{
}

template < typename InterfaceType >
void FDatasmithMeshActorElementImpl< InterfaceType >::AddMaterialOverride(const TCHAR* InMaterialName, int32 Id)
{
	FString MaterialName = FDatasmithUtils::SanitizeObjectName(InMaterialName);

	for (const TSharedPtr< IDatasmithMaterialIDElement >& Material : Materials)
	{
		if (FString(Material->GetName()) == MaterialName && Material->GetId() == Id)
		{
			return;
		}
	}

	TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(*MaterialName);
	MaterialIDElement->SetId(Id);
	Materials.Add(MaterialIDElement);
}

template < typename InterfaceType >
void FDatasmithMeshActorElementImpl< InterfaceType >::AddMaterialOverride(const TSharedPtr< IDatasmithMaterialIDElement >& Material)
{
	Materials.Add(Material);
}

template < typename InterfaceType >
int32 FDatasmithMeshActorElementImpl< InterfaceType >::GetMaterialOverridesCount() const
{
	return (int32)Materials.Num();
}

template < typename InterfaceType >
TSharedPtr<IDatasmithMaterialIDElement> FDatasmithMeshActorElementImpl< InterfaceType >::GetMaterialOverride(int32 i)
{
	if (Materials.IsValidIndex(i))
	{
		return Materials[i];
	}
	const TSharedPtr<IDatasmithMaterialIDElement> InvalidMaterialID;
	return InvalidMaterialID;
}

template < typename InterfaceType >
TSharedPtr<const IDatasmithMaterialIDElement> FDatasmithMeshActorElementImpl< InterfaceType >::GetMaterialOverride(int32 i) const
{
	if (Materials.IsValidIndex(i))
	{
		return Materials[i];
	}
	const TSharedPtr<IDatasmithMaterialIDElement> InvalidMaterialID;
	return InvalidMaterialID;
}

template < typename InterfaceType >
void FDatasmithMeshActorElementImpl< InterfaceType >::RemoveMaterialOverride(const TSharedPtr< IDatasmithMaterialIDElement >& Material)
{
	Materials.Remove(Material);
}

template < typename InterfaceType >
const TCHAR* FDatasmithMeshActorElementImpl< InterfaceType >::GetStaticMeshPathName() const
{
	return *StaticMeshPathName;
}

template < typename InterfaceType >
void FDatasmithMeshActorElementImpl< InterfaceType >::SetStaticMeshPathName(const TCHAR* InStaticMeshName)
{
	StaticMeshPathName = InStaticMeshName;
}

class FDatasmithHierarchicalInstancedStaticMeshActorElementImpl : public FDatasmithMeshActorElementImpl< IDatasmithHierarchicalInstancedStaticMeshActorElement >
{
public:
	explicit FDatasmithHierarchicalInstancedStaticMeshActorElementImpl(const TCHAR* InName);

	virtual ~FDatasmithHierarchicalInstancedStaticMeshActorElementImpl();

	virtual int32 GetInstancesCount() const override;
	virtual void ReserveSpaceForInstances(int32 NumIntances) override;
	virtual int32 AddInstance(const FTransform& Transform) override;
	virtual FTransform GetInstance(int32 InstanceIndex) const override;
	virtual void RemoveInstance(int32 InstanceIndex) override;

private:
	TArray<FTransform> Instances;
};

template< typename InterfaceType = IDatasmithLightActorElement >
class FDatasmithLightActorElementImpl : public FDatasmithActorElementImpl< InterfaceType >
{
public:
	virtual bool IsEnabled() const override	{ return bEnabled; }
	virtual void SetEnabled(bool bInIsEnabled) override { bEnabled = bInIsEnabled; }

	virtual double GetIntensity() const override { return Intensity; }
	virtual void SetIntensity(double InIntensity) override { Intensity = InIntensity; }

	virtual FLinearColor GetColor() const override { return Color; }
	virtual void SetColor(FLinearColor InColor) override { Color = InColor; }

	virtual double GetTemperature() const override { return Temperature; }
	virtual void SetTemperature(double InTemperature) override { Temperature = InTemperature; }

	virtual bool GetUseTemperature() const override { return bUseTemperature; }
	virtual void SetUseTemperature(bool bInUseTemperature) override { bUseTemperature = bInUseTemperature; }

	virtual const TCHAR* GetIesFile() const override { return *IesFile;	}
	virtual void SetIesFile(const TCHAR* InIesFile) override { IesFile = InIesFile;	}

	virtual bool GetUseIes() const override { return bUseIes; }
	virtual void SetUseIes(bool bInUseIes) override	{ bUseIes = bInUseIes; }

	virtual double GetIesBrightnessScale() const override { return IesBrightnessScale; }
	virtual void SetIesBrightnessScale(double InIesBrightnessScale) override { IesBrightnessScale = InIesBrightnessScale; }

	virtual bool GetUseIesBrightness() const override { return bUseIesBrightness; }
	virtual void SetUseIesBrightness(bool bInUseIesBrightness) override { bUseIesBrightness = bInUseIesBrightness; }

	virtual FQuat GetIesRotation() const override { return IesRotation; }
	virtual void SetIesRotation(const FQuat& InIesRotation) override { IesRotation = InIesRotation; }

	TSharedPtr< IDatasmithMaterialIDElement >& GetLightFunctionMaterial() override	{ return LightFunctionMaterial; }

	void SetLightFunctionMaterial(const TSharedPtr< IDatasmithMaterialIDElement >& InMaterial) override { LightFunctionMaterial = InMaterial; }

	void SetLightFunctionMaterial(const TCHAR* InMaterialName) override
	{
		FString MaterialName = FDatasmithUtils::SanitizeObjectName(InMaterialName);
		LightFunctionMaterial = FDatasmithSceneFactory::CreateMaterialId(*MaterialName);
	}

protected:
	explicit FDatasmithLightActorElementImpl(const TCHAR* InName, EDatasmithElementType ChildType)
		: FDatasmithActorElementImpl< InterfaceType >( InName, EDatasmithElementType::Light | ChildType )
	{
		Intensity = 1.0;

		Color.R = 1.0f;
		Color.G = 1.0f;
		Color.B = 1.0f;

		bEnabled = true;
		Temperature = 6500.0;
		bUseTemperature = false;
		bUseIes = false;
		IesBrightnessScale = 1.0;
		bUseIesBrightness = false;

		IesRotation = FQuat::Identity;
	}

private:
	bool bEnabled;
	double Intensity;
	FLinearColor Color;

	double Temperature;
	bool bUseTemperature;

	FString IesFile;
	bool bUseIes;

	double IesBrightnessScale;
	bool bUseIesBrightness;

	TSharedPtr< IDatasmithMaterialIDElement > LightFunctionMaterial;

	FQuat IesRotation;
};

template< typename InterfaceType = IDatasmithPointLightElement >
class FDatasmithPointLightElementImpl : public FDatasmithLightActorElementImpl< InterfaceType >
{
public:
	explicit FDatasmithPointLightElementImpl(const TCHAR* InName)
		: FDatasmithPointLightElementImpl( InName, EDatasmithElementType::None )
	{
	}

	virtual void SetIntensityUnits(EDatasmithLightUnits InUnits) { Units = InUnits; }
	virtual EDatasmithLightUnits GetIntensityUnits() const { return Units; }

	virtual float GetSourceRadius() const override { return SourceRadius; }
	virtual void SetSourceRadius(float InSourceRadius) override { SourceRadius = InSourceRadius; }

	virtual float GetSourceLength() const override { return SourceLength; }
	virtual void SetSourceLength(float InSourceLength) override { SourceLength = InSourceLength;}

	virtual float GetAttenuationRadius() const override	{ return AttenuationRadius;	}
	virtual void SetAttenuationRadius(float InAttenuationRadius) override {	AttenuationRadius = InAttenuationRadius; }

protected:
	explicit FDatasmithPointLightElementImpl(const TCHAR* InName, EDatasmithElementType ChildType)
		: FDatasmithLightActorElementImpl< InterfaceType >( InName, EDatasmithElementType::PointLight | ChildType )
	{
		Units = EDatasmithLightUnits::Unitless;
		SourceRadius = -1;
		SourceLength = -1;
		AttenuationRadius = -1;
	}

private:
	EDatasmithLightUnits Units;
	float SourceRadius;
	float SourceLength;
	float AttenuationRadius;
};

template< typename InterfaceType = IDatasmithSpotLightElement >
class FDatasmithSpotLightElementImpl : public FDatasmithPointLightElementImpl< InterfaceType >
{
public:
	explicit FDatasmithSpotLightElementImpl(const TCHAR* InName)
		: FDatasmithSpotLightElementImpl( InName, EDatasmithElementType::None )
	{
	}

	virtual float GetInnerConeAngle() const override
	{
		return InnerConeAngle;
	}

	virtual void SetInnerConeAngle(float InInnerConeAngle) override
	{
		InnerConeAngle = InInnerConeAngle;
	}

	virtual float GetOuterConeAngle() const override
	{
		return OuterConeAngle;
	}

	virtual void SetOuterConeAngle(float InOuterConeAngle) override
	{
		OuterConeAngle = InOuterConeAngle;
	}

protected:
	explicit FDatasmithSpotLightElementImpl(const TCHAR* InName, EDatasmithElementType ChildType)
		: FDatasmithPointLightElementImpl< InterfaceType >( InName, EDatasmithElementType::SpotLight | ChildType )
	{
		InnerConeAngle = 45.f;
		OuterConeAngle = 60.f;
	}

private:
	float InnerConeAngle;
	float OuterConeAngle;
};

class FDatasmithDirectionalLightElementImpl : public FDatasmithLightActorElementImpl< IDatasmithDirectionalLightElement >
{
public:
	explicit FDatasmithDirectionalLightElementImpl(const TCHAR* InName)
		: FDatasmithLightActorElementImpl< IDatasmithDirectionalLightElement >( InName, EDatasmithElementType::DirectionalLight )
	{
	}
};

class FDatasmithAreaLightElementImpl : public FDatasmithSpotLightElementImpl< IDatasmithAreaLightElement >
{
public:
	explicit FDatasmithAreaLightElementImpl(const TCHAR* InName)
		: FDatasmithSpotLightElementImpl< IDatasmithAreaLightElement >( InName, EDatasmithElementType::AreaLight )
		, LightShape( EDatasmithLightShape::Rectangle )
		, LightType( EDatasmithAreaLightType::Point )
		, Width( 0.f )
		, Length( 0.f )
	{
	}

	virtual EDatasmithLightShape GetLightShape() const override { return LightShape; }
	virtual void SetLightShape(EDatasmithLightShape InShape) override { LightShape = InShape; }

	virtual EDatasmithAreaLightType GetLightType() const override { return LightType; }
	virtual void SetLightType(EDatasmithAreaLightType InLightType) override { LightType = InLightType; }

	virtual void SetWidth(float InWidth) override { Width = InWidth; }
	virtual float GetWidth() const override { return Width; }

	virtual void SetLength(float InLength) override { Length = InLength; }
	virtual float GetLength() const override { return Length; }

private:
	EDatasmithLightShape LightShape;
	EDatasmithAreaLightType LightType;
	float Width;
	float Length;
};

class FDatasmithLightmassPortalElementImpl : public FDatasmithPointLightElementImpl< IDatasmithLightmassPortalElement >
{
public:
	explicit FDatasmithLightmassPortalElementImpl(const TCHAR* InName)
		: FDatasmithPointLightElementImpl< IDatasmithLightmassPortalElement >( InName, EDatasmithElementType::LightmassPortal )
	{
	}
};

class FDatasmithPostProcessElementImpl : public FDatasmithElementImpl< IDatasmithPostProcessElement >
{
public:
	FDatasmithPostProcessElementImpl();

	virtual float GetTemperature() const override { return Temperature; }
	virtual void SetTemperature(float InTemperature) override { Temperature = InTemperature; }

	virtual FLinearColor GetColorFilter() const override { return ColorFilter; }
	virtual void SetColorFilter(FLinearColor InColorFilter) override { ColorFilter = InColorFilter; }

	virtual float GetVignette() const override { return Vignette; }
	virtual void SetVignette(float InVignette) override { Vignette = InVignette; }

	virtual float GetDof() const override { return Dof; }
	virtual void SetDof(float InDof) override { Dof = InDof; }

	virtual float GetMotionBlur() const override { return MotionBlur; }
	virtual void SetMotionBlur(float InMotionBlur) override { MotionBlur = InMotionBlur; }

	virtual float GetSaturation() const override { return Saturation; }
	virtual void SetSaturation(float InSaturation) override { Saturation = InSaturation; }

	virtual float GetCameraISO() const override { return CameraISO; }
	virtual void SetCameraISO(float InCameraISO) override { CameraISO = InCameraISO; }

	virtual float GetCameraShutterSpeed() const override { return CameraShutterSpeed; }
	virtual void SetCameraShutterSpeed(float InCameraShutterSpeed) override { CameraShutterSpeed = InCameraShutterSpeed; }

	virtual float GetDepthOfFieldFstop() const override { return Fstop; }
	virtual void SetDepthOfFieldFstop( float InFstop ) override { Fstop = InFstop; }

private:
	float Temperature;
	FLinearColor ColorFilter;
	float Vignette;
	float Dof;
	float MotionBlur;
	float Saturation;
	float CameraISO;
	float CameraShutterSpeed;
	float Fstop;
};

class FDatasmithPostProcessVolumeElementImpl : public FDatasmithActorElementImpl< IDatasmithPostProcessVolumeElement >
{
public:
	FDatasmithPostProcessVolumeElementImpl( const TCHAR* InName );

	virtual const TSharedRef< IDatasmithPostProcessElement >& GetSettings() const override { return Settings; }
	virtual void SetSettings(const TSharedRef< IDatasmithPostProcessElement >& InSettings) override { Settings = InSettings; }

	virtual bool GetEnabled() const { return bEnabled; }
	virtual void SetEnabled( bool bInEnabled ) { bEnabled = bInEnabled; }

	virtual bool GetUnbound() const override { return bUnbound; }
	virtual void SetUnbound( bool bInUnbound) override { bUnbound = bInUnbound; }

private:
	TSharedRef< IDatasmithPostProcessElement > Settings;

	bool bEnabled;
	bool bUnbound;
};

class FDatasmithCameraActorElementImpl : public FDatasmithActorElementImpl< IDatasmithCameraActorElement >
{
public:
	explicit FDatasmithCameraActorElementImpl(const TCHAR* InName);

	virtual float GetSensorWidth() const override;
	virtual void SetSensorWidth(float InSensorWidth) override;

	virtual float GetSensorAspectRatio() const override;
	virtual void SetSensorAspectRatio(float InSensorAspectRatio) override;

	virtual bool GetEnableDepthOfField() const override { return bEnableDepthOfField; }
	virtual void SetEnableDepthOfField(bool bInEnableDepthOfField) override { bEnableDepthOfField = bInEnableDepthOfField; }

	virtual float GetFocusDistance() const override;
	virtual void SetFocusDistance(float InFocusDistance) override;

	virtual float GetFStop() const override;
	virtual void SetFStop(float InFStop) override;

	virtual float GetFocalLength() const override;
	virtual void SetFocalLength(float InFocalLength) override;

	virtual TSharedPtr< IDatasmithPostProcessElement >& GetPostProcess() override;
	virtual const TSharedPtr< IDatasmithPostProcessElement >& GetPostProcess() const override;
	virtual void SetPostProcess(const TSharedPtr< IDatasmithPostProcessElement >& InPostProcess) override;

	virtual const TCHAR* GetLookAtActor() const override { return *ActorName; }
	virtual void SetLookAtActor(const TCHAR* InActorName) override { ActorName = InActorName; }

	virtual bool GetLookAtAllowRoll() const override { return bLookAtAllowRoll; }
	virtual void SetLookAtAllowRoll(bool bAllow) override { bLookAtAllowRoll = bAllow; }

private:
	TSharedPtr< IDatasmithPostProcessElement > PostProcess;

	float SensorWidth;
	float SensorAspectRatio;
	bool bEnableDepthOfField;
	float FocusDistance;
	float FStop;
	float FocalLength;
	FString ActorName;
	bool bLookAtAllowRoll;
};

class DATASMITHCORE_API FDatasmithCustomActorElementImpl : public FDatasmithActorElementImpl< IDatasmithCustomActorElement >
{
public:
	explicit FDatasmithCustomActorElementImpl(const TCHAR* InName)
		: FDatasmithActorElementImpl(InName, EDatasmithElementType::CustomActor)
	{
	}

	/** The class name or path to the blueprint to instantiate. */
	virtual const TCHAR* GetClassOrPathName() const override { return *ClassOrPathName; }
	virtual void SetClassOrPathName( const TCHAR* InClassOrPathName ) override { ClassOrPathName = InClassOrPathName; }

	/** Get the total amount of properties in this actor */
	virtual int32 GetPropertiesCount() const override { return Properties.Num(); }

	/** Get the property i-th of this actor */
	virtual const TSharedPtr< IDatasmithKeyValueProperty >& GetProperty(int32 i) const override { return Properties[i]; }
	virtual TSharedPtr< IDatasmithKeyValueProperty >& GetProperty(int32 i) override { return Properties[i]; }

	/** Get a property by its name if it exists */
	virtual const TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName(const TCHAR* Name) const override;
	virtual TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName(const TCHAR* Name) override;

	/** Add a property to this actor */
	virtual void AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& Property ) override;

	/** Removes a property from this actor, doesn't preserve ordering */
	virtual void RemoveProperty( const TSharedPtr< IDatasmithKeyValueProperty >& Property ) override { Properties.RemoveSingleSwap( Property ); }

private:
	FString ClassOrPathName;

	TArray< TSharedPtr< IDatasmithKeyValueProperty > > Properties;
	TMap< FString, int > PropertyIndexMap;
};

class DATASMITHCORE_API FDatasmithLandscapeElementImpl : public FDatasmithActorElementImpl< IDatasmithLandscapeElement >
{
public:
	explicit FDatasmithLandscapeElementImpl(const TCHAR* InName)
		: FDatasmithActorElementImpl(InName, EDatasmithElementType::Landscape)
	{
		SetScale( 100.f, 100.f, 100.f );
	}

	virtual void SetHeightmap( const TCHAR* InFilePath ) override { HeightmapFilePath = InFilePath; }
	virtual const TCHAR* GetHeightmap() const override { return *HeightmapFilePath; }

	virtual void SetMaterial( const TCHAR* InMaterialPathName ) override { MaterialPathName = InMaterialPathName; }
	virtual const TCHAR* GetMaterial() const override { return *MaterialPathName; }

private:
	FString HeightmapFilePath;
	FString MaterialPathName;
};

class FDatasmithEnvironmentElementImpl : public FDatasmithLightActorElementImpl< IDatasmithEnvironmentElement >
{
public:
	explicit FDatasmithEnvironmentElementImpl(const TCHAR* InName);

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetEnvironmentComp() override;
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetEnvironmentComp() const override;

	virtual void SetEnvironmentComp(const TSharedPtr<IDatasmithCompositeTexture>& InEnvironmentComp) override;
	virtual bool GetIsIlluminationMap() const override;
	virtual void SetIsIlluminationMap(bool bInIsIlluminationMap) override;

private:
	TSharedPtr<IDatasmithCompositeTexture> EnvironmentComp;
	bool bIsIlluminationMap;
};

class FDatasmithTextureElementImpl : public FDatasmithElementImpl< IDatasmithTextureElement >
{
public:
	explicit FDatasmithTextureElementImpl(const TCHAR* InName);

	virtual FMD5Hash CalculateElementHash(bool bForce) override;

	virtual const TCHAR* GetFile() const override;
	virtual void SetFile(const TCHAR* InFile) override;

	virtual void SetData(const uint8* InData, uint32 InDataSize, EDatasmithTextureFormat InFormat) override;
	virtual const uint8* GetData(uint32& OutDataSize, EDatasmithTextureFormat& OutFormat) const override;

	virtual FMD5Hash GetFileHash() const override { return FileHash; }
	virtual void SetFileHash(FMD5Hash Hash) override { FileHash = Hash; }

	virtual EDatasmithTextureMode GetTextureMode() const override;
	virtual void SetTextureMode(EDatasmithTextureMode InMode) override;

	virtual EDatasmithTextureFilter GetTextureFilter() const override;
	virtual void SetTextureFilter(EDatasmithTextureFilter InFilter) override;

	virtual EDatasmithTextureAddress GetTextureAddressX() const override;
	virtual void SetTextureAddressX(EDatasmithTextureAddress InMode) override;

	virtual EDatasmithTextureAddress GetTextureAddressY() const override;
	virtual void SetTextureAddressY(EDatasmithTextureAddress InMode) override;

	virtual bool GetAllowResize() const override;
	virtual void SetAllowResize(bool bInAllowResize) override;

	virtual float GetRGBCurve() const override;
	virtual void SetRGBCurve(float InRGBCurve) override;

	virtual EDatasmithColorSpace GetSRGB() const override;
	virtual void SetSRGB(EDatasmithColorSpace Option) override;

private:
	FString File;
	FMD5Hash FileHash;
	float RGBCurve;
	EDatasmithColorSpace ColorSpace;
	EDatasmithTextureMode TextureMode;
	EDatasmithTextureFilter TextureFilter;
	EDatasmithTextureAddress TextureAddressX;
	EDatasmithTextureAddress TextureAddressY;
	bool bAllowResize;

	const uint8* Data;
	uint32 DataSize;
	EDatasmithTextureFormat TextureFormat;
};

class FDatasmithShaderElementImpl : public FDatasmithElementImpl< IDatasmithShaderElement >
{
public:
	explicit FDatasmithShaderElementImpl(const TCHAR* InName);

	virtual double GetIOR() const override { return IOR; }
	virtual void SetIOR(double InValue) override { IOR = InValue; }

	virtual double GetIORk() const override { return IORk; }
	virtual void SetIORk(double InValue) override { IORk = InValue; }

	virtual double GetIORRefra() const override { return IORRefra; }
	virtual void SetIORRefra(double Value) override { IORRefra = Value; }

	virtual double GetBumpAmount() const override { return BumpAmount; }
	virtual void SetBumpAmount(double InValue) override { BumpAmount = InValue; }

	virtual bool GetTwoSided() const override { return bTwoSided; }
	virtual void SetTwoSided(bool InValue) override { bTwoSided = InValue; }

	virtual FLinearColor GetDiffuseColor() const override { return DiffuseColor; }
	virtual void SetDiffuseColor(FLinearColor InValue) override { DiffuseColor = InValue; }

	virtual const TCHAR* GetDiffuseTexture() const override { return *DiffuseTexture; }
	virtual void SetDiffuseTexture(const TCHAR* InValue) override { DiffuseTexture = InValue; }

	virtual FDatasmithTextureSampler GetDiffTextureSampler() const override { return DiffSampler; }
	virtual void SetDiffTextureSampler(FDatasmithTextureSampler InValue) override { DiffSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetDiffuseComp() override { return DiffuseComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetDiffuseComp() const override { return DiffuseComp; }
	virtual void SetDiffuseComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { DiffuseComp = InValue; }

	virtual FLinearColor GetReflectanceColor() const override { return ReflectanceColor; }
	virtual void SetReflectanceColor(FLinearColor InValue) override { ReflectanceColor = InValue; }

	virtual const TCHAR* GetReflectanceTexture() const override { return *ReflectanceTexture; }
	virtual void SetReflectanceTexture(const TCHAR* InValue) override { ReflectanceTexture = InValue; }

	virtual FDatasmithTextureSampler GetRefleTextureSampler() const override { return RefleSampler; }
	virtual void SetRefleTextureSampler(FDatasmithTextureSampler InValue) override { RefleSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetRefleComp() override { return RefleComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetRefleComp() const override { return RefleComp; }
	virtual void SetRefleComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { RefleComp = InValue; }

	virtual double GetRoughness() const override { return Roughness; }
	virtual void SetRoughness(double InValue) override { Roughness = InValue; }

	virtual const TCHAR* GetRoughnessTexture() const override { return *RoughnessTexture; }
	virtual void SetRoughnessTexture(const TCHAR* InValue) override { RoughnessTexture = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetRoughnessComp() override { return RoughnessComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetRoughnessComp() const override { return RoughnessComp; }
	virtual void SetRoughnessComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { RoughnessComp = InValue; }

	virtual FDatasmithTextureSampler GetRoughTextureSampler() const override { return RoughSampler; }
	virtual void SetRoughTextureSampler(FDatasmithTextureSampler InValue) override { RoughSampler = InValue; }

	virtual const TCHAR* GetNormalTexture() const override { return *NormalTexture; }
	virtual void SetNormalTexture(const TCHAR* InValue) override { NormalTexture = InValue; }

	virtual FDatasmithTextureSampler GetNormalTextureSampler() const override { return NormalSampler; }
	virtual void SetNormalTextureSampler(FDatasmithTextureSampler InValue) override { NormalSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetNormalComp() override { return NormalComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetNormalComp() const override { return NormalComp; }
	virtual void SetNormalComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { NormalComp = InValue; }

	virtual const TCHAR* GetBumpTexture() const override { return *BumpTexture; }
	virtual void SetBumpTexture(const TCHAR* Value) override { BumpTexture = Value; }

	virtual FDatasmithTextureSampler GetBumpTextureSampler() const override { return BumpSampler; }
	virtual void SetBumpTextureSampler(FDatasmithTextureSampler InValue) override { BumpSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetBumpComp() override { return BumpComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetBumpComp() const override { return BumpComp; }
	virtual void SetBumpComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { BumpComp = InValue; }

	virtual FLinearColor GetTransparencyColor() const override { return TransparencyColor; }
	virtual void SetTransparencyColor(FLinearColor InValue) override { TransparencyColor = InValue; }

	virtual const TCHAR* GetTransparencyTexture() const override { return *TransparencyTexture; }
	virtual void SetTransparencyTexture(const TCHAR* InValue) override { TransparencyTexture = InValue; }

	virtual FDatasmithTextureSampler GetTransTextureSampler() const override { return TransSampler; }
	virtual void SetTransTextureSampler(FDatasmithTextureSampler InValue) override { TransSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetTransComp() override { return TransComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetTransComp() const override { return TransComp; }
	virtual void SetTransComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { TransComp = InValue; }

	virtual const TCHAR* GetMaskTexture() const override { return *MaskTexture; }
	virtual void SetMaskTexture(const TCHAR* InValue) override { MaskTexture = InValue; }

	virtual FDatasmithTextureSampler GetMaskTextureSampler() const override { return MaskSampler; }
	virtual void SetMaskTextureSampler(FDatasmithTextureSampler InValue) override { MaskSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetMaskComp() override { return MaskComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetMaskComp() const override { return MaskComp; }
	virtual void SetMaskComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { MaskComp = InValue; }

	virtual const TCHAR* GetDisplaceTexture() const override { return *DisplaceTexture; }
	virtual void SetDisplaceTexture(const TCHAR* InValue) override { DisplaceTexture = InValue; }

	virtual FDatasmithTextureSampler GetDisplaceTextureSampler() const override { return DisplaceSampler; }
	virtual void SetDisplaceTextureSampler(FDatasmithTextureSampler InValue) override { DisplaceSampler = InValue; }

	virtual double GetDisplace() const override { return Displace; }
	virtual void SetDisplace(double InValue) override { Displace = InValue; }

	virtual double GetDisplaceSubDivision() const override { return DisplaceSubDivision; }
	virtual void SetDisplaceSubDivision(double InValue) override { DisplaceSubDivision = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetDisplaceComp() override { return DisplaceComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetDisplaceComp() const override { return DisplaceComp; }
	virtual void SetDisplaceComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { DisplaceComp = InValue; }

	virtual double GetMetal() const override { return Metal; }
	virtual void SetMetal(double InValue) override { Metal = InValue; }

	virtual const TCHAR* GetMetalTexture() const override { return *MetalTexture; }
	virtual void SetMetalTexture(const TCHAR* InValue) override { MetalTexture = InValue; }

	virtual FDatasmithTextureSampler GetMetalTextureSampler() const override { return MetalSampler; }
	virtual void SetMetalTextureSampler(FDatasmithTextureSampler InValue) override { MetalSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetMetalComp() override { return MetalComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetMetalComp() const override { return MetalComp; }
	virtual void SetMetalComp(const TSharedPtr<IDatasmithCompositeTexture>& Value) override { MetalComp = Value; }

	virtual const TCHAR* GetEmitTexture() const override { return *EmitTexture; }
	virtual void SetEmitTexture(const TCHAR* InValue) override { EmitTexture = InValue; }

	virtual FDatasmithTextureSampler GetEmitTextureSampler() const override { return EmitSampler; }
	virtual void SetEmitTextureSampler(FDatasmithTextureSampler InValue) override { EmitSampler = InValue; }

	virtual FLinearColor GetEmitColor() const override { return EmitColor; }
	virtual void SetEmitColor(FLinearColor InValue) override { EmitColor = InValue; }

	virtual double GetEmitTemperature() const override { return EmitTemperature; }
	virtual void SetEmitTemperature(double InValue) override { EmitTemperature = InValue; }

	virtual double GetEmitPower() const override { return EmitPower; }
	virtual void SetEmitPower(double InValue) override { EmitPower = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetEmitComp() override { return EmitComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetEmitComp() const override { return EmitComp; }
	virtual void SetEmitComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { EmitComp = InValue; }

	virtual bool GetLightOnly() const override { return bLightOnly; }
	virtual void SetLightOnly(bool InValue) override { bLightOnly = InValue; }

	virtual FLinearColor GetWeightColor() const override { return WeightColor; }
	virtual void SetWeightColor(FLinearColor InValue) override { WeightColor = InValue; }

	virtual const TCHAR* GetWeightTexture() const override { return *WeightTexture; }
	virtual void SetWeightTexture(const TCHAR* InValue) override { WeightTexture = InValue; }

	virtual FDatasmithTextureSampler GetWeightTextureSampler() const override { return WeightSampler; }
	virtual void SetWeightTextureSampler(FDatasmithTextureSampler InValue) override { WeightSampler = InValue; }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetWeightComp() override { return WeightComp; }
	virtual const TSharedPtr<IDatasmithCompositeTexture>& GetWeightComp() const override { return WeightComp; }
	virtual void SetWeightComp(const TSharedPtr<IDatasmithCompositeTexture>& InValue) override { WeightComp = InValue; }

	virtual double GetWeightValue() const override { return WeightValue; }
	virtual void SetWeightValue(double InValue) override { WeightValue = InValue; }

	virtual EDatasmithBlendMode GetBlendMode() const override { return BlendMode; }
	virtual void SetBlendMode(EDatasmithBlendMode InValue) override { BlendMode = InValue; }

	virtual bool GetIsStackedLayer() const override { return bIsStackedLayer; }
	virtual void SetIsStackedLayer(bool InValue) override { bIsStackedLayer = InValue; }

	virtual const EDatasmithShaderUsage GetShaderUsage() const override { return ShaderUsage; }
	virtual void SetShaderUsage(EDatasmithShaderUsage InShaderUsage) override { ShaderUsage = InShaderUsage; };

	virtual const bool GetUseEmissiveForDynamicAreaLighting() const override { return bUseEmissiveForDynamicAreaLighting; }
	virtual void SetUseEmissiveForDynamicAreaLighting(bool InUseEmissiveForDynamicAreaLighting) override {	bUseEmissiveForDynamicAreaLighting = InUseEmissiveForDynamicAreaLighting; };

private:
	double IOR;
	double IORk;
	double IORRefra;

	double BumpAmount;
	bool bTwoSided;

	FLinearColor DiffuseColor;
	FString DiffuseTexture;
	FDatasmithTextureSampler DiffSampler;
	TSharedPtr<IDatasmithCompositeTexture> DiffuseComp;

	FLinearColor ReflectanceColor;
	FString ReflectanceTexture;
	FDatasmithTextureSampler RefleSampler;
	TSharedPtr<IDatasmithCompositeTexture> RefleComp;

	double Roughness;
	FString RoughnessTexture;
	FDatasmithTextureSampler RoughSampler;
	TSharedPtr<IDatasmithCompositeTexture> RoughnessComp;

	FString NormalTexture;
	FDatasmithTextureSampler NormalSampler;
	TSharedPtr<IDatasmithCompositeTexture> NormalComp;

	FString BumpTexture;
	FDatasmithTextureSampler BumpSampler;
	TSharedPtr<IDatasmithCompositeTexture> BumpComp;

	FLinearColor TransparencyColor;
	FString TransparencyTexture;
	FDatasmithTextureSampler TransSampler;
	TSharedPtr<IDatasmithCompositeTexture> TransComp;

	FString MaskTexture;
	FDatasmithTextureSampler MaskSampler;
	TSharedPtr<IDatasmithCompositeTexture> MaskComp;

	FString DisplaceTexture;
	FDatasmithTextureSampler DisplaceSampler;
	double Displace;
	double DisplaceSubDivision;
	TSharedPtr<IDatasmithCompositeTexture> DisplaceComp;

	double Metal;
	FString MetalTexture;
	FDatasmithTextureSampler MetalSampler;
	TSharedPtr<IDatasmithCompositeTexture> MetalComp;

	FString EmitTexture;
	FDatasmithTextureSampler EmitSampler;
	FLinearColor EmitColor;
	double EmitTemperature;
	double EmitPower;
	TSharedPtr<IDatasmithCompositeTexture> EmitComp;

	bool bLightOnly;

	FLinearColor WeightColor;
	FString WeightTexture;
	FDatasmithTextureSampler WeightSampler;
	TSharedPtr<IDatasmithCompositeTexture> WeightComp;
	double WeightValue;

	EDatasmithBlendMode BlendMode;
	bool bIsStackedLayer;

	EDatasmithShaderUsage ShaderUsage;
	bool bUseEmissiveForDynamicAreaLighting;
};

template< typename InterfaceType >
class FDatasmithBaseMaterialElementImpl : public FDatasmithElementImpl< InterfaceType >
{
public:
	explicit FDatasmithBaseMaterialElementImpl(const TCHAR* InName, EDatasmithElementType ChildType);
};

template< typename T >
inline FDatasmithBaseMaterialElementImpl<T>::FDatasmithBaseMaterialElementImpl(const TCHAR* InName, EDatasmithElementType ChildType)
	: FDatasmithElementImpl<T>(InName, EDatasmithElementType::BaseMaterial | ChildType)
{
}

class FDatasmithMaterialElementImpl : public FDatasmithBaseMaterialElementImpl< IDatasmithMaterialElement >
{
public:
	explicit FDatasmithMaterialElementImpl(const TCHAR* InName);

	virtual bool IsSingleShaderMaterial() const override;
	virtual bool IsClearCoatMaterial() const override;

	virtual void AddShader(const TSharedPtr< IDatasmithShaderElement >& InShader) override;

	virtual int32 GetShadersCount() const override;
	virtual TSharedPtr< IDatasmithShaderElement >& GetShader(int32 InIndex) override;
	virtual const TSharedPtr< IDatasmithShaderElement >& GetShader(int32 InIndex) const override;

private:
	TArray< TSharedPtr< IDatasmithShaderElement > > Shaders;
};

class FDatasmithMasterMaterialElementImpl : public FDatasmithBaseMaterialElementImpl< IDatasmithMasterMaterialElement >
{
public:
	FDatasmithMasterMaterialElementImpl(const TCHAR* InName);

	virtual EDatasmithMasterMaterialType GetMaterialType() const { return MaterialType; }
	virtual void SetMaterialType( EDatasmithMasterMaterialType InType ) override { MaterialType = InType; }

	virtual EDatasmithMasterMaterialQuality GetQuality() const { return Quality; }
	virtual void SetQuality( EDatasmithMasterMaterialQuality InQuality ) { Quality = InQuality; }

	virtual const TCHAR* GetCustomMaterialPathName() const { return *CustomMaterialPathName; }
	virtual void SetCustomMaterialPathName( const TCHAR* InPathName ){ CustomMaterialPathName = InPathName; }

	int32 GetPropertiesCount() const override { return Properties.Num(); }

	const TSharedPtr< IDatasmithKeyValueProperty >& GetProperty( int32 InIndex ) const override;
	TSharedPtr< IDatasmithKeyValueProperty >& GetProperty( int32 InIndex ) override;

	const TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName( const TCHAR* InName ) const override;
	TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName( const TCHAR* InName ) override;

	void AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty ) override;

private:
	TArray< TSharedPtr< IDatasmithKeyValueProperty > > Properties;
	TMap< FString, int > PropertyIndexMap;

	EDatasmithMasterMaterialType MaterialType;
	EDatasmithMasterMaterialQuality Quality;

	FString CustomMaterialPathName;
};

class FDatasmithCompositeSurface
{
public:
	FDatasmithCompositeSurface(const TSharedPtr<IDatasmithCompositeTexture>& SubComp);
	FDatasmithCompositeSurface(const TCHAR* InTexture, FDatasmithTextureSampler InTexUV);
	FDatasmithCompositeSurface(const FLinearColor& InColor);

	bool GetUseTexture() const;
	bool GetUseColor() const;
	bool GetUseComposite() const;

	FDatasmithTextureSampler& GetParamTextureSampler();
	const TCHAR* GetParamTexture() const;
	void SetParamTexture(const TCHAR* InTexture);
	const FLinearColor& GetParamColor() const;
	TSharedPtr<IDatasmithCompositeTexture>& GetParamSubComposite();

private:
	FDatasmithTextureSampler ParamSampler;
	FString ParamTextures;
	FLinearColor ParamColor;
	TSharedPtr<IDatasmithCompositeTexture> ParamSubComposite;
	bool bParamUseTexture;
};

class FDatasmithCompositeTextureImpl : public IDatasmithCompositeTexture
{
public:
	FDatasmithCompositeTextureImpl();

	virtual bool IsValid() const override;

	virtual EDatasmithCompMode GetMode() const override { return CompMode; }
	virtual void SetMode(EDatasmithCompMode InMode) override { CompMode = InMode; }
	virtual int32 GetParamSurfacesCount() const override { return ParamSurfaces.Num(); }

	virtual bool GetUseTexture(int32 InIndex) override;

	virtual const TCHAR* GetParamTexture(int32 InIndex) override;
	virtual void SetParamTexture(int32 InIndex, const TCHAR* InTexture) override;

	virtual FDatasmithTextureSampler& GetParamTextureSampler(int32 InIndex) override;

	virtual bool GetUseColor(int32 InIndex) override;
	virtual const FLinearColor& GetParamColor(int32 InIndex) override;

	virtual bool GetUseComposite(int32 InIndex) override;

	virtual int32 GetParamVal1Count() const override { return ParamVal1.Num(); }
	virtual ParamVal GetParamVal1(int32 InIndex) const override;
	virtual void AddParamVal1(ParamVal InParamVal) override { ParamVal1.Add( ParamValImpl( InParamVal.Key, InParamVal.Value ) ); }

	virtual int32 GetParamVal2Count() const override { return ParamVal2.Num(); }
	virtual ParamVal GetParamVal2(int32 InIndex) const override;
	virtual void AddParamVal2(ParamVal InParamVal) override { ParamVal2.Add( ParamValImpl( InParamVal.Key, InParamVal.Value ) ); }

	virtual int32 GetParamMaskSurfacesCount() const override { return ParamMaskSurfaces.Num(); }
	virtual const TCHAR* GetParamMask(int32 InIndex) override;
	virtual const FLinearColor& GetParamMaskColor(int32 i) const override;
	virtual bool GetMaskUseComposite(int32 InIndex) const override;
	virtual void AddMaskSurface(const TCHAR* InMask, const FDatasmithTextureSampler InMaskSampler) override { ParamMaskSurfaces.Add( FDatasmithCompositeSurface( InMask, InMaskSampler )); }
	virtual void AddMaskSurface(const FLinearColor& InColor) override { ParamMaskSurfaces.Add( FDatasmithCompositeSurface( InColor ) ); }

	virtual FDatasmithTextureSampler GetParamMaskTextureSampler(int32 InIndex) override;

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetParamSubComposite(int32 InIndex) override;
	virtual void AddSurface(const TSharedPtr<IDatasmithCompositeTexture>& SubComp) override { ParamSurfaces.Add( FDatasmithCompositeSurface( SubComp ) ); }

	virtual TSharedPtr<IDatasmithCompositeTexture>& GetParamMaskSubComposite(int32 InIndex) override;
	virtual void AddMaskSurface(const TSharedPtr<IDatasmithCompositeTexture>& InMaskSubComp) override { ParamMaskSurfaces.Add( FDatasmithCompositeSurface( InMaskSubComp ) ); }

	virtual const TCHAR* GetBaseTextureName() const override { return *BaseTexName; }
	virtual const TCHAR* GetBaseColName() const override { return *BaseColName; }
	virtual const TCHAR* GetBaseValName() const override { return *BaseValName; }
	virtual const TCHAR* GetBaseCompName() const override { return *BaseCompName; }

	virtual void SetBaseNames(const TCHAR* InTextureName, const TCHAR* InColorName, const TCHAR* InValueName, const TCHAR* InCompName) override;

	virtual void AddSurface(const TCHAR* InTexture, FDatasmithTextureSampler InTexUV) override { ParamSurfaces.Add( FDatasmithCompositeSurface( InTexture, InTexUV )); }
	virtual void AddSurface(const FLinearColor& InColor) override { ParamSurfaces.Add( FDatasmithCompositeSurface( InColor )); }
	virtual void ClearSurface() override
	{
		ParamSurfaces.Empty();
	}

private:
	TArray<FDatasmithCompositeSurface> ParamSurfaces;
	TArray<FDatasmithCompositeSurface> ParamMaskSurfaces;

	typedef TPair<float, FString> ParamValImpl;
	TArray<ParamValImpl> ParamVal1;
	TArray<ParamValImpl> ParamVal2;

	EDatasmithCompMode CompMode;

	// used for single material
	FString BaseTexName;
	FString BaseColName;
	FString BaseValName;
	FString BaseCompName;
};

class DATASMITHCORE_API FDatasmithMetaDataElementImpl : public FDatasmithElementImpl< IDatasmithMetaDataElement >
{
public:
	explicit FDatasmithMetaDataElementImpl(const TCHAR* InName);

	virtual const TSharedPtr< IDatasmithElement >& GetAssociatedElement() const override { return AssociatedElement; }
	virtual void SetAssociatedElement(const TSharedPtr< IDatasmithElement >& Element) { AssociatedElement = Element; }

	int32 GetPropertiesCount() const override { return Properties.Num(); }

	virtual const TSharedPtr< IDatasmithKeyValueProperty >& GetProperty(int32 i) const override { return Properties[i]; }
	virtual TSharedPtr< IDatasmithKeyValueProperty >& GetProperty(int32 i) override { return Properties[i]; }

	const TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName( const TCHAR* InName ) const override;
	TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName( const TCHAR* InName ) override;

	virtual void AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& Property ) override;

private:
	TSharedPtr< IDatasmithElement > AssociatedElement;
	TArray< TSharedPtr< IDatasmithKeyValueProperty > > Properties;
	TMap< FString, int > PropertyIndexMap;
};

class DATASMITHCORE_API FDatasmithSceneImpl : public FDatasmithElementImpl< IDatasmithScene >
{
public:
	explicit FDatasmithSceneImpl(const TCHAR* InName);

	virtual void Reset() override;

	virtual const TCHAR* GetHost() const
	{
		return *Hostname;
	}

	virtual void SetHost(const TCHAR* InHostname)
	{
		Hostname = InHostname;
	}

	virtual const TCHAR* GetExporterVersion() const override { return *ExporterVersion; }
	virtual void SetExporterVersion(const TCHAR* InVersion) override { ExporterVersion = InVersion; }

	virtual const TCHAR* GetExporterSDKVersion() const override { return *ExporterSDKVersion; }
	virtual void SetExporterSDKVersion(const TCHAR* InVersion) override { ExporterSDKVersion = InVersion; }

	virtual const TCHAR* GetVendor() const override	{ return *Vendor; }
	virtual void SetVendor(const TCHAR* InVendor) override { Vendor = InVendor; }

	virtual const TCHAR* GetProductName() const override { return *ProductName;	}
	virtual void SetProductName(const TCHAR* InProductName) override { ProductName = InProductName;	}

	virtual const TCHAR* GetProductVersion() const override	{ return *ProductVersion; }
	virtual void SetProductVersion(const TCHAR* InProductVersion) override { ProductVersion = InProductVersion;	}

	virtual const TCHAR* GetUserID() const override { return *UserID; }
	virtual void SetUserID(const TCHAR* InUserID) override { UserID = InUserID; }

	virtual const TCHAR* GetUserOS() const override { return *UserOS; }
	virtual void SetUserOS(const TCHAR* InUserOS) override { UserOS = InUserOS; }

	virtual int32 GetExportDuration() const override { return ExportDuration; }
	virtual void SetExportDuration(int32 InExportDuration) override { ExportDuration = InExportDuration; }

	virtual void AddMesh(const TSharedPtr< IDatasmithMeshElement >& InMesh) override { Meshes.Add(InMesh); }
	virtual int32 GetMeshesCount() const override { return Meshes.Num(); }
	virtual TSharedPtr< IDatasmithMeshElement > GetMesh(int32 InIndex) override;
	virtual const TSharedPtr< IDatasmithMeshElement >& GetMesh(int32 InIndex) const override;
	virtual void RemoveMesh(const TSharedPtr< IDatasmithMeshElement >& InMesh) override { Meshes.Remove(InMesh); }
	virtual void EmptyMeshes() override { Meshes.Empty(); }

	virtual void AddActor(const TSharedPtr< IDatasmithActorElement >& InActor) override { Actors.Add(InActor);  }
	virtual int32 GetActorsCount() const override { return Actors.Num(); }
	virtual TSharedPtr< IDatasmithActorElement > GetActor(int32 InIndex) override { return Actors[InIndex]; }
	virtual const TSharedPtr< IDatasmithActorElement >& GetActor(int32 InIndex) const override { return Actors[InIndex]; }
	virtual void RemoveActor(const TSharedPtr< IDatasmithActorElement >& InActor, EDatasmithActorRemovalRule RemoveRule) override;

	virtual void AddMaterial(const TSharedPtr< IDatasmithBaseMaterialElement >& InMaterial) override { Materials.Add(InMaterial); }
	virtual int32 GetMaterialsCount() const override { return Materials.Num(); }
	virtual TSharedPtr< IDatasmithBaseMaterialElement > GetMaterial(int32 InIndex) override { return Materials[InIndex]; }
	virtual const TSharedPtr< IDatasmithBaseMaterialElement >& GetMaterial(int32 InIndex) const override { return Materials[InIndex]; }
	virtual void RemoveMaterial(const TSharedPtr< IDatasmithBaseMaterialElement >& InMaterial) override { Materials.Remove(InMaterial); }
	virtual void EmptyMaterials() override { Materials.Empty(); }

	virtual void AddTexture(const TSharedPtr< IDatasmithTextureElement >& InTexture) override { Textures.Add(InTexture); }
	virtual int32 GetTexturesCount() const override { return Textures.Num(); }
	virtual TSharedPtr< IDatasmithTextureElement > GetTexture(int32 InIndex) override { return Textures[InIndex]; }
	virtual const TSharedPtr< IDatasmithTextureElement >& GetTexture(int32 InIndex) const override { return Textures[InIndex]; }
	virtual void RemoveTexture(const TSharedPtr< IDatasmithTextureElement >& InTexture) override { Textures.Remove(InTexture); }
	virtual void EmptyTextures() override { Textures.Empty(); }

	virtual void SetPostProcess(const TSharedPtr< IDatasmithPostProcessElement >& InPostProcess) override { PostProcess = InPostProcess; }
	virtual TSharedPtr< IDatasmithPostProcessElement > GetPostProcess() override { return PostProcess; }
	virtual const TSharedPtr< IDatasmithPostProcessElement >& GetPostProcess() const override { return PostProcess; }

	virtual void SetUsePhysicalSky(bool bInUsePhysicalSky) override { bUseSky = bInUsePhysicalSky; }
	virtual bool GetUsePhysicalSky() const override { return bUseSky; }

	virtual void AddLODScreenSize( float ScreenSize ) override { LODScreenSizes.Add( FMath::Clamp( ScreenSize, 0.f, 1.f ) ); }
	virtual int32 GetLODScreenSizesCount() const override { return LODScreenSizes.Num(); }
	virtual float GetLODScreenSize(int32 InIndex) const override { return LODScreenSizes.IsValidIndex( InIndex ) ? LODScreenSizes[InIndex] : 0.f; }

	virtual void AddMetaData(const TSharedPtr< IDatasmithMetaDataElement >& InMetaData) override { MetaData.Add(InMetaData); ElementToMetaDataMap.Add(InMetaData->GetAssociatedElement(), InMetaData); }

	virtual int32 GetMetaDataCount() const override { return MetaData.Num(); }
	virtual TSharedPtr< IDatasmithMetaDataElement > GetMetaData(int32 InIndex) override;
	virtual const TSharedPtr< IDatasmithMetaDataElement >& GetMetaData(int32 InIndex) const override;
	virtual TSharedPtr< IDatasmithMetaDataElement > GetMetaData(const TSharedPtr<IDatasmithElement>& Element) override;
	virtual const TSharedPtr< IDatasmithMetaDataElement >& GetMetaData(const TSharedPtr<IDatasmithElement>& Element) const override;

	virtual void AddLevelSequence(const TSharedRef< IDatasmithLevelSequenceElement >& InSequence) override { LevelSequences.Add(InSequence);  }
	virtual int32 GetLevelSequencesCount() const override { return LevelSequences.Num(); }
	virtual TSharedPtr< IDatasmithLevelSequenceElement > GetLevelSequence(int32 InIndex) override
	{
		return LevelSequences.IsValidIndex(InIndex) ? LevelSequences[InIndex] : TSharedPtr< IDatasmithLevelSequenceElement >();
	}
	virtual void RemoveLevelSequence(const TSharedRef< IDatasmithLevelSequenceElement >& InSequence) override { LevelSequences.Remove(InSequence); }

	virtual void AddLevelVariantSets(const TSharedPtr< IDatasmithLevelVariantSetsElement >& InLevelVariantSets) override { LevelVariantSets.Add(InLevelVariantSets);  }
	virtual int32 GetLevelVariantSetsCount() const override { return LevelVariantSets.Num(); }
	virtual TSharedPtr< IDatasmithLevelVariantSetsElement > GetLevelVariantSets(int32 InIndex) override
	{
		return LevelVariantSets.IsValidIndex(InIndex) ? LevelVariantSets[InIndex] : TSharedPtr< IDatasmithLevelVariantSetsElement >();
	}
	virtual void RemoveLevelVariantSets(const TSharedPtr< IDatasmithLevelVariantSetsElement >& InLevelVariantSets) override { LevelVariantSets.Remove(InLevelVariantSets); }

	virtual void AttachActor(const TSharedPtr< IDatasmithActorElement >& NewParent, const TSharedPtr< IDatasmithActorElement >& Child, EDatasmithActorAttachmentRule AttachmentRule) override;
	virtual void AttachActorToSceneRoot(const TSharedPtr< IDatasmithActorElement >& Child, EDatasmithActorAttachmentRule AttachmentRule) override;

private:
	TArray< TSharedPtr< IDatasmithActorElement > >				Actors;
	TArray< TSharedPtr< IDatasmithMeshElement > >				Meshes;
	TArray< TSharedPtr< IDatasmithBaseMaterialElement > >		Materials;
	TArray< TSharedPtr< IDatasmithTextureElement > >			Textures;
	TArray< TSharedPtr< IDatasmithMetaDataElement > >			MetaData;
	TArray< TSharedRef< IDatasmithLevelSequenceElement > >		LevelSequences;
	TArray< TSharedPtr< IDatasmithLevelVariantSetsElement > >	LevelVariantSets;
	TArray< float >												LODScreenSizes;
	TSharedPtr< IDatasmithPostProcessElement >					PostProcess;
	TMap< TSharedPtr< IDatasmithElement >, TSharedPtr< IDatasmithMetaDataElement> > ElementToMetaDataMap;

	FString Hostname;
	FString ExporterVersion;
	FString ExporterSDKVersion;
	FString Vendor;
	FString ProductName;
	FString ProductVersion;
	FString UserID;
	FString UserOS;

	uint32 ExportDuration;

	bool bUseSky;
};
