// Copyright Epic Games, Inc. All Rights Reserved.
using Rhino;
using Rhino.DocObjects;
using Rhino.Geometry;
using System.Collections.Generic;
using System.IO;
using System.Drawing;
using System;

namespace DatasmithRhino
{
	public static class FDatasmithRhinoMaterialExporter
	{
		public static void ExportMaterials(FDatasmithFacadeScene DatasmithScene, DatasmithRhinoSceneParser SceneParser)
		{
			int TextureAndMaterialCount = SceneParser.TextureHashToTextureInfo.Count + SceneParser.MaterialHashToMaterialInfo.Count;
			int TextureAndMaterialProgress = 0;

			foreach (RhinoTextureInfo CurrentTextureInfo in SceneParser.TextureHashToTextureInfo.Values)
			{
				FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress((float)(++TextureAndMaterialProgress) / TextureAndMaterialCount);

				FDatasmithFacadeTexture TextureElement = ParseTexture(CurrentTextureInfo);
				if(TextureElement != null)
				{
					DatasmithScene.AddTexture(TextureElement);
				}
			}

			foreach (RhinoMaterialInfo CurrentMaterialInfo in SceneParser.MaterialHashToMaterialInfo.Values)
			{
				FDatasmithRhinoProgressManager.Instance.UpdateCurrentTaskProgress((float)(++TextureAndMaterialProgress) / TextureAndMaterialCount);
				FDatasmithFacadeUEPbrMaterial DSMaterial = new FDatasmithFacadeUEPbrMaterial(CurrentMaterialInfo.Name);
				DSMaterial.SetLabel(CurrentMaterialInfo.Label);
				ParseMaterial(DSMaterial, CurrentMaterialInfo.RhinoMaterial, SceneParser);

				DatasmithScene.AddMaterial(DSMaterial);
			}
		}

		public static FDatasmithFacadeTexture ParseTexture(RhinoTextureInfo TextureInfo)
		{
			Texture RhinoTexture = TextureInfo.RhinoTexture;
			if (!TextureInfo.IsSupported())
			{
				return null;
			}

			FDatasmithFacadeTexture TextureElement = new FDatasmithFacadeTexture(TextureInfo.Name);
			TextureElement.SetLabel(TextureInfo.Label);
			TextureElement.SetFile(TextureInfo.FilePath);
			TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
			TextureElement.SetRGBCurve(1);
			TextureElement.SetTextureAddressX(RhinoTexture.WrapU == TextureUvwWrapping.Clamp ? FDatasmithFacadeTexture.ETextureAddress.Clamp : FDatasmithFacadeTexture.ETextureAddress.Wrap);
			TextureElement.SetTextureAddressY(RhinoTexture.WrapV == TextureUvwWrapping.Clamp ? FDatasmithFacadeTexture.ETextureAddress.Clamp : FDatasmithFacadeTexture.ETextureAddress.Wrap);

			FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Diffuse;
			if (RhinoTexture.TextureType == TextureType.Bump)
			{
				TextureMode = FDatasmithFacadeTexture.ETextureMode.Bump;
			}

			TextureElement.SetTextureMode(TextureMode);

			return TextureElement;
		}

		public static void ParseMaterial(FDatasmithFacadeUEPbrMaterial DSMaterial, Material RhinoMaterial, DatasmithRhinoSceneParser SceneParser)
		{
			Color MaterialDiffuseColor = RhinoMaterial.DiffuseColor;
			MaterialDiffuseColor = Color.FromArgb(255 - (byte)(255 * RhinoMaterial.Transparency), MaterialDiffuseColor);

			Texture[] MaterialTextures = RhinoMaterial.GetTextures();
			for (int TextureIndex = 0; TextureIndex < MaterialTextures.Length; ++TextureIndex)
			{
				Texture RhinoTexture = MaterialTextures[TextureIndex];
				if(RhinoTexture != null)
				{
					RhinoTextureInfo TextureInfo = SceneParser.GetTextureInfoFromRhinoTexture(RhinoTexture.Id);
					if (TextureInfo != null)
					{
						AddTextureToMaterial(DSMaterial, TextureInfo, MaterialDiffuseColor);
					}
				}
			}

			// Set a diffuse color if there's nothing in the BaseColor
			if (DSMaterial.GetBaseColor().GetExpression() == null)
			{
				FDatasmithFacadeMaterialExpressionColor ColorExpression = DSMaterial.AddMaterialExpressionColor();
				ColorExpression.SetName("Diffuse Color");
				ColorExpression.SetsRGBColor(MaterialDiffuseColor.R, MaterialDiffuseColor.G, MaterialDiffuseColor.B, MaterialDiffuseColor.A);

				ColorExpression.ConnectExpression(DSMaterial.GetBaseColor());
			}

			if (RhinoMaterial.Transparency > 0)
			{
				DSMaterial.SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
				if (DSMaterial.GetOpacity().GetExpression() == null)
				{
					// Transparent color
					FDatasmithFacadeMaterialExpressionScalar Scalar = DSMaterial.AddMaterialExpressionScalar();
					Scalar.SetName("Opacity");
					Scalar.SetScalar(1 - (float)RhinoMaterial.Transparency);
					Scalar.ConnectExpression(DSMaterial.GetOpacity());
				}
				else
				{
					// Modulate the opacity map with the color transparency setting
					FDatasmithFacadeMaterialExpressionGeneric Multiply = DSMaterial.AddMaterialExpressionGeneric();
					Multiply.SetExpressionName("Multiply");

					FDatasmithFacadeMaterialExpressionScalar Scalar = DSMaterial.AddMaterialExpressionScalar();
					Scalar.SetName("Opacity Output Level");
					Scalar.SetScalar(1 - (float)RhinoMaterial.Transparency);
					Scalar.ConnectExpression(Multiply.GetInput(0));

					FDatasmithFacadeMaterialExpression CurrentOpacityExpression = DSMaterial.GetOpacity().GetExpression();
					CurrentOpacityExpression.ConnectExpression(Multiply.GetInput(1));

					DSMaterial.GetOpacity().SetExpression(Multiply);
				}
			}

			float Shininess = (float) (RhinoMaterial.Shine / Material.MaxShine);
			if (Math.Abs(Shininess) > float.Epsilon)
			{
				FDatasmithFacadeMaterialExpressionScalar ShininessExpression = DSMaterial.AddMaterialExpressionScalar();
				ShininessExpression.SetName("Roughness");
				ShininessExpression.SetScalar(1f - Shininess);
				ShininessExpression.ConnectExpression(DSMaterial.GetRoughness());
			}

			float Reflectivity = (float) RhinoMaterial.Reflectivity;
			if (Math.Abs(Reflectivity) > float.Epsilon)
			{
				FDatasmithFacadeMaterialExpressionScalar ReflectivityExpression = DSMaterial.AddMaterialExpressionScalar();
				ReflectivityExpression.SetName("Metallic");
				ReflectivityExpression.SetScalar(Reflectivity);
				ReflectivityExpression.ConnectExpression(DSMaterial.GetMetallic());
			}
		}


		private static void AddTextureToMaterial(FDatasmithFacadeUEPbrMaterial DSMaterial, RhinoTextureInfo TextureInfo, Color DiffuseColor)
		{
			if (!TextureInfo.IsSupported())
			{
				return;
			}

			Texture RhinoTexture = TextureInfo.RhinoTexture;

			// Extract texture mapping info
			double BlendConstant, BlendA0, BlendA1, BlendA2, BlendA3;
			RhinoTexture.GetAlphaBlendValues(out BlendConstant, out BlendA0, out BlendA1, out BlendA2, out BlendA3);
			FDatasmithFacadeMaterialsUtils.FWeightedMaterialExpressionParameters WeightedExpressionParameters = new FDatasmithFacadeMaterialsUtils.FWeightedMaterialExpressionParameters((float)BlendConstant);
			FDatasmithFacadeMaterialsUtils.FUVEditParameters UVParameters = GetUVParameter(RhinoTexture);

			switch (RhinoTexture.TextureType)
			{
				case TextureType.Bitmap:
					{
						FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(DSMaterial, "Diffuse Map", TextureInfo.Name, UVParameters);

						WeightedExpressionParameters.SetColorsRGB(DiffuseColor.R, DiffuseColor.G, DiffuseColor.B, DiffuseColor.A);
						WeightedExpressionParameters.SetExpression(TextureExpression);
						FDatasmithFacadeMaterialExpression Expression = FDatasmithFacadeMaterialsUtils.CreateWeightedMaterialExpression(DSMaterial, "Diffuse Color", WeightedExpressionParameters);

						DSMaterial.GetBaseColor().SetExpression(Expression);
					}
					break;
				case TextureType.Bump:
					{
						FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(DSMaterial, "Bump Map", TextureInfo.Name, UVParameters);

						WeightedExpressionParameters.SetExpression(TextureExpression);
						WeightedExpressionParameters.SetTextureMode(FDatasmithFacadeTexture.ETextureMode.Bump);
						FDatasmithFacadeMaterialExpression Expression = FDatasmithFacadeMaterialsUtils.CreateWeightedMaterialExpression(DSMaterial, "Bump Height", WeightedExpressionParameters);

						DSMaterial.GetNormal().SetExpression(Expression);
					}
					break;
				case TextureType.Transparency:
					{
						FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(DSMaterial, "Opacity Map", TextureInfo.Name, UVParameters);

						Color BlendColor = Color.White;
						WeightedExpressionParameters.SetColorsRGB(BlendColor.R, BlendColor.G, BlendColor.B, BlendColor.A);
						WeightedExpressionParameters.SetExpression(TextureExpression);
						FDatasmithFacadeMaterialExpression Expression = FDatasmithFacadeMaterialsUtils.CreateWeightedMaterialExpression(DSMaterial, "White", WeightedExpressionParameters);

						DSMaterial.GetOpacity().SetExpression(Expression);
						if (Math.Abs(BlendConstant) > float.Epsilon)
						{
							DSMaterial.SetBlendMode(/*EBlendMode::BLEND_Translucent*/2);
						}
					}
					break;
			}
		}

		/// <summary>
		/// Returns the UV Parameters for the given Texture.
		/// This function uses the same operations as in FOpenNurbsTranslatorImpl::TranslateMaterialTable(), improvements should be applied to both functions.
		/// </summary>
		/// <param name="RhinoTexture"></param>
		/// <returns></returns>
		private static FDatasmithFacadeMaterialsUtils.FUVEditParameters GetUVParameter(Texture RhinoTexture)
		{
			// Extract texture mapping info
			FDatasmithFacadeMaterialsUtils.FUVEditParameters UVParameters = new FDatasmithFacadeMaterialsUtils.FUVEditParameters();

			// Use cached texture coordinates(channel 0)
			UVParameters.SetChannelIndex(0);

			//// Extract the UV tiling, offset and rotation angle from the UV transform matrix
			Transform RotationTransform, OrthogonalTransform;
			Vector3d Translation, Scale;
			RhinoTexture.UvwTransform.DecomposeAffine(out Translation, out RotationTransform, out OrthogonalTransform, out Scale);

			double RotX, RotY, RotZ;
			if (!RotationTransform.GetYawPitchRoll(out RotX, out RotY, out RotZ))
			{
				//This is not a valid rotation make sure the angles are at 0;
				RotX = RotY = RotZ = 0;
			}
			else
			{
				RotX = FDatasmithRhinoUtilities.RadianToDegree(RotX);
				RotY = FDatasmithRhinoUtilities.RadianToDegree(RotY);
				RotZ = FDatasmithRhinoUtilities.RadianToDegree(RotZ);
			}

			UVParameters.SetUVTiling((float)Scale.X, (float)Scale.Y);
			
			//If the tiling vector is not zero.
			if (Math.Abs(Scale.X) > float.Epsilon && Math.Abs(Scale.Y) > float.Epsilon)
			{
				float UVOffsetX = (float) (Translation.X / Scale.X);
				float UVOffsetY = (float)-(Translation.Y / Scale.Y + 0.5f - 0.5f / Scale.Y);

				UVParameters.SetUVOffset(UVOffsetX, UVOffsetY); // V-coordinate is inverted in Unreal
			}

			// Rotation angle is reversed because V-axis points down in Unreal while it points up in OpenNurbs
			UVParameters.SetRotationAngle((float)-RotX);

			return UVParameters;
		}
	}
}