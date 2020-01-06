// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;

using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Visual;


namespace DatasmithRevitExporter
{
	public class FMaterialData
	{
		private Material                 CurrentMaterial   = null;
		private string                   MaterialLabel     = null;	
		public  int                      MaterialIndex     = 0;
		public  FDatasmithFacadeMaterial MasterMaterial    = null;
		private IList<string>            ExtraTexturePaths = null;
		public  IList<string>            MessageList       = new List<string>();

		// Multi-line debug log.
		// private FDatasmithFacadeLog DebugLog = null;

		public static string GetMaterialName(
			MaterialNode InMaterialNode,
			Material     InMaterial
		)
		{
			if (InMaterial == null)
			{
				Color MaterialColor        = InMaterialNode.Color.IsValid ? InMaterialNode.Color : new Color(255, 255, 255);
				int   MaterialTransparency = (int)(InMaterialNode.Transparency * 100.0);
				int   MaterialSmoothness   = InMaterialNode.Smoothness;

				// Generate a unique name for the fallback material.
				return $"{MaterialColor.Red:x2}{MaterialColor.Green:x2}{MaterialColor.Blue:x2}{MaterialTransparency:x2}{MaterialSmoothness:x2}";
			}
			else
			{
				return $"{Path.GetFileNameWithoutExtension(InMaterial.Document.PathName)}:{InMaterial.UniqueId}";
			}
		}

		public FMaterialData(
			MaterialNode  InMaterialNode,
			Material      InMaterial,
			int           InMaterialIndex,
			IList<string> InExtraTexturePaths
		)
		{
			CurrentMaterial   = InMaterial;
			MaterialLabel     = GetMaterialLabel(InMaterialNode, InMaterial);
			MaterialIndex     = InMaterialIndex;
			ExtraTexturePaths = InExtraTexturePaths;

			// Create a new Datasmith master material.
			MasterMaterial = new FDatasmithFacadeMaterial(GetMaterialName(InMaterialNode, CurrentMaterial), GetMaterialLabel(InMaterialNode, CurrentMaterial));

			// Hash the Datasmith master material name to shorten it.
			MasterMaterial.HashName();

			// Set the properties of the Datasmith master material.
			if (!SetMasterMaterial(CurrentMaterial, MasterMaterial))
			{
				SetFallbackMaterial(InMaterialNode.Color, (float) InMaterialNode.Transparency, InMaterialNode.Smoothness / 100.0F, MasterMaterial);
			}
		}

		public FMaterialData(
			string InMaterialName,
			Color  InMaterialColor,
			int    InMaterialIndex
		)
		{
			MaterialLabel = InMaterialName;
			MaterialIndex = InMaterialIndex;

			// Create a new Datasmith master material.
			MasterMaterial = new FDatasmithFacadeMaterial(InMaterialName, MaterialLabel);

			// Hash the Datasmith master material name to shorten it.
			MasterMaterial.HashName();

			// Set the properties of the Datasmith master material.
			SetFallbackMaterial(InMaterialColor, 0.0F, 0.5F, MasterMaterial);
		}

		public void Log(
			MaterialNode        InMaterialNode,
			FDatasmithFacadeLog InDebugLog,
			string              InLinePrefix
		)
		{
			if (InDebugLog != null)
			{
				int MaterialId = (CurrentMaterial == null) ? 0 : CurrentMaterial.Id.IntegerValue;

				InDebugLog.AddLine($"{InLinePrefix} {MaterialId}: '{GetMaterialLabel(InMaterialNode, CurrentMaterial)}'");
			}
		}

		private static string GetMaterialLabel(
			MaterialNode InMaterialNode,
			Material     InMaterial
		)
		{
			if (InMaterial != null)
			{
				Asset RenderingAsset = (InMaterial.Document.GetElement(InMaterial.AppearanceAssetId) as AppearanceAssetElement)?.GetRenderingAsset();

				if (RenderingAsset != null)
				{
					string RenderingAssetName = RenderingAsset.Name.Replace("Schema", "");
					Type RenderingAssetType = Type.GetType($"Autodesk.Revit.DB.Visual.{RenderingAssetName},RevitAPI");

					if (RenderingAssetType != null)
					{
						switch (RenderingAssetType.Name)
						{
							case "Ceramic":
							case "Concrete":
							case "Glazing":
							case "Hardwood":
							case "MasonryCMU":
							case "Metal":
							case "MetallicPaint":
							case "Mirror":
							case "PlasticVinyl":
							case "SolidGlass":
							case "Stone":
							case "WallPaint":
							case "Generic":
								return InMaterial.Name;
							default:
								break;
						}
					}

					return InMaterial.Name;
				}

				return InMaterial.Name;
			}

			return InMaterialNode.NodeName;
		}

		private bool SetMasterMaterial(
			Material                 InMaterial,
			FDatasmithFacadeMaterial IOMasterMaterial
		)
		{
			if (InMaterial == null)
			{
				// The properties of the Datasmith master material cannot be set.
				return false;
			}

			Asset RenderingAsset = (InMaterial.Document.GetElement(InMaterial.AppearanceAssetId) as AppearanceAssetElement)?.GetRenderingAsset();

			if (RenderingAsset == null)
			{
				// The properties of the Datasmith master material cannot be set.
				return false;
			}

			string RenderingAssetName = RenderingAsset.Name.Replace("Schema", "");
			Type   RenderingAssetType = Type.GetType($"Autodesk.Revit.DB.Visual.{RenderingAssetName},RevitAPI");

			if (RenderingAssetType == null)
			{
				// The properties of the Datasmith master material cannot be set.
				return false;
			}

			Color sourceMaterialColor = InMaterial.Color.IsValid ? InMaterial.Color : new Color(255, 255, 255);

			// TODO: Some master material setup code below should be put in reusable methods.

			switch (RenderingAssetType.Name)
			{
				case "Ceramic":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					Color color = GetColorPropertyValue(RenderingAsset, Ceramic.CeramicColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, Ceramic.CeramicColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Ceramic.CeramicColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Ceramic.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Ceramic.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", false);

					float glossiness = 0.6F; // CeramicApplicationType.Satin
					switch ((CeramicApplicationType) GetIntegerPropertyValue(RenderingAsset, Ceramic.CeramicApplication, (int) CeramicApplicationType.Satin))
					{
						case CeramicApplicationType.HighGlossy:
							glossiness = 0.9F;
							break;
						case CeramicApplicationType.Satin:
							glossiness = 0.6F;
							break;
						case CeramicApplicationType.Matte:
							glossiness = 0.35F;
							break;
					}                        

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", glossiness);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					if ((CeramicPatternType) GetIntegerPropertyValue(RenderingAsset, Ceramic.CeramicPattern, (int) CeramicPatternType.None) == CeramicPatternType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, Ceramic.CeramicPatternMap);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, Ceramic.CeramicPatternAmount, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicPatternMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicPatternMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicPatternMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Ceramic.CeramicPatternMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Ceramic.CeramicPatternMap, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}
				}
				break;

				case "Concrete":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					Color color = GetColorPropertyValue(RenderingAsset, Concrete.ConcreteColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, Concrete.ConcreteColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Concrete.ConcreteColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Concrete.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Concrete.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", false);

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", 0.5F);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					if ((ConcreteFinishType) GetIntegerPropertyValue(RenderingAsset, Concrete.ConcreteFinish, (int) ConcreteFinishType.Straight) == ConcreteFinishType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, Concrete.ConcreteBumpMap);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount  = GetFloatPropertyValue(RenderingAsset, Concrete.ConcreteBumpAmount, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteBumpMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteBumpMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteBumpMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Concrete.ConcreteBumpMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Concrete.ConcreteBumpMap, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}
				}
				break;

				case "Glazing":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Transparent);

					// TODO: Should use the Glazing.GlazingTransmittanceColor to select a predefined color value.
					Color color           = sourceMaterialColor;
					string diffuseMapPath = null;
					if ((GlazingTransmittanceColorType) GetIntegerPropertyValue(RenderingAsset, Glazing.GlazingTransmittanceColor, (int) GlazingTransmittanceColorType.Clear) == GlazingTransmittanceColorType.Custom)
					{
						color          = GetColorPropertyValue(RenderingAsset, Glazing.GlazingTransmittanceMap, sourceMaterialColor);
						diffuseMapPath = GetTexturePropertyPath(RenderingAsset, Glazing.GlazingTransmittanceMap);
					}

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Glazing.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Glazing.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					float transparency = 1.0F - GetFloatPropertyValue(RenderingAsset, Glazing.GlazingReflectance, 0.0F);

					// Control the Unreal material Opacity.
					IOMasterMaterial.AddFloat("Transparency", transparency);
					IOMasterMaterial.AddFloat("TransparencyMapFading", 0.0F);

					// Control the Unreal material Normal.
					IOMasterMaterial.AddFloat("BumpAmount", 0.0F);

					// Control the Unreal material Refraction.
					IOMasterMaterial.AddFloat("RefractionIndex", 1.01F);
				}
				break;

				case "Hardwood":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", sourceMaterialColor.Red / 255.0F, sourceMaterialColor.Green / 255.0F, sourceMaterialColor.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, Hardwood.HardwoodColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Hardwood.HardwoodColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Hardwood.HardwoodColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Hardwood.HardwoodColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Hardwood.HardwoodColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Hardwood.HardwoodColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Hardwood.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Hardwood.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", false);

					float glossiness = 0.45F; // HardwoodFinishType.Satin
					switch ((HardwoodFinishType) GetIntegerPropertyValue(RenderingAsset, Hardwood.HardwoodFinish, (int) HardwoodFinishType.Satin))
					{
						case HardwoodFinishType.Gloss:
							glossiness = 0.8F;
							break;
						case HardwoodFinishType.Semigloss:
							glossiness = 0.65F;
							break;
						case HardwoodFinishType.Satin:
							glossiness = 0.45F;
							break;
						case HardwoodFinishType.Unfinished:
							glossiness = 0.05F;
							break;
					}                        

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", glossiness);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					HardwoodImperfectionsType hardwoodImperfectionsType = (HardwoodImperfectionsType) GetIntegerPropertyValue(RenderingAsset, Hardwood.HardwoodImperfections, (int) HardwoodImperfectionsType.None);
					if (hardwoodImperfectionsType != HardwoodImperfectionsType.None)
					{
						string texturePropertyName = (hardwoodImperfectionsType == HardwoodImperfectionsType.Automatic) ? Hardwood.HardwoodColor : Hardwood.HardwoodImperfectionsShader;

						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, texturePropertyName);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, Hardwood.HardwoodImperfectionsAmount, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, texturePropertyName, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, texturePropertyName, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, texturePropertyName, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, texturePropertyName, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, texturePropertyName, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}
				}
				break;

				case "MasonryCMU":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					Color color = GetColorPropertyValue(RenderingAsset, MasonryCMU.MasonryCMUColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, MasonryCMU.MasonryCMUColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, MasonryCMU.MasonryCMUColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, MasonryCMU.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, MasonryCMU.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", false);

					float glossiness = 0.25F; // MasonryCMUApplicationType.Matte
					switch ((MasonryCMUApplicationType) GetIntegerPropertyValue(RenderingAsset, MasonryCMU.MasonryCMUApplication, (int) MasonryCMUApplicationType.Matte))
					{
						case MasonryCMUApplicationType.Glossy:
							glossiness = 0.9F;
							break;
						case MasonryCMUApplicationType.Matte:
						case MasonryCMUApplicationType.Unfinished:
							glossiness = 0.25F;
							break;
					}                        

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", glossiness);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					if ((MasonryCMUPatternType) GetIntegerPropertyValue(RenderingAsset, MasonryCMU.MasonryCMUPattern, (int) MasonryCMUPatternType.None) == MasonryCMUPatternType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, MasonryCMU.MasonryCMUPatternMap);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, MasonryCMU.MasonryCMUPatternHeight, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUPatternMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUPatternMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUPatternMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, MasonryCMU.MasonryCMUPatternMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, MasonryCMU.MasonryCMUPatternMap, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}
				}
				break;

				case "Metal":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					// TODO: Should use the Metal.MetalColor to select a predefined color value.

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", sourceMaterialColor.Red / 255.0F, sourceMaterialColor.Green / 255.0F, sourceMaterialColor.Blue / 255.0F, 1.0F);
					IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Metal.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Metal.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", true);

					float glossiness = 0.5F; // MetalFinishType.SemiPolished
					switch ((MetalFinishType) GetIntegerPropertyValue(RenderingAsset, Metal.MetalFinish, (int) MetalFinishType.SemiPolished))
					{
						case MetalFinishType.Polished:
							glossiness = 1.0F;
							break;
						case MetalFinishType.SemiPolished:
							glossiness = 0.5F;
							break;
						case MetalFinishType.Satin:
						case MetalFinishType.Brushed:
							glossiness = 0.25F;
							break;
					}

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", glossiness);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					if ((MetalPerforationsType) GetIntegerPropertyValue(RenderingAsset, Metal.MetalPerforations, (int) MetalPerforationsType.None) == MetalPerforationsType.Custom)
					{
						string cutoutMapPath = GetTexturePropertyPath(RenderingAsset, Metal.MetalPerforationsShader);

						if (!string.IsNullOrEmpty(cutoutMapPath))
						{
							float cutoutMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Metal.MetalPerforationsShader, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float cutoutMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Metal.MetalPerforationsShader, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float cutoutMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Metal.MetalPerforationsShader, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float cutoutMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Metal.MetalPerforationsShader, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float cutoutMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Metal.MetalPerforationsShader, UnifiedBitmap.TextureWAngle);

							IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.CutOut);

							// Control the Unreal material Opacity Mask.
							IOMasterMaterial.AddTexture("CutoutOpacityMap",  cutoutMapPath);
							IOMasterMaterial.AddFloat("CutoutMap_UVOffsetX", cutoutMapUVOffsetX);
							IOMasterMaterial.AddFloat("CutoutMap_UVOffsetY", cutoutMapUVOffsetY);
							IOMasterMaterial.AddFloat("CutoutMap_UVScaleX",  cutoutMapUVScaleX);
							IOMasterMaterial.AddFloat("CutoutMap_UVScaleY",  cutoutMapUVScaleY);
							IOMasterMaterial.AddFloat("CutoutMap_UVWAngle",  cutoutMapUVWAngle);
						}
					}

					if ((MetalPatternType) GetIntegerPropertyValue(RenderingAsset, Metal.MetalPattern, (int) MetalPatternType.None) == MetalPatternType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, Metal.MetalPatternShader);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, Metal.MetalPatternHeight, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Metal.MetalPatternShader, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Metal.MetalPatternShader, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Metal.MetalPatternShader, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Metal.MetalPatternShader, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Metal.MetalPatternShader, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}
				}
				break;

				case "MetallicPaint":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					Color color = GetColorPropertyValue(RenderingAsset, MetallicPaint.MetallicpaintBaseColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, MetallicPaint.MetallicpaintBaseColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, MetallicPaint.MetallicpaintBaseColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, MetallicPaint.MetallicpaintBaseColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, MetallicPaint.MetallicpaintBaseColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, MetallicPaint.MetallicpaintBaseColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, MetallicPaint.MetallicpaintBaseColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, MetallicPaint.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, MetallicPaint.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", true);

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", 0.9F);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					// Control the Unreal material Normal.
					IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
				}
				break;

				case "Mirror":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					Color color = GetColorPropertyValue(RenderingAsset, Mirror.MirrorTintcolor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);
					IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Mirror.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Mirror.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", true);

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", 1.0F);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					// Control the Unreal material Normal.
					IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
				}
				break;

				case "PlasticVinyl":
				{
					Color color = GetColorPropertyValue(RenderingAsset, PlasticVinyl.PlasticvinylColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, PlasticVinyl.PlasticvinylColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, PlasticVinyl.PlasticvinylColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, PlasticVinyl.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, PlasticVinyl.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					if ((PlasticvinylBumpType) GetIntegerPropertyValue(RenderingAsset, PlasticVinyl.PlasticvinylBump, (int) PlasticvinylBumpType.None) == PlasticvinylBumpType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, PlasticVinyl.PlasticvinylBumpMap);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, PlasticVinyl.PlasticvinylBumpAmount, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylBumpMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylBumpMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylBumpMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, PlasticVinyl.PlasticvinylBumpMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, PlasticVinyl.PlasticvinylBumpMap, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}

					if ((PlasticvinylType) GetIntegerPropertyValue(RenderingAsset, PlasticVinyl.PlasticvinylType, (int) PlasticvinylType.Plasticsolid) == PlasticvinylType.Plastictransparent)
					{
						IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Transparent);

						// Control the Unreal material Opacity.
						IOMasterMaterial.AddFloat("Transparency", 0.5F);
						IOMasterMaterial.AddFloat("TransparencyMapFading", 0.0F);

						// Control the Unreal material Refraction.
						IOMasterMaterial.AddFloat("RefractionIndex", 1.0F);
					}
					else
					{
						IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

						// Control the Unreal material Metallic.
						IOMasterMaterial.AddBoolean("IsMetal", false);

						float glossiness = 0.9F; // PlasticvinylApplicationType.Polished
						switch ((PlasticvinylApplicationType) GetIntegerPropertyValue(RenderingAsset, PlasticVinyl.PlasticvinylApplication, (int) PlasticvinylApplicationType.Polished))
						{
							case PlasticvinylApplicationType.Glossy:
								glossiness = 1.0F;
								break;
							case PlasticvinylApplicationType.Polished:
								glossiness = 0.9F;
								break;
							case PlasticvinylApplicationType.Matte:
								glossiness = 0.75F;
								break;
						}                        

						// Control the Unreal material Roughness.
						IOMasterMaterial.AddFloat("Glossiness", glossiness);
					}
				}
				break;

				case "SolidGlass":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Transparent);

					// TODO: Should use the SolidGlass.SolidglassTransmittance to select a predefined color value.
					Color color           = sourceMaterialColor;
					string diffuseMapPath = null;
					if ((SolidglassTransmittanceType) GetIntegerPropertyValue(RenderingAsset, SolidGlass.SolidglassTransmittance, (int) SolidglassTransmittanceType.Clear) == SolidglassTransmittanceType.CustomColor)
					{
						color          = GetColorPropertyValue(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, sourceMaterialColor);
						diffuseMapPath = GetTexturePropertyPath(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor);
					}

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, SolidGlass.SolidglassTransmittanceCustomColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, SolidGlass.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, SolidGlass.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					float transparency = 1.0F - GetFloatPropertyValue(RenderingAsset, SolidGlass.SolidglassReflectance, 0.0F);

					// Control the Unreal material Opacity.
					IOMasterMaterial.AddFloat("Transparency", transparency);
					IOMasterMaterial.AddFloat("TransparencyMapFading", 0.0F);

					if ((SolidglassBumpEnableType) GetIntegerPropertyValue(RenderingAsset, SolidGlass.SolidglassBumpEnable, (int) SolidglassBumpEnableType.None) == SolidglassBumpEnableType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, SolidGlass.SolidglassBumpMap);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, SolidGlass.SolidglassBumpAmount, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassBumpMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassBumpMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassBumpMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, SolidGlass.SolidglassBumpMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, SolidGlass.SolidglassBumpMap, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}

					// Control the Unreal material Refraction.
					IOMasterMaterial.AddFloat("RefractionIndex", 1.0F);
				}
				break;
				
				case "Stone":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", sourceMaterialColor.Red / 255.0F, sourceMaterialColor.Green / 255.0F, sourceMaterialColor.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, Stone.StoneColor);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Stone.StoneColor, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Stone.StoneColor, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Stone.StoneColor, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Stone.StoneColor, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Stone.StoneColor, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", 1.0F);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Stone.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Stone.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", false);

					float glossiness = 0.35F; // StoneApplicationType.Matte
					switch ((StoneApplicationType) GetIntegerPropertyValue(RenderingAsset, Stone.StoneApplication, (int) StoneApplicationType.Matte))
					{
						case StoneApplicationType.Polished:
							glossiness = 1.0F;
							break;
						case StoneApplicationType.Glossy:
							glossiness = 0.8F;
							break;
						case StoneApplicationType.Matte:
							glossiness = 0.35F;
							break;
						case StoneApplicationType.Unfinished:
							glossiness = 0.25F;
							break;
					}                        

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", glossiness);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					if ((StonePatternType) GetIntegerPropertyValue(RenderingAsset, Stone.StonePattern, (int) StonePatternType.None) == StonePatternType.Custom)
					{
						string bumpMapPath = GetTexturePropertyPath(RenderingAsset, Stone.StonePatternMap);

						if (!string.IsNullOrEmpty(bumpMapPath))
						{
							float bumpAmount       = GetFloatPropertyValue(RenderingAsset, Stone.StonePatternAmount, 0.0F);
							float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Stone.StonePatternMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Stone.StonePatternMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Stone.StonePatternMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Stone.StonePatternMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Stone.StonePatternMap, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Normal.
							IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
							IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
							IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
							IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
							IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
						}
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}
				}
				break;

				case "WallPaint":
				{
					IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

					Color color = GetColorPropertyValue(RenderingAsset, WallPaint.WallpaintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);
					IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, WallPaint.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, WallPaint.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					// Control the Unreal material Metallic.
					IOMasterMaterial.AddBoolean("IsMetal", false);

					float glossiness = 0.6F; // WallpaintFinishType.Pearl
					switch ((WallpaintFinishType) GetIntegerPropertyValue(RenderingAsset, WallPaint.WallpaintFinish, (int) WallpaintFinishType.Pearl))
					{
						case WallpaintFinishType.Gloss:
							glossiness = 0.75F;
							break;
						case WallpaintFinishType.Semigloss:
							glossiness = 0.7F;
							break;
						case WallpaintFinishType.Pearl:
							glossiness = 0.6F;
							break;
						case WallpaintFinishType.Platinum:
							glossiness = 0.55F;
							break;
						case WallpaintFinishType.Eggshell:
							glossiness = 0.5F;
							break;
						case WallpaintFinishType.Flat:
							glossiness = 0.4F;
							break;
					}                        

					// Control the Unreal material Roughness.
					IOMasterMaterial.AddFloat("Glossiness", glossiness);

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

					// Control the Unreal material Normal.
					IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
				}
				break;

				case "Generic":
				{
					Color color = GetColorPropertyValue(RenderingAsset, Generic.GenericDiffuse, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddColor("DiffuseColor", color.Red / 255.0F, color.Green / 255.0F, color.Blue / 255.0F, 1.0F);

					string diffuseMapPath = GetTexturePropertyPath(RenderingAsset, Generic.GenericDiffuse);

					if (!string.IsNullOrEmpty(diffuseMapPath))
					{
						float diffuseImageFade    = GetFloatPropertyValue(RenderingAsset, Generic.GenericDiffuseImageFade, 0.0F);
						float diffuseMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Generic.GenericDiffuse, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float diffuseMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Generic.GenericDiffuse, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float diffuseMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericDiffuse, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float diffuseMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericDiffuse, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float diffuseMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Generic.GenericDiffuse, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Base Color.
						IOMasterMaterial.AddFloat("DiffuseMapFading", diffuseImageFade);                          
						IOMasterMaterial.AddTexture("DiffuseMap", diffuseMapPath);                    
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetX", diffuseMapUVOffsetX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVOffsetY", diffuseMapUVOffsetY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleX",  diffuseMapUVScaleX);
						IOMasterMaterial.AddFloat("DiffuseMap_UVScaleY",  diffuseMapUVScaleY);
						IOMasterMaterial.AddFloat("DiffuseMap_UVWAngle",  diffuseMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);                          
					}

					bool  tintEnabled = GetBooleanPropertyValue(RenderingAsset, Generic.CommonTintToggle, false);
					Color tintColor   = GetColorPropertyValue(RenderingAsset, Generic.CommonTintColor, sourceMaterialColor);

					// Control the Unreal material Base Color.
					IOMasterMaterial.AddBoolean("TintEnabled", tintEnabled);
					IOMasterMaterial.AddColor("TintColor", tintColor.Red / 255.0F, tintColor.Green / 255.0F, tintColor.Blue / 255.0F, 1.0F);

					float selfIlluminationLuminance   = GetFloatPropertyValue(RenderingAsset, Generic.GenericSelfIllumLuminance, 0.0F);
					Color selfIlluminationFilterColor = GetColorPropertyValue(RenderingAsset, Generic.GenericSelfIllumFilterMap, new Color(255, 255, 255));

					// Control the Unreal material Emissive Color.
					IOMasterMaterial.AddFloat("SelfIlluminationLuminance", selfIlluminationLuminance);
					IOMasterMaterial.AddColor("SelfIlluminationFilter", selfIlluminationFilterColor.Red / 255.0F, selfIlluminationFilterColor.Green / 255.0F, selfIlluminationFilterColor.Blue / 255.0F, 1.0F);

					string selfIlluminationMapPath = GetTexturePropertyPath(RenderingAsset, Generic.GenericSelfIllumFilterMap);

					if (!string.IsNullOrEmpty(selfIlluminationMapPath))
					{
						float selfIlluminationMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Generic.GenericSelfIllumFilterMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float selfIlluminationMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Generic.GenericSelfIllumFilterMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float selfIlluminationMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericSelfIllumFilterMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float selfIlluminationMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericSelfIllumFilterMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float selfIlluminationMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Generic.GenericSelfIllumFilterMap, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Emissive Color.
						IOMasterMaterial.AddBoolean("SelfIlluminationMapEnable", true);
						IOMasterMaterial.AddTexture("SelfIlluminationMap", selfIlluminationMapPath);
						IOMasterMaterial.AddFloat("SelfIlluminationMap_UVOffsetX", selfIlluminationMapUVOffsetX);
						IOMasterMaterial.AddFloat("SelfIlluminationMap_UVOffsetY", selfIlluminationMapUVOffsetY);
						IOMasterMaterial.AddFloat("SelfIlluminationMap_UVScaleX",  selfIlluminationMapUVScaleX);
						IOMasterMaterial.AddFloat("SelfIlluminationMap_UVScaleY",  selfIlluminationMapUVScaleY);
						IOMasterMaterial.AddFloat("SelfIlluminationMap_UVWAngle",  selfIlluminationMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddBoolean("SelfIlluminationMapEnable", false);
					}

					string bumpMapPath = GetTexturePropertyPath(RenderingAsset, Generic.GenericBumpMap);

					if (!string.IsNullOrEmpty(bumpMapPath))
					{
						float bumpAmount       = GetFloatPropertyValue(RenderingAsset, Generic.GenericBumpAmount, 0.0F);
						float bumpMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Generic.GenericBumpMap, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
						float bumpMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Generic.GenericBumpMap, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
						float bumpMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericBumpMap, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
						float bumpMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericBumpMap, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
						float bumpMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Generic.GenericBumpMap, UnifiedBitmap.TextureWAngle);

						// Control the Unreal material Normal.
						IOMasterMaterial.AddFloat("BumpAmount", bumpAmount);
						IOMasterMaterial.AddTexture("BumpMap", bumpMapPath);
						IOMasterMaterial.AddFloat("BumpMap_UVOffsetX", bumpMapUVOffsetX);
						IOMasterMaterial.AddFloat("BumpMap_UVOffsetY", bumpMapUVOffsetY);
						IOMasterMaterial.AddFloat("BumpMap_UVScaleX",  bumpMapUVScaleX);
						IOMasterMaterial.AddFloat("BumpMap_UVScaleY",  bumpMapUVScaleY);
						IOMasterMaterial.AddFloat("BumpMap_UVWAngle",  bumpMapUVWAngle);
					}
					else
					{
						IOMasterMaterial.AddFloat("BumpAmount", 0.0F);
					}

					float transparency = GetFloatPropertyValue(RenderingAsset, Generic.GenericTransparency, 0.0F);

					if (transparency > 0.0F)
					{
						IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Transparent);

						// Control the Unreal material Opacity.
						IOMasterMaterial.AddFloat("Transparency", transparency);

						string transparencyMapPath = GetTexturePropertyPath(RenderingAsset, Generic.GenericTransparency);

						if (!string.IsNullOrEmpty(transparencyMapPath))
						{
							float transparencyImageFade    = GetFloatPropertyValue(RenderingAsset, Generic.GenericTransparencyImageFade, 0.0F);
							float transparencyMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Generic.GenericTransparency, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float transparencyMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Generic.GenericTransparency, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float transparencyMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericTransparency, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float transparencyMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericTransparency, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float transparencyMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Generic.GenericTransparency, UnifiedBitmap.TextureWAngle);

							// Control the Unreal material Opacity.
							IOMasterMaterial.AddFloat("TransparencyMapFading", transparencyImageFade);
							IOMasterMaterial.AddTexture("TransparencyMap", transparencyMapPath);
							IOMasterMaterial.AddFloat("TransparencyMap_UVOffsetX", transparencyMapUVOffsetX);
							IOMasterMaterial.AddFloat("TransparencyMap_UVOffsetY", transparencyMapUVOffsetY);
							IOMasterMaterial.AddFloat("TransparencyMap_UVScaleX",  transparencyMapUVScaleX);
							IOMasterMaterial.AddFloat("TransparencyMap_UVScaleY",  transparencyMapUVScaleY);
							IOMasterMaterial.AddFloat("TransparencyMap_UVWAngle",  transparencyMapUVWAngle);
						}
						else
						{
							IOMasterMaterial.AddFloat("TransparencyMapFading", 0.0F);
						}

						float refractionIndex = GetFloatPropertyValue(RenderingAsset, Generic.GenericRefractionIndex, 1.0F);

						// Control the Unreal material Refraction.
						IOMasterMaterial.AddFloat("RefractionIndex", refractionIndex);
					}
					else
					{
						IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

						bool isMetal = GetBooleanPropertyValue(RenderingAsset, Generic.GenericIsMetal, false);

						// Control the Unreal material Metallic.
						IOMasterMaterial.AddBoolean("IsMetal", isMetal);

						float glossiness = GetFloatPropertyValue(RenderingAsset, Generic.GenericGlossiness, InMaterial.Smoothness / 100.0F);

						// Control the Unreal material Roughness.
						IOMasterMaterial.AddFloat("Glossiness", glossiness);

						string cutoutMapPath = GetTexturePropertyPath(RenderingAsset, Generic.GenericCutoutOpacity);

						if (!string.IsNullOrEmpty(cutoutMapPath))
						{
							float cutoutMapUVOffsetX = GetTexturePropertyDistance(RenderingAsset, Generic.GenericCutoutOpacity, UnifiedBitmap.TextureRealWorldOffsetX, 0.0F);
							float cutoutMapUVOffsetY = GetTexturePropertyDistance(RenderingAsset, Generic.GenericCutoutOpacity, UnifiedBitmap.TextureRealWorldOffsetY, 0.0F);
							float cutoutMapUVScaleX  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericCutoutOpacity, UnifiedBitmap.TextureRealWorldScaleX, 1.0F);
							float cutoutMapUVScaleY  = 1.0F / GetTexturePropertyDistance(RenderingAsset, Generic.GenericCutoutOpacity, UnifiedBitmap.TextureRealWorldScaleY, 1.0F);
							float cutoutMapUVWAngle  = GetTexturePropertyAngle(RenderingAsset, Generic.GenericCutoutOpacity, UnifiedBitmap.TextureWAngle);

							IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.CutOut);

							// Control the Unreal material Opacity Mask.
							IOMasterMaterial.AddTexture("CutoutOpacityMap",  cutoutMapPath);
							IOMasterMaterial.AddFloat("CutoutMap_UVOffsetX", cutoutMapUVOffsetX);
							IOMasterMaterial.AddFloat("CutoutMap_UVOffsetY", cutoutMapUVOffsetY);
							IOMasterMaterial.AddFloat("CutoutMap_UVScaleX",  cutoutMapUVScaleX);
							IOMasterMaterial.AddFloat("CutoutMap_UVScaleY",  cutoutMapUVScaleY);
							IOMasterMaterial.AddFloat("CutoutMap_UVWAngle",  cutoutMapUVWAngle);
						}
					}
				}
				break;

				default:
				{
					// The properties of the Datasmith master material cannot be set.
					return false;
				}
			}

			// The properties of the Datasmith master material are set.
			return true;
        }

		private void SetFallbackMaterial(
			Color                    InMaterialColor,
			float                    InMaterialTransparency, // in range 0.0-1.0
			float                    InMaterialGlossiness,   // in range 0.0-1.0
			FDatasmithFacadeMaterial IOMasterMaterial
		)
		{
			// Control the Unreal material Base Color.
			Color MaterialColor = InMaterialColor.IsValid ? InMaterialColor : new Color(255, 255, 255);
			IOMasterMaterial.AddColor("DiffuseColor", MaterialColor.Red / 255.0F, MaterialColor.Green / 255.0F, MaterialColor.Blue / 255.0F, 1.0F);
			IOMasterMaterial.AddFloat("DiffuseMapFading", 0.0F);
			IOMasterMaterial.AddBoolean("TintEnabled", false);

			// Control the Unreal material Emissive Color.
			IOMasterMaterial.AddFloat("SelfIlluminationLuminance", 0.0F);

			// Control the Unreal material Normal.
			IOMasterMaterial.AddFloat("BumpAmount", 0.0F);

			if (InMaterialTransparency > 0.0F)
			{
				IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Transparent);

				// Control the Unreal material Opacity.
				IOMasterMaterial.AddFloat("Transparency", InMaterialTransparency);
				IOMasterMaterial.AddFloat("TransparencyMapFading", 0.0F);

				// Control the Unreal material Refraction.
				IOMasterMaterial.AddFloat("RefractionIndex", 1.0F);
			}
			else
			{
				IOMasterMaterial.SetMasterMaterialType(FDatasmithFacadeMaterial.EMasterMaterialType.Opaque);

				// Control the Unreal material Metallic.
				IOMasterMaterial.AddBoolean("IsMetal", false);

				// Control the Unreal material Roughness.
				IOMasterMaterial.AddFloat("Glossiness", InMaterialGlossiness);
			}
		}

		private bool GetBooleanPropertyValue(
			Asset  in_asset,
			string in_propertyName,
			bool   in_defaultValue
		)
        {
			// DebugLog.AddLine($"Boolean Property {in_propertyName}");

			AssetProperty booleanProperty = in_asset.FindByName(in_propertyName);

			return (booleanProperty != null) ? (booleanProperty as AssetPropertyBoolean).Value : in_defaultValue;
        }

		private int GetIntegerPropertyValue(
			Asset  in_asset,
			string in_propertyName,
			int    in_defaultValue
		)
        {
			// DebugLog.AddLine($"Integer Property {in_propertyName}");

			AssetProperty integerProperty = in_asset.FindByName(in_propertyName);

			if (integerProperty != null)
			{
				if (integerProperty.Type == AssetPropertyType.Enumeration)
				{
					// Handle the spurious case of an enumerated type value not stored in an integer asset property.
					return (integerProperty as AssetPropertyEnum).Value;
				}
				else
				{
					return (integerProperty as AssetPropertyInteger).Value;
				}
			}

			return in_defaultValue;
        }

		private float GetFloatPropertyValue(
			Asset  in_asset,
			string in_propertyName,
			float  in_defaultValue
		)
        {
			// DebugLog.AddLine($"Float Property {in_propertyName}");

			AssetProperty doubleProperty = in_asset.FindByName(in_propertyName);

			if (doubleProperty != null)
			{
				if (doubleProperty.Type == AssetPropertyType.Float)
				{
					// Handle the spurious case of a value not stored in a double asset property.
					return (doubleProperty as AssetPropertyFloat).Value;
				}
				else
				{
					return (float) (doubleProperty as AssetPropertyDouble).Value;
				}
			}

			return in_defaultValue;
        }

		private Color GetColorPropertyValue(
			Asset  in_asset,
			string in_propertyName,
			Color  in_defaultValue
		)
        {
			// DebugLog.AddLine($"Color Property {in_propertyName}");

			AssetProperty colorProperty = in_asset.FindByName(in_propertyName);

			if (colorProperty != null)
			{
				Color color = (colorProperty as AssetPropertyDoubleArray4d).GetValueAsColor();

				if (color.IsValid)
				{
					return color;
				}
			}

			return in_defaultValue;
		}

		private string GetTexturePropertyPath(
			Asset  in_asset,
			string in_propertyName
		)
        {
			// DebugLog.AddLine($"Texture Property {in_propertyName}");

			AssetProperty textureProperty = in_asset.FindByName(in_propertyName);

			if (textureProperty != null)
			{
				Asset unifiedBitmapAsset = textureProperty.GetSingleConnectedAsset();

				if (unifiedBitmapAsset != null)
				{
					AssetProperty sourceProperty = unifiedBitmapAsset.FindByName(UnifiedBitmap.UnifiedbitmapBitmap);

					if (sourceProperty != null)
					{
						string sourcePath = (sourceProperty as AssetPropertyString).Value;

						// DebugLog.AddLine($"Texture path: {sourcePath}");

						if (!string.IsNullOrEmpty(sourcePath))
						{
							if (sourcePath.Contains("|"))
							{
								sourcePath = sourcePath.Split('|')[0];
							}

							if (!string.IsNullOrEmpty(sourcePath))
							{
								// TODO: Better handle relative paths.
								const string RevitTextureFolder1 = "C:\\Program Files (x86)\\Common Files\\Autodesk Shared\\Materials\\Textures\\";
								const string RevitTextureFolder2 = "C:\\Program Files (x86)\\Common Files\\Autodesk Shared\\Materials\\Textures\\1\\Mats\\";

								if (sourcePath[0] == '1')
								{
									sourcePath =  $"{RevitTextureFolder1}{sourcePath}";
								}

								if (sourcePath.Contains("Materials\\Generic\\Presets\\"))
								{
									sourcePath = sourcePath.Replace("Materials\\Generic\\Presets\\", RevitTextureFolder2).Replace(".jpg", ".png");
								}

								if (!Path.IsPathRooted(sourcePath))
								{
									string rootedSourcePath = Path.Combine(RevitTextureFolder2, sourcePath);

									if (File.Exists(rootedSourcePath))
									{
										sourcePath = rootedSourcePath;
									}
								}

								// Also search a relative path in the extra texture paths.
								if (!Path.IsPathRooted(sourcePath))
								{
									foreach (string extraTexturePath in ExtraTexturePaths)
									{
										string extraSourcePath = Path.Combine(extraTexturePath, sourcePath);

										if (Path.IsPathRooted(extraSourcePath) && File.Exists(extraSourcePath))
										{
											sourcePath = extraSourcePath;
											break;
										}
									}
								}
								
								if (!File.Exists(sourcePath))
								{
									MessageList.Add($"Warning - Material \"{MaterialLabel}\": Cannot find texture file {sourcePath}");
								}

								return sourcePath;
							}
						}
					}
				}
			}

            return "";
        }

		private float GetTexturePropertyDistance(
			Asset  in_asset,
			string in_propertyName,
			string in_distanceName,
			float  in_defaultValue
		)
        {
			// DebugLog.AddLine($"Texture Distance Property {in_propertyName}");

			AssetProperty textureProperty = in_asset.FindByName(in_propertyName);

			if (textureProperty != null)
			{
				Asset unifiedBitmapAsset = textureProperty.GetSingleConnectedAsset();

				if (unifiedBitmapAsset != null)
				{
					float scalingFactor = 1.0F;

					AssetProperty versionProperty = unifiedBitmapAsset.FindByName("version");

					if (versionProperty != null)
					{
						// DebugLog.AddLine($"Texture version: {(versionProperty as AssetPropertyInteger).Value}");

						scalingFactor = ((versionProperty as AssetPropertyInteger).Value == 4) ? 12.0F /* inches per foot */ : 1.0F;
					}

					AssetProperty distanceProperty = unifiedBitmapAsset.FindByName(in_distanceName);

					if (distanceProperty != null)
					{
						double distance = (distanceProperty as AssetPropertyDistance).Value;

						return (distance != 0.0) ? (float) distance / scalingFactor : in_defaultValue;
					}
				}
			}

            return in_defaultValue;
        }

		private float GetTexturePropertyAngle(
			Asset  in_asset,
			string in_propertyName,
			string in_angleName
		)
        {
			// DebugLog.AddLine($"Texture Angle Property {in_propertyName}");

			AssetProperty textureProperty = in_asset.FindByName(in_propertyName);

			if (textureProperty != null)
			{
				Asset unifiedBitmapAsset = textureProperty.GetSingleConnectedAsset();

				if (unifiedBitmapAsset != null)
				{
					AssetProperty angleProperty = unifiedBitmapAsset.FindByName(in_angleName);

					if (angleProperty != null)
					{
						return (float) (angleProperty as AssetPropertyDouble).Value / 360.0F;
					}
				}
			}

            return 0.0F;
        }
	}
}
