// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialFactory.h"

#include "GLTFMapFactory.h"
#include "GLTFMaterialExpressions.h"

#include "GLTFAsset.h"

#include "Engine/EngineTypes.h"

namespace GLTF
{
	namespace
	{
		const GLTF::FTexture& GetTexture(const GLTF::FTextureMap& Map, const TArray<GLTF::FTexture>& Textures)
		{
			static const GLTF::FImage   Immage;
			static const GLTF::FTexture None(FString(), Immage, GLTF::FSampler::DefaultSampler);
			return Map.TextureIndex != INDEX_NONE ? Textures[Map.TextureIndex] : None;
		}
	}

	class FMaterialFactoryImpl : public GLTF::FBaseLogger
	{
	public:
		FMaterialFactoryImpl(IMaterialElementFactory* MaterialElementFactory, ITextureFactory* TextureFactory)
		    : MaterialElementFactory(MaterialElementFactory)
		    , TextureFactory(TextureFactory)
		{
			check(MaterialElementFactory);
		}

		const TArray<FMaterialElement*>& CreateMaterials(const GLTF::FAsset& Asset, UObject* ParentPackage, EObjectFlags Flags);

	private:
		void HandleOpacity(const TArray<GLTF::FTexture>& Texture, const GLTF::FMaterial& GLTFMaterial, FMaterialElement& MaterialElement);
		void HandleShadingModel(const TArray<GLTF::FTexture>& Texture, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement);
		void HandleOcclusion(const TArray<GLTF::FTexture>& Texture, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement);
		void HandleEmissive(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement);
		void HandleNormal(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement);


	private:
		TUniquePtr<IMaterialElementFactory> MaterialElementFactory;
		TUniquePtr<ITextureFactory>         TextureFactory;
		TArray<FMaterialElement*>           Materials;

		friend class FMaterialFactory;
	};

	namespace
	{
		EBlendMode ConvertAlphaMode(FMaterial::EAlphaMode Mode)
		{
			switch (Mode)
			{
				case FMaterial::EAlphaMode::Opaque:
					return EBlendMode::BLEND_Opaque;
				case FMaterial::EAlphaMode::Blend:
					return EBlendMode::BLEND_Translucent;
				case FMaterial::EAlphaMode::Mask:
					return EBlendMode::BLEND_Masked;
				default:
					return EBlendMode::BLEND_Opaque;
			}
		}

		template <class ReturnClass>
		ReturnClass* FindExpression(const FString& Name, FMaterialElement& MaterialElement)
		{
			ReturnClass* Result = nullptr;
			for (int32 Index = 0; Index < MaterialElement.GetExpressionsCount(); ++Index)
			{
				FMaterialExpression* Expression = MaterialElement.GetExpression(Index);
				if (Expression->GetType() != EMaterialExpressionType::ConstantColor &&
				    Expression->GetType() != EMaterialExpressionType::ConstantScalar && Expression->GetType() != EMaterialExpressionType::Texture)
					continue;

				FMaterialExpressionParameter* ExpressionParameter = static_cast<FMaterialExpressionParameter*>(Expression);
				if (ExpressionParameter->GetName() == Name)
				{
					Result = static_cast<ReturnClass*>(ExpressionParameter);
					check(Expression->GetType() == (EMaterialExpressionType)ReturnClass::Type);
					break;
				}
			}
			return Result;
		}

	}

	const TArray<FMaterialElement*>& FMaterialFactoryImpl::CreateMaterials(const GLTF::FAsset& Asset, UObject* ParentPackage, EObjectFlags Flags)
	{
		TextureFactory->CleanUp();
		Materials.Empty();
		Materials.Reserve(Asset.Materials.Num() + 1);

		Messages.Empty();

		FPBRMapFactory MapFactory(*TextureFactory);
		MapFactory.SetParentPackage(ParentPackage, Flags);

		for (const GLTF::FMaterial& GLTFMaterial : Asset.Materials)
		{
			check(!GLTFMaterial.Name.IsEmpty());

			FMaterialElement* MaterialElement = MaterialElementFactory->CreateMaterial(*GLTFMaterial.Name, ParentPackage, Flags);
			MaterialElement->SetTwoSided(GLTFMaterial.bIsDoubleSided);
			MaterialElement->SetBlendMode(ConvertAlphaMode(GLTFMaterial.AlphaMode));

			MapFactory.CurrentMaterialElement = MaterialElement;

			HandleShadingModel(Asset.Textures, GLTFMaterial, MapFactory, *MaterialElement);
			HandleOpacity(Asset.Textures, GLTFMaterial, *MaterialElement);

			// Additional maps
			HandleOcclusion(Asset.Textures, GLTFMaterial, MapFactory, *MaterialElement);
			HandleEmissive(Asset.Textures, GLTFMaterial, MapFactory, *MaterialElement);
			HandleNormal(Asset.Textures, GLTFMaterial, MapFactory, *MaterialElement);

			MaterialElement->SetGLTFMaterialHash(GLTFMaterial.GetHash());
            MaterialElement->Finalize();
			Materials.Add(MaterialElement);
		}

		return Materials;
	}

	void FMaterialFactoryImpl::HandleOpacity(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FMaterialElement& MaterialElement)
	{
		if (GLTFMaterial.IsOpaque())
		{
			return;
		}

		const TCHAR* GroupName = TEXT("Opacity");

		FMaterialExpressionTexture* BaseColorMap = FindExpression<FMaterialExpressionTexture>(TEXT("BaseColor Map"), MaterialElement);
		switch (GLTFMaterial.AlphaMode)
		{
		case FMaterial::EAlphaMode::Mask:
		{
			FMaterialExpressionColor* BaseColorFactor = FindExpression<FMaterialExpressionColor>(TEXT("BaseColor"), MaterialElement);

			FMaterialExpressionGeneric* MultiplyExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionGeneric>();
			MultiplyExpression->SetExpressionName(TEXT("Multiply"));
			BaseColorFactor->ConnectExpression(*MultiplyExpression->GetInput(1), (int)FPBRMapFactory::EChannel::Alpha);
			BaseColorMap->ConnectExpression(*MultiplyExpression->GetInput(0), (int)FPBRMapFactory::EChannel::Alpha);

			FMaterialExpressionFunctionCall* CuttofExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionFunctionCall>();
			CuttofExpression->SetFunctionPathName(TEXT("/Engine/Functions/Engine_MaterialFunctions02/SmoothStep.SmoothStep"));

			FMaterialExpressionScalar* ValueExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionScalar>();
			ValueExpression->SetName(TEXT("Alpha Cuttof"));
			ValueExpression->SetGroupName(GroupName);
			ValueExpression->GetScalar() = GLTFMaterial.AlphaCutoff;

			MultiplyExpression->ConnectExpression(*CuttofExpression->GetInput(0), 0);
			ValueExpression->ConnectExpression(*CuttofExpression->GetInput(1), 0);
			ValueExpression->ConnectExpression(*CuttofExpression->GetInput(2), 0);

			CuttofExpression->ConnectExpression(MaterialElement.GetOpacity(), 0);
			break;
		}
		case FMaterial::EAlphaMode::Blend:
		{
			FMaterialExpressionScalar* ValueExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionScalar>();
			ValueExpression->SetName(TEXT("IOR"));
			ValueExpression->SetGroupName(GroupName);
			ValueExpression->GetScalar() = 1.f;
			ValueExpression->ConnectExpression(MaterialElement.GetRefraction(), 0);

			FMaterialExpressionColor* BaseColorFactor = FindExpression<FMaterialExpressionColor>(TEXT("BaseColor"), MaterialElement);
			if (BaseColorMap)
			{
				FMaterialExpressionGeneric* MultiplyExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionGeneric>();
				MultiplyExpression->SetExpressionName(TEXT("Multiply"));
				BaseColorFactor->ConnectExpression(*MultiplyExpression->GetInput(1), (int)FPBRMapFactory::EChannel::Alpha);
				BaseColorMap->ConnectExpression(*MultiplyExpression->GetInput(0), (int)FPBRMapFactory::EChannel::Alpha);
				MultiplyExpression->ConnectExpression(MaterialElement.GetOpacity(), 0);
			}
			else
			{
				BaseColorFactor->ConnectExpression(MaterialElement.GetOpacity(), (int)FPBRMapFactory::EChannel::Alpha);
			}
			break;
		}
		default:
			check(false);
			break;
		}
	}

	void FMaterialFactoryImpl::HandleShadingModel(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement)
	{
		MapFactory.GroupName = TEXT("GGX");

		TArray<FPBRMapFactory::FMapChannel, TFixedAllocator<4>> Maps;
		if (GLTFMaterial.ShadingModel == FMaterial::EShadingModel::MetallicRoughness)
		{
			// Base Color
			MapFactory.GroupName = TEXT("Base Color");
			MapFactory.CreateColorMap(GetTexture(GLTFMaterial.BaseColor, Textures),
									  GLTFMaterial.BaseColor.TexCoord,
									  GLTFMaterial.BaseColorFactor,
									  TEXT("BaseColor"),
									  nullptr,
									  ETextureMode::Color,
									  MaterialElement.GetBaseColor());

			// Metallic
			Maps.Emplace(GLTFMaterial.MetallicRoughness.MetallicFactor,
						 TEXT("Metallic Factor"),
						 FPBRMapFactory::EChannel::Blue,
						 &MaterialElement.GetMetallic(),
						 nullptr);

			// Roughness
			Maps.Emplace(GLTFMaterial.MetallicRoughness.RoughnessFactor,
						 TEXT("Roughness Factor"),
						 FPBRMapFactory::EChannel::Green,
						 &MaterialElement.GetRoughness(),
						 nullptr);

			MapFactory.CreateMultiMap(GetTexture(GLTFMaterial.MetallicRoughness.Map, Textures),
									  GLTFMaterial.MetallicRoughness.Map.TexCoord,
				                      TEXT("MetallicRoughness"),
									  Maps.GetData(),
									  Maps.Num(),
									  ETextureMode::Grayscale);
		}
		else if (GLTFMaterial.ShadingModel == FMaterial::EShadingModel::SpecularGlossiness)
		{
			// We'll actually just convert it into MetalRoughness in the material graph
			FMaterialExpressionFunctionCall* SpecGlossToMetalRough = MaterialElement.AddMaterialExpression<FMaterialExpressionFunctionCall>();
			SpecGlossToMetalRough->SetFunctionPathName(TEXT("/GLTFImporter/SpecGlossToMetalRoughness.SpecGlossToMetalRoughness"));
			SpecGlossToMetalRough->ConnectExpression(MaterialElement.GetBaseColor(), 0);
			SpecGlossToMetalRough->ConnectExpression(MaterialElement.GetMetallic(), 1);

			FMaterialExpressionGeneric* GlossToRoughness = MaterialElement.AddMaterialExpression<FMaterialExpressionGeneric>();
			GlossToRoughness->SetExpressionName(TEXT("OneMinus"));
			GlossToRoughness->ConnectExpression(MaterialElement.GetRoughness(), 0);

			// Diffuse Color (BaseColor/BaseColorFactor are used to store the Diffuse alternatives for Spec/Gloss)
			MapFactory.GroupName = TEXT("Diffuse Color");
			FMaterialExpression* Diffuse = MapFactory.CreateColorMap(GetTexture(GLTFMaterial.BaseColor, Textures),
																	 GLTFMaterial.BaseColor.TexCoord,
																	 GLTFMaterial.BaseColorFactor,
																	 TEXT("Diffuse"),
																	 TEXT("Color"),
																	 ETextureMode::Color,
																	 *SpecGlossToMetalRough->GetInput(1));

			// Specular (goes into SpecGlossToMetalRough conversion)
			Maps.Emplace(GLTFMaterial.SpecularGlossiness.SpecularFactor,
						 TEXT("Specular Factor"),
						 FPBRMapFactory::EChannel::RGB,
						 SpecGlossToMetalRough->GetInput(0),
						 nullptr);

			// Glossiness (converted to Roughness)
			Maps.Emplace(GLTFMaterial.SpecularGlossiness.GlossinessFactor,
						 TEXT("Glossiness Factor"),
				         FPBRMapFactory::EChannel::Alpha,
						 GlossToRoughness->GetInput(0),
						 nullptr);

			// Creates the multimap for Specular and Glossiness
			MapFactory.CreateMultiMap(GetTexture(GLTFMaterial.SpecularGlossiness.Map, Textures),
									  GLTFMaterial.SpecularGlossiness.Map.TexCoord,
				                      TEXT("SpecularGlossiness"),
									  Maps.GetData(),
									  Maps.Num(),
				                      ETextureMode::Color);
		}
	}

	void FMaterialFactoryImpl::HandleOcclusion(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement)
	{
		MapFactory.GroupName = TEXT("Occlusion");

		FMaterialExpressionTexture* TexExpression = MapFactory.CreateTextureMap(
			GetTexture(GLTFMaterial.Occlusion, Textures), GLTFMaterial.Occlusion.TexCoord, TEXT("Occlusion"), ETextureMode::Grayscale);

		if (!TexExpression)
			return;

		FMaterialExpressionScalar* ConstantExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionScalar>();
		ConstantExpression->GetScalar()               = 1.f;

		FMaterialExpressionGeneric* LerpExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionGeneric>();
		LerpExpression->SetExpressionName(TEXT("LinearInterpolate"));

		FMaterialExpressionScalar* ValueExpression = MaterialElement.AddMaterialExpression<FMaterialExpressionScalar>();
		ValueExpression->SetName(TEXT("Occlusion Strength"));
		ValueExpression->SetGroupName(*MapFactory.GroupName);
		ValueExpression->GetScalar() = GLTFMaterial.OcclusionStrength;

		ConstantExpression->ConnectExpression(*LerpExpression->GetInput(0), 0);
		TexExpression->ConnectExpression(*LerpExpression->GetInput(1), (int)FPBRMapFactory::EChannel::Red);  // ignore other channels
		ValueExpression->ConnectExpression(*LerpExpression->GetInput(2), 0);

		LerpExpression->ConnectExpression(MaterialElement.GetAmbientOcclusion(), 0);
	}

	void FMaterialFactoryImpl::HandleEmissive(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement)
	{
		if (GLTFMaterial.Emissive.TextureIndex == INDEX_NONE || GLTFMaterial.EmissiveFactor.IsNearlyZero())
		{
			return;
		}

		MapFactory.GroupName = TEXT("Emission");
		MapFactory.CreateColorMap(GetTexture(GLTFMaterial.Emissive, Textures),
								  GLTFMaterial.Emissive.TexCoord,
								  GLTFMaterial.EmissiveFactor,
								  TEXT("Emissive"),
								  TEXT("Color"),
								  ETextureMode::Color, // emissive map is in sRGB space
								  MaterialElement.GetEmissiveColor());
	}

	void FMaterialFactoryImpl::HandleNormal(const TArray<GLTF::FTexture>& Textures, const GLTF::FMaterial& GLTFMaterial, FPBRMapFactory& MapFactory, FMaterialElement& MaterialElement)
	{
		MapFactory.GroupName = TEXT("Normal");
		MapFactory.CreateNormalMap(GetTexture(GLTFMaterial.Normal, Textures),
								   GLTFMaterial.Normal.TexCoord,
								   GLTFMaterial.NormalScale);
	}

	FMaterialFactory::FMaterialFactory(IMaterialElementFactory* MaterialElementFactory, ITextureFactory* TextureFactory)
	    : Impl(new FMaterialFactoryImpl(MaterialElementFactory, TextureFactory))
	{
	}

	FMaterialFactory::~FMaterialFactory() {}

	const TArray<FMaterialElement*>& FMaterialFactory::CreateMaterials(const GLTF::FAsset& Asset, UObject* ParentPackage, EObjectFlags Flags)
	{
		return Impl->CreateMaterials(Asset, ParentPackage, Flags);
	}

	const TArray<FLogMessage>& FMaterialFactory::GetLogMessages() const
	{
		return Impl->GetLogMessages();
	}

	const TArray<FMaterialElement*>& FMaterialFactory::GetMaterials() const
	{
		return Impl->Materials;
	}

	IMaterialElementFactory& FMaterialFactory::GetMaterialElementFactory()
	{
		return *Impl->MaterialElementFactory;
	}

	ITextureFactory& FMaterialFactory::GetTextureFactory()
	{
		return *Impl->TextureFactory;
	}

	void FMaterialFactory::CleanUp()
	{
		for (FMaterialElement* MaterialElement : Impl->Materials)
		{
			delete MaterialElement;
		}
		Impl->Materials.Empty();
	}

}  // namespace GLTF
