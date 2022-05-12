#pragma once

namespace MaterialX
{
	namespace StandardSurface
	{
		namespace Input
		{
			static constexpr const char* Base = "base";
			static constexpr const char* BaseColor = "base_color";
			static constexpr const char* DiffuseRoughness = "diffuse_roughness";
			static constexpr const char* Metalness = "metalness";
			static constexpr const char* Specular = "specular";
			static constexpr const char* SpecularColor = "specular_color";
			static constexpr const char* SpecularRoughness = "specular_roughness";
			static constexpr const char* SpecularIOR = "specular_IOR";
			static constexpr const char* SpecularAnisotropy = "specular_anisotropy";
			static constexpr const char* SpecularRotation = "specular_rotation";
			static constexpr const char* Transmission = "transmission";
			static constexpr const char* TransmissionColor = "transmission_color";
			static constexpr const char* TransmissionDepth = "transmission_depth";
			static constexpr const char* TransmissionScatter = "transmission_scatter";
			static constexpr const char* TransmissionScatterAnisotropy = "transmission_scatter_anisotropy";
			static constexpr const char* TransmissionDispersion = "transmission_dispersion";
			static constexpr const char* TransmissionExtraRoughness= "transmission_extra_roughness";
			static constexpr const char* Subsurface = "subsurface";
			static constexpr const char* SubsurfaceColor = "subsurface_color";
			static constexpr const char* SubsurfaceRadius = "subsurface_radius";
			static constexpr const char* SubsurfaceScale = "subsurface_scale";
			static constexpr const char* SubsurfaceAnisotropy = "subsurface_anisotropy";
			static constexpr const char* Sheen = "sheen";
			static constexpr const char* SheenColor = "sheen_color";
			static constexpr const char* SheenRoughness = "sheen_roughness";
			static constexpr const char* Coat = "coat";
			static constexpr const char* CoatColor = "coat_color";
			static constexpr const char* CoatRoughness = "coat_roughness";
			static constexpr const char* CoatAnisotropy = "coat_anisotropy";
			static constexpr const char* CoatRotation = "coat_rotation";
			static constexpr const char* CoatIOR = "coat_IOR";
			static constexpr const char* CoatNormal = "coat_normal";
			static constexpr const char* CoatAffectColor = "coat_affect_color";
			static constexpr const char* CoatAffectRoughness = "coat_affect_roughness";
			static constexpr const char* ThinFilmThickness = "thin_film_thickness";
			static constexpr const char* ThinFilmIOR = "thin_film_IOR";
			static constexpr const char* Emission = "emission";
			static constexpr const char* EmissionColor = "emission_color";
			static constexpr const char* Opacity = "opacity";
			static constexpr const char* ThinWalled= "thin_walled";
			static constexpr const char* Normal = "normal";
			static constexpr const char* Tangent = "tangent";
		}
	}

	namespace Attributes
	{
		static constexpr const char* IsVisited = "UE:IsVisited";
		static constexpr const char* OldName = "UE:OldName";
	}

	namespace Category
	{
		static constexpr const char* Add = "add";
		static constexpr const char* Sub = "subtract";
		static constexpr const char* Multiply = "multiply";
		static constexpr const char* Clamp = "clamp";
		static constexpr const char* Max = "max";
		static constexpr const char* Min = "min";
		static constexpr const char* Mix = "mix";
		static constexpr const char* DotProduct = "dotproduct";
		static constexpr const char* Combine2 = "combine2";
		static constexpr const char* Combine3 = "combine2";
		static constexpr const char* Combine4 = "combine4";
		static constexpr const char* Constant = "constant";
		static constexpr const char* Cos = "cos";
		static constexpr const char* Sin = "sin";
		static constexpr const char* Dot = "dot";
		static constexpr const char* Image = "image";
		static constexpr const char* TiledImage = "tiledimage";
		static constexpr const char* NormalMap = "normalmap";
		static constexpr const char* Extract = "extract";
		static constexpr const char* TexCoord = "texcoord";
	}

	namespace NodeDefinition
	{
		static constexpr const char* StandardSurface = "ND_standard_surface_surfaceshader";
	}

	namespace Library
	{
		static constexpr const char* Std = "stdlib";
		static constexpr const char* Pbr= "pbrlib";
		static constexpr const char* Bxdf = "bxdf";
	}
}