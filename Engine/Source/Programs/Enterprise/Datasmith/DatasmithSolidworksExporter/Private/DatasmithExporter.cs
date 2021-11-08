// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;

namespace DatasmithSolidworks
{
	public enum EActorType
	{
		SimpleActor,
		MeshActor
	};

	public class FDatasmithActorExportInfo
	{
		public EActorType Type;
		public string Label;
		public string Name;
		public string ParentName;
		public float[] Transform;
		public bool bVisible;
	};

	public class FDatasmithExporter
	{
		private Dictionary<string, Tuple<EActorType, FDatasmithFacadeActor>> ExportedActorsMap = new Dictionary<string, Tuple<EActorType, FDatasmithFacadeActor>>();
		private ConcurrentDictionary<string, Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>> ExportedMeshesMap = new ConcurrentDictionary<string, Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>>();
		private ConcurrentDictionary<int, FDatasmithFacadeUEPbrMaterial> ExportedMaterialsMap = new ConcurrentDictionary<int, FDatasmithFacadeUEPbrMaterial>();
		private ConcurrentDictionary<string, FDatasmithFacadeTexture> ExportedTexturesMap = new ConcurrentDictionary<string, FDatasmithFacadeTexture>();
		private Dictionary<string, FDatasmithFacadeActorBinding> ExportedActorBindingsMap = new Dictionary<string, FDatasmithFacadeActorBinding>();

		private FDatasmithFacadeScene DatasmithScene = null;

		public FDatasmithExporter(FDatasmithFacadeScene InScene)
		{
			DatasmithScene = InScene;
		}

		public EActorType? GetExportedActorType(string InActorName)
		{
			Tuple<EActorType, FDatasmithFacadeActor> ActorInfo = null;
			if (ExportedActorsMap.TryGetValue(InActorName, out ActorInfo))
			{
				return ActorInfo.Item1;
			}
			return null;
		}

		public void ExportOrUpdateActor(FDatasmithActorExportInfo InExportInfo)
		{
			FDatasmithFacadeActor Actor = null;

			if (ExportedActorsMap.ContainsKey(InExportInfo.Name))
			{
				Tuple<EActorType, FDatasmithFacadeActor> ActorInfo = ExportedActorsMap[InExportInfo.Name];

				if (ActorInfo.Item1 != InExportInfo.Type)
				{
					// Actor was exported but is different type -- delete old one
					DatasmithScene.RemoveActor(ActorInfo.Item2);
					ExportedActorsMap.Remove(InExportInfo.Name);
				}
				else
				{
					Actor = ActorInfo.Item2;
				}
			}

			if (Actor == null)
			{
				switch (InExportInfo.Type)
				{
					case EActorType.SimpleActor: Actor = new FDatasmithFacadeActor(InExportInfo.Name); break;
					case EActorType.MeshActor: Actor = new FDatasmithFacadeActorMesh(InExportInfo.Name); break;
				}
				
				Actor.AddTag(InExportInfo.Name);

				ExportedActorsMap[InExportInfo.Name] = new Tuple<EActorType, FDatasmithFacadeActor>(InExportInfo.Type, Actor);

				Tuple<EActorType, FDatasmithFacadeActor> ParentExportInfo = null;
				if (!string.IsNullOrEmpty(InExportInfo.ParentName) && ExportedActorsMap.TryGetValue(InExportInfo.ParentName, out ParentExportInfo))
				{
					FDatasmithFacadeActor ParentActor = ParentExportInfo.Item2;
					ParentActor.AddChild(Actor);
				}
				else
				{
					DatasmithScene.AddActor(Actor);
				}
			}

			// ImportBinding uses Tag[0] ('original name') to group parts used in variants
			Actor.SetLabel(InExportInfo.Label);
			Actor.SetVisibility(InExportInfo.bVisible);
			Actor.SetWorldTransform(AdjustTransformForDatasmith(InExportInfo.Transform));
		}

		public void RemoveActor(string InActorName)
		{
			if (ExportedActorsMap.ContainsKey(InActorName))
			{
				Tuple<EActorType, FDatasmithFacadeActor> ActorInfo = ExportedActorsMap[InActorName];
				FDatasmithFacadeActor Actor = ActorInfo.Item2;
				DatasmithScene.RemoveActor(Actor);
				ExportedActorsMap.Remove(InActorName);
			}
		}

		public void ExportMesh(string InMeshName, FMeshData InData, string InUpdateMeshActor, out Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh> OutMeshPair)
		{
			OutMeshPair = null;

			if (InData.Vertices == null || InData.Normals == null || InData.TexCoords == null || InData.Triangles == null)
			{
				return;
			}

			if (InData.Vertices.Length == 0 || InData.Normals.Length == 0 || InData.TexCoords.Length == 0 || InData.Triangles.Length == 0)
			{
				return;
			}

			InMeshName = SanitizeName(InMeshName);

			FDatasmithFacadeMesh Mesh = new FDatasmithFacadeMesh();
			Mesh.SetName(InMeshName);

			FDatasmithFacadeMeshElement MeshElement = new FDatasmithFacadeMeshElement(InMeshName);

			Mesh.SetVerticesCount(InData.Vertices.Length);
			Mesh.SetFacesCount(InData.Triangles.Length);

			for (int i = 0; i < InData.Vertices.Length; i++)
			{
				Mesh.SetVertex(i, InData.Vertices[i].X, InData.Vertices[i].Y, InData.Vertices[i].Z);
			}
			for (int i = 0; i < InData.Normals.Length; i++)
			{
				Mesh.SetNormal(i, InData.Normals[i].X, InData.Normals[i].Y, InData.Normals[i].Z);
			}

			if (InData.TexCoords != null)
			{
				Mesh.SetUVChannelsCount(1);
				Mesh.SetUVCount(0, InData.TexCoords.Length);
				for (int i = 0; i < InData.TexCoords.Length; i++)
				{
					Mesh.SetUV(0, i, InData.TexCoords[i].X, InData.TexCoords[i].Y);
				}

			}

			HashSet<int> MeshAddedMaterials = new HashSet<int>();

			for (int TriIndex = 0; TriIndex < InData.Triangles.Length; TriIndex++)
			{
				FTriangle Triangle = InData.Triangles[TriIndex];
				int MatID = 0;

				if (Triangle.MaterialID >= 1)
				{
					if (!MeshAddedMaterials.Contains(Triangle.MaterialID))
					{
						FDatasmithFacadeUEPbrMaterial Material = null;
						ExportedMaterialsMap.TryGetValue(Triangle.MaterialID, out Material);

						if (Material != null)
						{
							MeshAddedMaterials.Add(Triangle.MaterialID);
							MeshElement.SetMaterial(Material.GetName(), Triangle.MaterialID);
							MatID = Triangle.MaterialID;
						}
					}
					else
					{
						MatID = Triangle.MaterialID;
					}
				}

				Mesh.SetFace(TriIndex, Triangle[0], Triangle[1], Triangle[2], MatID);
				Mesh.SetFaceUV(TriIndex, 0, Triangle[0], Triangle[1], Triangle[2]);
			}

			OutMeshPair = new Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>(MeshElement, Mesh);

			DatasmithScene.ExportDatasmithMesh(MeshElement, Mesh);

			ExportedMeshesMap.TryAdd(InMeshName, new Tuple<FDatasmithFacadeMeshElement, FDatasmithFacadeMesh>(MeshElement, Mesh));

			if (!string.IsNullOrEmpty(InUpdateMeshActor))
			{
				Tuple<EActorType, FDatasmithFacadeActor> ExportedActorInfo = null;
				if (ExportedActorsMap.TryGetValue(InUpdateMeshActor, out ExportedActorInfo) && ExportedActorInfo.Item1 == EActorType.MeshActor)
				{
					FDatasmithFacadeActorMesh MeshActor = ExportedActorInfo.Item2 as FDatasmithFacadeActorMesh;
					MeshActor.SetMesh(InMeshName);
				}
			}
		}

		public void ExportMetadata(FMetadata InMetadata)
		{
			FDatasmithFacadeElement Element = null;

			if (InMetadata.OwnerType == FMetadata.EOwnerType.Actor)
			{
				if (ExportedActorsMap.ContainsKey(InMetadata.OwnerName))
				{
					Tuple<EActorType, FDatasmithFacadeActor> ActorInfo = ExportedActorsMap[InMetadata.OwnerName];
					Element = ActorInfo.Item2;
				}
			}
			else if (InMetadata.OwnerType == FMetadata.EOwnerType.MeshActor)
			{
				//TODO do we need metadata on mesh elements?
				//Element = processor.MeshFactory.GetFacadeElement(cmd.MetadataOwnerName);
			}

			if (Element != null)
			{
				FDatasmithFacadeMetaData DatasmithMetadata = DatasmithScene.GetMetaData(Element);

				if (DatasmithMetadata == null)
				{
					DatasmithMetadata = new FDatasmithFacadeMetaData("SolidWorks Document Metadata");
					DatasmithMetadata.SetAssociatedElement(Element);
					DatasmithScene.AddMetaData(DatasmithMetadata);
				}

				foreach (IMetadataPair Pair in InMetadata.Pairs)
				{
					Pair.WriteToDatasmithMetaData(DatasmithMetadata);
				}
			}
		}

		public void ExportConfiguration(string InConfigurationsSetName, FConfigurationData InConfig)
		{
			FDatasmithFacadeActorBinding GetActorBinding(string InActorName, FDatasmithFacadeVariant InVariant)
			{
				FDatasmithFacadeActorBinding Binding = null;

				if (!ExportedActorBindingsMap.ContainsKey(InActorName))
				{
					// Find a datasmith actor
					FDatasmithFacadeActor Actor = null;

					if (ExportedActorsMap.ContainsKey(InActorName))
					{
						Tuple<EActorType, FDatasmithFacadeActor> ActorInfo = ExportedActorsMap[InActorName];
						Actor = ActorInfo.Item2;
					}
					else
					{
						// Actor was not found, should not happen
						return null;
					}

					// Make a new binding
					Binding = new FDatasmithFacadeActorBinding(Actor);
					ExportedActorBindingsMap.Add(InActorName, Binding);
					InVariant.AddActorBinding(Binding);
				}
				else
				{
					// Get an existing binding
					Binding = ExportedActorBindingsMap[InActorName];
				}
				return Binding;
			}

			// Request existing VariantSet, or create a new one
			FDatasmithFacadeLevelVariantSets LevelVariantSets = null;
			FDatasmithFacadeVariantSet VariantSet = null;

			if (DatasmithScene.GetLevelVariantSetsCount() == 0)
			{
				LevelVariantSets = new FDatasmithFacadeLevelVariantSets("LevelVariantSets");
				DatasmithScene.AddLevelVariantSets(LevelVariantSets);
			}
			else
			{
				LevelVariantSets = DatasmithScene.GetLevelVariantSets(0);
			}

			int VariantSetsCount = LevelVariantSets.GetVariantSetsCount();
			for (int VariantSetIndex = 0; VariantSetIndex < VariantSetsCount; ++VariantSetIndex)
			{
				FDatasmithFacadeVariantSet VSet = LevelVariantSets.GetVariantSet(VariantSetIndex);

				if (VSet.GetName() == InConfigurationsSetName)
				{
					VariantSet = VSet;
					break;
				}
			}

			if (VariantSet == null)
			{
				VariantSet = new FDatasmithFacadeVariantSet(InConfigurationsSetName);
				LevelVariantSets.AddVariantSet(VariantSet);
			}

			// Add a new variant
			FDatasmithFacadeVariant Variant = new FDatasmithFacadeVariant(InConfig.Name);
			VariantSet.AddVariant(Variant);

			// Build a visibility variant data
			foreach (var VisibilityMap in InConfig.ComponentVisibility)
			{
				FDatasmithFacadeActorBinding Binding = GetActorBinding(VisibilityMap.Key, Variant);
				if (Binding != null)
				{
					Binding.AddVisibilityCapture(VisibilityMap.Value);
				}
			}

			// Provide transform variants
			foreach (var TransformMap in InConfig.ComponentTransform)
			{
				FDatasmithFacadeActorBinding Binding = GetActorBinding(TransformMap.Key, Variant);
				if (Binding != null)
				{
					Binding.AddRelativeTransformCapture(TransformMap.Value);
				}
			}

			// Iterate over all material assignments
			foreach (var KVP in InConfig.ComponentMaterials)
			{
				FDatasmithFacadeActorBinding Binding = GetActorBinding(KVP.Key, Variant);
				if (Binding != null)
				{
					FObjectMaterials Materials = KVP.Value;

					FMaterial TopMaterial = Materials.GetComponentMaterial();
					if (TopMaterial != null)
					{
						Binding.AddMaterialCapture(ExportedMaterialsMap[TopMaterial.ID]);
					}
				}
			}
		}

		private bool CreateAndCacheMaterial(FMaterial InMaterial, out List<FDatasmithFacadeTexture> OutCreatedTextures, out FDatasmithFacadeUEPbrMaterial OutCreatedMaterial)
		{
			OutCreatedTextures = null;
			OutCreatedMaterial = null;

			if (ExportedMaterialsMap.ContainsKey(InMaterial.ID))
			{
				return false;
			}

            FMaterial.EMaterialType TType = FMaterial.GetMaterialType(InMaterial.ShaderName);

			float Roughness = (float)InMaterial.Roughness;
			float Metallic = 0f;

            if (TType != FMaterial.EMaterialType.TYPE_LIGHTWEIGHT)
            {
                if (TType == FMaterial.EMaterialType.TYPE_METAL)
                {
                    Metallic = 1f;
                    //roughness = (float)MetallicRoughness;
                }
                else if (TType == FMaterial.EMaterialType.TYPE_METALLICPAINT)
				{
					Metallic = 0.7f;
				}

                if (InMaterial.BlurryReflections)
				{
					Roughness = (float)InMaterial.SpecularSpread;
				}
                else
                {
                    if (InMaterial.Reflectivity > 0.0)
                    {
						Roughness = (1f - (float) InMaterial.Reflectivity) * 0.2f;
					}
                    else
                    {
						Roughness = 1f;
					}
                }
            }

            FDatasmithFacadeUEPbrMaterial PBR = new FDatasmithFacadeUEPbrMaterial(InMaterial.Name);

			OutCreatedTextures = new List<FDatasmithFacadeTexture>();
			OutCreatedMaterial = PBR;

			double Mult = (TType == FMaterial.EMaterialType.TYPE_LIGHTWEIGHT) ? InMaterial.Diffuse : 1.0;

			double fR = Mult * InMaterial.PrimaryColor.R * 1.0 / 255.0;
			double fG = Mult * InMaterial.PrimaryColor.G * 1.0 / 255.0;
			double fB = Mult * InMaterial.PrimaryColor.B * 1.0 / 255.0;

			FDatasmithFacadeMaterialExpressionColor ColorExpr = PBR.AddMaterialExpressionColor();
			ColorExpr.SetColor((float) fR, (float) fG, (float) fB, 1f);
            ColorExpr.SetName("Diffuse Color");
            ColorExpr.ConnectExpression(PBR.GetBaseColor());

            FDatasmithFacadeMaterialExpressionScalar RoughExpr = PBR.AddMaterialExpressionScalar();
			RoughExpr.SetName("Roughness");
            RoughExpr.SetScalar(Roughness);
            RoughExpr.ConnectExpression(PBR.GetRoughness());

            FDatasmithFacadeMaterialExpressionScalar MetalExpr = PBR.AddMaterialExpressionScalar();
			MetalExpr.SetName("Metallic");
            MetalExpr.SetScalar(Metallic);
            MetalExpr.ConnectExpression(PBR.GetMetallic());

            if (InMaterial.Emission > 0.0)
            {
                FDatasmithFacadeMaterialExpressionColor EmissionExpr = PBR.AddMaterialExpressionColor();
				EmissionExpr.SetColor((float) fR, (float) fG, (float) fB, 1f);
                EmissionExpr.SetName("Emissive Color");
                EmissionExpr.ConnectExpression(PBR.GetEmissiveColor());
            }

            if (InMaterial.Transparency > 0.0)
            {
                FDatasmithFacadeMaterialExpressionScalar OpacityExpr = PBR.AddMaterialExpressionScalar();
				OpacityExpr.SetName("Opacity");
                OpacityExpr.SetScalar(1f - (float)InMaterial.Transparency);
				OpacityExpr.ConnectExpression(PBR.GetOpacity());
                //PBR.SetBlendMode()
            }

            if (!string.IsNullOrEmpty(InMaterial.Texture) && !File.Exists(InMaterial.Texture))
            {
                InMaterial.Texture = MaterialUtils.ComputeAssemblySideTexturePath(InMaterial.Texture);
            }

            if (!string.IsNullOrEmpty(InMaterial.Texture) && File.Exists(InMaterial.Texture))
            {
                string TextureName = SanitizeName(Path.GetFileNameWithoutExtension(InMaterial.Texture));

				FDatasmithFacadeTexture TextureElement = null;
                if (!ExportedTexturesMap.TryGetValue(InMaterial.Texture, out TextureElement))
                {
                    TextureElement = new FDatasmithFacadeTexture(TextureName);
					TextureElement.SetFile(InMaterial.Texture);
                    TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
                    TextureElement.SetRGBCurve(1);
                    TextureElement.SetTextureAddressX(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    TextureElement.SetTextureAddressY(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Diffuse;
					TextureElement.SetTextureMode(TextureMode);
					OutCreatedTextures.Add(TextureElement);
					ExportedTexturesMap.TryAdd(InMaterial.Texture, TextureElement);
				}

                FDatasmithFacadeMaterialsUtils.FWeightedMaterialExpressionParameters WeightedExpressionParameters = new FDatasmithFacadeMaterialsUtils.FWeightedMaterialExpressionParameters(1f);
				FDatasmithFacadeMaterialsUtils.FUVEditParameters UVParameters = new FDatasmithFacadeMaterialsUtils.FUVEditParameters();
                FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(PBR, "Diffuse Map", TextureName, UVParameters);
				WeightedExpressionParameters.SetColorsRGB(InMaterial.PrimaryColor.R, InMaterial.PrimaryColor.G, InMaterial.PrimaryColor.B, 255);
                WeightedExpressionParameters.SetExpression(TextureExpression);
                FDatasmithFacadeMaterialExpression Expression = FDatasmithFacadeMaterialsUtils.CreateWeightedMaterialExpression(PBR, "Diffuse Color", WeightedExpressionParameters);
				PBR.GetBaseColor().SetExpression(Expression);
            }

            if (!string.IsNullOrEmpty(InMaterial.BumpTextureFileName) && !File.Exists(InMaterial.BumpTextureFileName))
            {
                InMaterial.BumpTextureFileName = MaterialUtils.ComputeAssemblySideTexturePath(InMaterial.BumpTextureFileName);
            }

            if (!string.IsNullOrEmpty(InMaterial.BumpTextureFileName) && File.Exists(InMaterial.BumpTextureFileName))
            {
                string TextureName = SanitizeName(Path.GetFileNameWithoutExtension(InMaterial.BumpTextureFileName));

				FDatasmithFacadeTexture TextureElement = null;
                if (!ExportedTexturesMap.TryGetValue(InMaterial.BumpTextureFileName, out TextureElement))
                {
                    TextureElement = new FDatasmithFacadeTexture(TextureName);
					TextureElement.SetFile(InMaterial.BumpTextureFileName);
                    TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
                    TextureElement.SetRGBCurve(1);
                    TextureElement.SetTextureAddressX(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    TextureElement.SetTextureAddressY(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Normal;
					TextureElement.SetTextureMode(TextureMode);
					OutCreatedTextures.Add(TextureElement);
                    ExportedTexturesMap.TryAdd(InMaterial.BumpTextureFileName, TextureElement);
                }

                FDatasmithFacadeMaterialsUtils.FUVEditParameters UVParameters = new FDatasmithFacadeMaterialsUtils.FUVEditParameters();
				FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(PBR, "Bump Map", TextureName, UVParameters);
				PBR.GetNormal().SetExpression(TextureExpression);
            }

			ExportedMaterialsMap[InMaterial.ID] = PBR;

			return true;
		}

		public void ExportMaterials(ConcurrentDictionary<int, FMaterial> InMaterialsMap)
		{
			ConcurrentBag<FDatasmithFacadeTexture> CreatedTextures = new ConcurrentBag<FDatasmithFacadeTexture>();
			ConcurrentBag<FDatasmithFacadeUEPbrMaterial> CreatedMaterials = new ConcurrentBag<FDatasmithFacadeUEPbrMaterial>();
			Parallel.ForEach(InMaterialsMap, MatKVP =>
			{
				List<FDatasmithFacadeTexture> NewMaterialTextures = null;
				FDatasmithFacadeUEPbrMaterial NewMaterial = null;
				if (CreateAndCacheMaterial(MatKVP.Value, out NewMaterialTextures, out NewMaterial))
				{
					CreatedMaterials.Add(NewMaterial);

					foreach (FDatasmithFacadeTexture Texture in NewMaterialTextures)
					{
						CreatedTextures.Add(Texture);
					}
				}
			});
			// Adding stuff to a datasmith scene cannot be multithreaded!
			foreach (FDatasmithFacadeUEPbrMaterial Mat in CreatedMaterials)
			{
				DatasmithScene.AddMaterial(Mat);
			}
			foreach (FDatasmithFacadeTexture Texture in CreatedTextures)
			{
				DatasmithScene.AddTexture(Texture);
			}
		}

#if false // this is for 5.0
		public void ExportMaterial(FMaterial InMaterial)
		{
			FMaterial.EMaterialType Type = FMaterial.GetMaterialType(InMaterial.ShaderName);

			float Roughness = (float)InMaterial.Roughness;
			float Metallic = 0f;

			if (Type != FMaterial.EMaterialType.TYPE_LIGHTWEIGHT)
			{
				if (Type == FMaterial.EMaterialType.TYPE_METAL)
				{
					Metallic = 1f;
					//roughness = (float)MetallicRoughness;
				}
				else if (Type == FMaterial.EMaterialType.TYPE_METALLICPAINT)
				{
					Metallic = 0.7f;
				}

				if (InMaterial.BlurryReflections)
				{
					Roughness = (float)InMaterial.SpecularSpread;
				}
				else
				{
					if (InMaterial.Reflectivity > 0.0)
					{
						Roughness = (1f - (float)InMaterial.Reflectivity) * 0.2f;
					}
					else
					{
						Roughness = 1f;
					}
				}
			}

			float Mult = (Type == FMaterial.EMaterialType.TYPE_LIGHTWEIGHT) ? (float)InMaterial.Diffuse : 1.0f;

			float R = Mult * InMaterial.PrimaryColor.R * 1.0f / 255.0f;
			float G = Mult * InMaterial.PrimaryColor.G * 1.0f / 255.0f;
			float B = Mult * InMaterial.PrimaryColor.B * 1.0f / 255.0f;

			FDatasmithFacadeMasterMaterial MasterMaterial = new FDatasmithFacadeMasterMaterial(InMaterial.Name);

			MasterMaterial.SetMaterialType(FDatasmithFacadeMasterMaterial.EMasterMaterialType.Opaque);
			MasterMaterial.AddColor("TintColor", R, G, B, 1.0F);
			MasterMaterial.AddFloat("RoughnessAmount", Roughness);

			if (InMaterial.Transparency > 0.0)
			{
				MasterMaterial.SetMaterialType(FDatasmithFacadeMasterMaterial.EMasterMaterialType.Transparent);
				MasterMaterial.AddFloat("Metalness", Metallic);

				MasterMaterial.AddColor("OpacityAndRefraction",
					0.25f,                              // Opacity
					1.0f,                               // Refraction
					0.0f,                               // Refraction Exponent
					1f - (float)InMaterial.Transparency // Fresnel Opacity
				);

				SetNormalMap(InMaterial, MasterMaterial, "NormalMap");
			}
			else
			{
				MasterMaterial.AddFloat("MetallicAmount", Metallic);

				if (InMaterial.Emission > 0.0)
				{
					MasterMaterial.AddFloat("LuminanceAmount", (float)InMaterial.Emission);
					MasterMaterial.AddColor("LuminanceFilter", R, G, B, 1.0f);
				}

				SetDiffuseMap(InMaterial, MasterMaterial, "ColorMap");
				SetNormalMap(InMaterial, MasterMaterial, "NormalMap");
			}

			DatasmithScene.AddMaterial(MasterMaterial);

			ExportedMaterialsMap[InMaterial.ID] = MasterMaterial;
		}
#endif

		public void ExportAnimation(FAnimation InAnim)
		{
#if false // Animation export is not available for 4.27
			FDatasmithFacadeLevelSequence LevelSeq = new FDatasmithFacadeLevelSequence(InAnim.Name);

			LevelSeq.SetFrameRate(InAnim.FPS);

			foreach (var NodePair in InAnim.ComponentToChannelMap)
			{
				FAnimation.FChannel Chan = NodePair.Value;
				Component2 Component = Chan.Target;
				FDatasmithFacadeTransformAnimation Anim = new FDatasmithFacadeTransformAnimation(Component.Name2);

				foreach (var Keyframe in Chan.Keyframes)
				{
					FMatrix4 LocalMatrix = Keyframe.LocalMatrix;

					// Get euler angles in degrees
					float X = MathUtils.Rad2Deg * (float)Math.Atan2(LocalMatrix[6], LocalMatrix[10]);
					float Y = MathUtils.Rad2Deg * (float)Math.Atan2(-LocalMatrix[2], Math.Sqrt(LocalMatrix[6] * LocalMatrix[6] + LocalMatrix[10] * LocalMatrix[10]));
					float Z = MathUtils.Rad2Deg * (float)Math.Atan2(LocalMatrix[1], LocalMatrix[0]);

					float Scale = LocalMatrix[15];

					FVec3 Translation = new FVec3(LocalMatrix[12], LocalMatrix[13], LocalMatrix[14]);

					Anim.AddFrame(EDatasmithFacadeAnimationTransformType.Rotation, Keyframe.Step, X, -Y, -Z);
					Anim.AddFrame(EDatasmithFacadeAnimationTransformType.Scale, Keyframe.Step, Scale, Scale, Scale);
					Anim.AddFrame(EDatasmithFacadeAnimationTransformType.Translation, Keyframe.Step, Translation.X, -Translation.Y, Translation.Z);
				}

				LevelSeq.AddAnimation(Anim);
			}

			// Check if we already have a sequence with the same name and remove it if we do
			int SequencesSetsCount = DatasmithScene.GetLevelSequencesCount();
			for (int Index = 0; Index < SequencesSetsCount; ++Index)
			{
				FDatasmithFacadeLevelSequence ExistingSeq = DatasmithScene.GetLevelSequence(Index);

				if (ExistingSeq.GetName() == LevelSeq.GetName())
				{
					DatasmithScene.RemoveLevelSequence(ExistingSeq);
					break;
				}
			}

			DatasmithScene.AddLevelSequence(LevelSeq);
#endif
		}

		private void SetDiffuseMap(FMaterial InMaterial, FDatasmithFacadeMasterMaterial InMasterMaterial, string InParamName)
		{
			if (!string.IsNullOrEmpty(InMaterial.Texture) && !File.Exists(InMaterial.Texture))
			{
				InMaterial.Texture = MaterialUtils.ComputeAssemblySideTexturePath(InMaterial.Texture);
			}

			if (!string.IsNullOrEmpty(InMaterial.Texture) && File.Exists(InMaterial.Texture))
			{
				string TextureName = Path.GetFileNameWithoutExtension(InMaterial.Texture);

				FDatasmithFacadeTexture TextureElement = null;
				if (!ExportedTexturesMap.TryGetValue(InMaterial.Texture, out TextureElement))
				{
					TextureElement = new FDatasmithFacadeTexture(TextureName);
					TextureElement.SetFile(InMaterial.Texture);
					TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
					TextureElement.SetRGBCurve(1);
					TextureElement.SetTextureAddressX(FDatasmithFacadeTexture.ETextureAddress.Wrap);
					TextureElement.SetTextureAddressY(FDatasmithFacadeTexture.ETextureAddress.Wrap);
					FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Diffuse;
					TextureElement.SetTextureMode(TextureMode);
					DatasmithScene.AddTexture(TextureElement);

					ExportedTexturesMap.TryAdd(InMaterial.Texture, TextureElement);

					InMasterMaterial.AddTexture(InParamName, TextureElement);
				}
			}
		}

		private void SetNormalMap(FMaterial InMaterial, FDatasmithFacadeMasterMaterial InMasterMaterial, string InParamName)
		{
			if (!string.IsNullOrEmpty(InMaterial.BumpTextureFileName) && !File.Exists(InMaterial.BumpTextureFileName))
			{
				InMaterial.BumpTextureFileName = MaterialUtils.ComputeAssemblySideTexturePath(InMaterial.BumpTextureFileName);
			}

			if (!string.IsNullOrEmpty(InMaterial.BumpTextureFileName) && File.Exists(InMaterial.BumpTextureFileName))
			{
				string textureName = Path.GetFileNameWithoutExtension(InMaterial.BumpTextureFileName);

				FDatasmithFacadeTexture TextureElement = null;
				if (!ExportedTexturesMap.TryGetValue(InMaterial.BumpTextureFileName, out TextureElement))
				{
					TextureElement = new FDatasmithFacadeTexture(textureName);
					TextureElement.SetFile(InMaterial.BumpTextureFileName);
					TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
					TextureElement.SetRGBCurve(1);
					TextureElement.SetTextureAddressX(FDatasmithFacadeTexture.ETextureAddress.Wrap);
					TextureElement.SetTextureAddressY(FDatasmithFacadeTexture.ETextureAddress.Wrap);
					FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Normal;
					TextureElement.SetTextureMode(TextureMode);
					DatasmithScene.AddTexture(TextureElement);

					ExportedTexturesMap.TryAdd(InMaterial.BumpTextureFileName, TextureElement);

					InMasterMaterial.AddTexture(InParamName, TextureElement);
				}
			}
		}

		public static string SanitizeName(string InStringToSanitize)
		{
			const string Original = "^/()#$&.?!ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿБбВвГгДдЁёЖжЗзИиЙйКкЛлМмНнОоПпРрСсТтУуФфХхЦцЧчШшЩщЪъЫыЬьЭэЮюЯя'\"";
			const string Modified = "_____S____AAAAAAECEEEEIIIIDNOOOOOx0UUUUYPsaaaaaaeceeeeiiiiOnoooood0uuuuypyBbVvGgDdEeJjZzIiYyKkLlMmNnOoPpRrSsTtUuFfJjTtCcSsSs__ii__EeYyYy__";

			string Result = "";
			for (int i = 0; i < InStringToSanitize.Length; i++)
			{
				if (InStringToSanitize[i] <= 32)
				{
					Result += '_';
				}
				else
				{
					bool bReplaced = false;
					for (int j = 0; j < Original.Length; j++)
					{
						if (InStringToSanitize[i] == Original[j])
						{
							Result += Modified[j];
							bReplaced = true;
							break;
						}
					}
					if (!bReplaced)
					{
						Result += InStringToSanitize[i];
					}
				}
			}
			return Result;
		}

		private FMatrix4 AdjustTransformForDatasmith(float[] InXForm)
		{
			FMatrix4 RotMatrix = FMatrix4.FromRotationX(-90f);
			if (InXForm != null)
			{
				FMatrix4 Mat = new FMatrix4(InXForm);
				Mat = Mat * RotMatrix;
				return Mat;
			}
			return RotMatrix;
		}
	}
}