// Copyright Epic Games, Inc. All Rights Reserved.

//#define WRITE_CONFIGS_TO_XML

using System;
using System.Runtime;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;
using SolidworksDatasmith.Geometry;
using SolidworksDatasmith.Engine;
using System.Diagnostics;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwScene
	{
		private ModelDoc2 _doc = null;
		public ModelDoc2 Doc { get { return _doc; } }

		// filename to part map
		public Dictionary<string, SwPartDoc> Parts { get; set; } = new Dictionary<string, SwPartDoc>();
		// filename to assembly map
		public Dictionary<string, SwAssemblyDoc> Assemblies { get; set; } = new Dictionary<string, SwAssemblyDoc>();

		// material mapper keeps track of which materials are being used by which entity
		public Materials.MaterialMapper MaterialMapper { get; private set; } = new Materials.MaterialMapper();

		private uint FaceCounter = 1;
		public uint NewFaceID { get { return FaceCounter++; } }

		public Dictionary<int, SwMaterial> SwMatID2Mat { get; } = new Dictionary<int, SwMaterial>();

		private Processor _processor = null;
		public Processor Processor { get { return _processor; } }

		public bool bDirectLinkAutoSync { get; set; } = false;
		public bool bDirectLinkRequestedManualSync { get; set; } = false;

		public bool bIsDirty { get; set; } = false;

		// Used with global (unlinked) display states.
		// Tracks which materials are assigned to components for a specific display state.
		class DisplayStateUsage
		{
			public DisplayStateUsage(string InName)
			{
				Name = InName;
			}
			public string Name;
			public Dictionary<IComponent2, SwMaterial> ComponentMaterails = new Dictionary<IComponent2, SwMaterial>();
		};

		// accessory used during export
		public HashSet<int> AddedMaterials = new HashSet<int>();

		public void AddDocument(PartDoc doc)
		{
			var swDoc = new SwPartDoc(doc);
			Parts.Add(swDoc.PathName, swDoc);
		}

		public void AddDocument(AssemblyDoc doc)
		{
			var swDoc = new SwAssemblyDoc(doc);
			Assemblies.Add(swDoc.PathName, swDoc);
		}

		public SwPartDoc FindPart(string pathName)
		{
			if (Parts.ContainsKey(pathName))
				return Parts[pathName];
			return null;
		}

		public SwScene(ModelDoc2 doc)
		{
			_doc = doc;

			_processor = new Processor(this);
			_processor.Start();
		}

		~SwScene()
		{
			Cleanup();
		}

		public void Cleanup()
		{
			_processor.Stop();
			_processor = null;
			SwSingleton.FireStopProgressEvent();
		}

		public void ExportToFile()
		{
			ExportCommand cmd = new ExportCommand();
			cmd.Path = System.Environment.GetFolderPath(System.Environment.SpecialFolder.Desktop);
			cmd.SceneName = Path.GetFileNameWithoutExtension(_doc.GetPathName());
			_processor.AddCommand(cmd);
		}

		static public string GetBodyPath(IBody2 body, IModelDoc2 doc)
		{
			string path = "";
			if (body != null && doc != null)
			{
				var title = doc.GetPathName();
				var name = body.Name;
				path = title + "\\\\" + name;
			}
			return path;
		}

		static public string GetFeaturePath(IFeature feature, IModelDoc2 doc)
		{
			string path = "";
			if (feature != null && doc != null)
			{
				var title = doc.GetPathName();
				string name = feature.Name;
				path = title + "\\\\" + name;
			}
			return path;
		}

		static public void ResetFaceID(IFace2 face)
		{
			face.SetFaceId(0);
		}

		static public void SetFaceID(IFace2 face, uint id)
		{
			face.SetFaceId(unchecked((int)id));
		}

		static public uint GetFaceID(IFace2 face)
		{
			uint id = unchecked((uint)face.GetFaceId());
			return id;
		}

		// material collection
		public void CollectMaterials(ModelDoc2 doc = null)
		{
			if (doc == null)
				doc = Doc;

			IModelDocExtension ext = doc.Extension;

			int numMaterials = ext.GetRenderMaterialsCount2((int)swDisplayStateOpts_e.swThisDisplayState, null);

			if (numMaterials > 0)
			{
				object[] materials = ext.GetRenderMaterials2((int)swDisplayStateOpts_e.swThisDisplayState, null);
				foreach (var omm in materials)
				{
					var mm = omm as RenderMaterial;
					int numUsers = mm.GetEntitiesCount();
					if (numUsers > 0)
					{
						SwSingleton.FireProgressEvent("Extracting Material " + Path.GetFileNameWithoutExtension(mm.FileName));

						SwMaterial mat = MaterialMapper.AddMaterial(mm, ext);

						if (!SwMatID2Mat.ContainsKey(mat.ID))
						{
							SwMatID2Mat.Add(mat.ID, mat);
						}
						else
						{
							// todo: decide what should happen
							// todo: decide what should happen
						}

						MaterialCommand cmd = new MaterialCommand();
						cmd.Material = mat;
						_processor.AddCommand(cmd);

						object[] users = mm.GetEntities();
						foreach (var user in users)
						{
							if (user is IPartDoc part)
							{
								IModelDoc2 partModel = part as IModelDoc2;
								var path = partModel.GetPathName();
								MaterialMapper.SetMaterialUser(Materials.MaterialMapper.EntityType.MU_PART, path, mat);
								continue;
							}

							if (user is IBody2 body)
							{
								string path = GetBodyPath(body, doc);
								MaterialMapper.SetMaterialUser(Materials.MaterialMapper.EntityType.MU_BODY, path, mat);
								continue;
							}

							if (user is IFeature feat)
							{
								var path = GetFeaturePath(feat, doc);
								MaterialMapper.SetMaterialUser(Materials.MaterialMapper.EntityType.MU_FEATURE, path, mat);
								continue;
							}

							if (user is IFace2 face)
							{
								uint id = GetFaceID(face);
								if (id <= 0)
								{
									SetFaceID(face, NewFaceID);
									id = GetFaceID(face);
								}
								MaterialMapper.SetMaterialUser(Materials.MaterialMapper.EntityType.MU_FACE, id, mat);
								continue;
							}

							if (user is IComponent2 component)
							{
								MaterialMapper.SetMaterialUser(Materials.MaterialMapper.EntityType.MU_COMPONENT, component.Name, mat);

								var cmcmd = new ComponentMaterialCommand() { Material = mat, ComponentName = component.Name };
								_processor.AddCommand(cmcmd);

								continue;
							}
						}
					}
				}
			}
		}

		// Get list of all materials assigned to components for a particular configuration. Mapping is saved to 'materialMapping',
		// assigning a material to the component name. Note that only difference from the default configuration is stored.
		public void CollectMaterialsForConfiguration(ModelDoc2 doc, string[] displayStates, Dictionary<string, SwMaterial> materialMapping)
		{
			IModelDocExtension ext = doc.Extension;

			int numMaterials = ext.GetRenderMaterialsCount2((int)swDisplayStateOpts_e.swSpecifyDisplayState, displayStates);

			if (numMaterials > 0)
			{
				object[] materials = ext.GetRenderMaterials2((int)swDisplayStateOpts_e.swSpecifyDisplayState, displayStates);

				foreach (var omm in materials)
				{
					var mm = omm as RenderMaterial;
					int numUsers = mm.GetEntitiesCount();
					if (numUsers > 0)
					{
						SwMaterial mat = MaterialMapper.FindOrAddMaterial(mm, ext);

						object[] users = mm.GetEntities();
						foreach (var user in users)
						{
							if (user is IComponent2 component)
							{
								if (!materialMapping.ContainsKey(component.Name)) // this happens and triggers an exception
									materialMapping.Add(component.Name, mat);
								else
								{
									// TODO DEFINE BEHAVIOUR
								}
							}
						}
					}
				}
			}
		}

		// builds component tree and relative datasmith objects
		public void EvaluateScene(bool fast = false)
		{
			if (_doc != null)
			{
				if (_doc is AssemblyDoc)
				{
					EvaluateScene(_doc as AssemblyDoc, fast);
				}
				else if (_doc is PartDoc)
				{
					EvaluateScene(_doc as PartDoc, fast);
				}
			}
			if (!fast)
			{
				bool exportcurrentviewonly = false; //Waiting for Epic decision on this subject
				if (exportcurrentviewonly == false)
				{
					ExportAllUserViewsandCameras();
				}
				else
				{
					ExportCurrentViewOnly();
				}
			}
		}

		public void EvaluateSceneTransforms()
		{
			if (_doc != null)
			{
				if (_doc is AssemblyDoc)
				{
					EvaluateSceneTransforms(_doc as AssemblyDoc);
				}
			}
		}

		public void EvaluateSceneForDocument(ModelDoc2 doc, bool fast)
		{
			if (doc != null)
			{
				var config = ((doc as ModelDoc2).GetActiveConfiguration() as Configuration);
				if (config != null)
				{
					var root = config.GetRootComponent3(true);
					if (root != null)
						EvaluateSceneRecurse(root, null, fast);
				}
			}
		}
		
		private void ExtractLightweightComponent(Component2 component, string parentComponentName)
		{
			float[] tris = null;
			float[] normals = null;
			SwLightweightMaterial material = null;

			tris = component.GetTessTriangles(true) as float[];
			if (tris != null && tris.Length > 0)
			{
				normals = component.GetTessNorms();
				if (normals != null)
				{
					if (normals.Length != tris.Length)
					{
						normals = null;
						tris = null;
					}
					else
					{
						// [ R, G, B, Ambient, Diffuse, Specular, Shininess, Transparency, Emission ]
						double[] matProps = component.MaterialPropertyValues as double[];
						if (matProps != null && matProps.Length == 9)
						{
							material = new SwLightweightMaterial(matProps); 
							var ext = Doc.Extension;
							var settings = ext.GetDisplayStateSetting(1); // 1 = swThisDisplayState
							settings.Entities = new Component2[1] { component };
							object[] appearances = ext.DisplayStateSpecMaterialPropertyValues[settings] as object[];
							if (appearances != null && appearances.Length > 0)
							{
								var appearance = appearances[0] as IAppearanceSetting;
								if (appearance != null)
									material.SetAppearance(appearance);
							}
						}
					}
				}
			}

			var componentName = component.Name2;
			var componentTransform = component.GetTotalTransform(true);
			if (componentTransform == null)
				componentTransform = component.Transform2;

			var cmd = new LightweightComponentCommand();

			string[] NameComponents = componentName.Split('/');
			Debug.Assert(NameComponents.Length > 0);

			cmd.Label = NameComponents.Last();
			cmd.Name = componentName;
			cmd.ParentName = parentComponentName;
			if (componentTransform != null)
				cmd.Transform = MathUtil.ConvertFromSolidworksTransform(componentTransform);
			cmd.Vertices = tris;
			cmd.Normals = normals;
			cmd.Material = material;

			dynamic visibility = component.GetVisibilityInAsmDisplayStates((int)swDisplayStateOpts_e.swThisDisplayState, null);
			if (visibility != null)
			{
				int visible = visibility[0];
				if (visible == (int)swComponentVisibilityState_e.swComponentHidden)
				{
					cmd.Visible = false;
				}
			}
			Processor.AddCommand(cmd);
		}

		public void EvaluateComponent(Component2 component, string parentComponentName, bool fast)
		{
			if (component.IsSuppressed())
			{
				SwSingleton.FireProgressEvent("Extracting Lightweight geometry for " + component.Name2);
				ExtractLightweightComponent(component, parentComponentName);
			}
			else
			{
				object modelDoc = (object)component.GetModelDoc2();
				SwPartDoc part = null;
				if (modelDoc is PartDoc)
				{
					string referencedPart = (modelDoc as ModelDoc2).GetPathName();
					part = FindPart(referencedPart);
				}

				var componentName = component.Name2;

				var componentTransform = component.GetTotalTransform(true);
				if (componentTransform == null)
					componentTransform = component.Transform2;

				var cmd = new ComponentCommand();

				string[] NameComponents = componentName.Split('/');
				Debug.Assert(NameComponents.Length > 0);

				cmd.Label = NameComponents.Last();
				cmd.Name = componentName;
				cmd.ParentName = parentComponentName;

				if (componentTransform != null)
					cmd.Transform = MathUtil.ConvertFromSolidworksTransform(componentTransform);
				if (part != null)
				{
					cmd.PartName = part.Name;
					cmd.PartPath = part.PathName;
				}
				dynamic visibility = component.GetVisibilityInAsmDisplayStates((int)swDisplayStateOpts_e.swThisDisplayState, null);
				if (visibility != null)
				{
					int visible = visibility[0];
					if (visible == (int)swComponentVisibilityState_e.swComponentHidden)
					{
						cmd.Visible = false;
					}
				}
				Processor.AddCommand(cmd);

				if ((!fast) && (parentComponentName != null))
				{
					MetadataCommand mc = new MetadataCommand(MetadataCommand.MetadataType.Actor);
					if (mc != null)
					{
						mc.MetadataOwnerName = cmd.Name;
						SwMetaDataManager.AddAssemblyComponentMetadata(component, mc);
						Processor.AddCommand(mc);
					}
				}
			}
		}

		private void EvaluateSceneRecurse(Component2 component, string parentComponentName, bool fast)
		{
			EvaluateComponent(component, parentComponentName, fast);

			var children = (Object[])component.GetChildren();
			foreach (var obj in children)
			{
				Component2 child = (Component2)obj;
				EvaluateSceneRecurse(child, component.Name2, fast);
			}
		}

		public void EvaluateComponentTransform(Component2 component, ComponentTransformMultiCommand cmd)
		{
			object modelDoc = (object)component.GetModelDoc2();

			var componentName = component.Name2;

			var componentTransform = component.GetTotalTransform(true);
			if (componentTransform == null)
				componentTransform = component.Transform2;
			if (componentTransform != null)
				cmd.Transforms.Add(new Tuple<string, float[]>(componentName, MathUtil.ConvertFromSolidworksTransform(componentTransform)));
		}

		private void EvaluateSceneTransformsRecurse(Component2 component, ComponentTransformMultiCommand cmd)
		{
			EvaluateComponentTransform(component, cmd);
			var children = (Object[])component.GetChildren();
			foreach (var obj in children)
			{
				Component2 child = (Component2)obj;
				EvaluateSceneTransformsRecurse(child, cmd);
			}
		}

		// Note: "public" is used here just to make class's reflection accessible for XmlSerializer
		public class NodeConfig
		{
			public string ConfigName = null;

			public bool Visible;
			public bool Suppressed;
#if WRITE_CONFIGS_TO_XML
			public string MaterialName; // used just for debug purposes
#endif
			// Node transform. Null value if transform is not changed in this configuration.
			public float[] Transform;
			public float[] RelativeTransform;

			public SwMaterial GetMaterial()
			{
				return _Material;
			}
			public void SetMaterial(SwMaterial value)
			{
				_Material = value;
#if WRITE_CONFIGS_TO_XML
				MaterialName = value != null ? value.Name : null;
#endif
			}

			// Protected, just for correct XML serialization.
			// Null value if configuration doesn't override the material.
			SwMaterial _Material;

			public void CopyFrom(NodeConfig input)
			{
				Visible = input.Visible;
				Suppressed = input.Suppressed;
				SetMaterial(input.GetMaterial());
				Transform = input.Transform;
				RelativeTransform = input.RelativeTransform;
			}
		};

		public class NodeInfo
		{
			public string ComponentName;
			public int ComponentID;

			// Common configuration data
			public NodeConfig CommonConfig = new NodeConfig();
			// Per-configuration data
			public List<NodeConfig> Configurations = null;
			// true when configurations doesn't change the node visibility
			public bool VisibilitySame = true;
			// true when configurations doesn't change the node suppression
			public bool SuppressionSame = true;
			// true when configurations doesn't change the node material
			public bool MaterialSame = true;

			public string PartName;
			public string PartPath;
			public List<NodeInfo> Children;

			public NodeConfig AddConfiguration(string configurationName)
			{
				if (Configurations == null)
					Configurations = new List<NodeConfig>();
				NodeConfig result = new NodeConfig();
				result.ConfigName = configurationName;
				Configurations.Add(result);
				return result;
			}

			public NodeConfig GetConfiguration(string configurationName)
			{
				if (Configurations == null)
					return null;
				foreach (NodeConfig config in Configurations)
				{
					if (config.ConfigName == configurationName)
						return config;
				}
				return null;
			}

			// Add all parameters except those which could be configuration-specific
			public void AddParametersFrom(NodeInfo input)
			{
				ComponentName = input.ComponentName;
				ComponentID = input.ComponentID;
				PartName = input.PartName;
				PartPath = input.PartPath;
			}
		};

		private void MergeConfigurationTrees(NodeInfo combined, NodeInfo tree, string configurationName, Dictionary<string, SwMaterial> materialOverrides)
		{
			if (tree.Children != null)
			{
				if (combined.Children == null)
				{
					combined.Children = new List<NodeInfo>();
				}

				foreach (NodeInfo child in tree.Children)
				{
					// Find the same node in 'combined', merge parameters
					NodeInfo combinedChild = combined.Children.FirstOrDefault(x => x.ComponentName == child.ComponentName);

					if (combinedChild == null)
					{
						// The node doesn't exist yet, so add it
						combinedChild = new NodeInfo();
						combinedChild.AddParametersFrom(child);
						combined.Children.Add(combinedChild);
						// Copy common materials, which should be "default" for the component. Do not propagate these
						// material to configurations, so configuration will only have material when it is changed.
						combined.CommonConfig.SetMaterial(child.CommonConfig.GetMaterial());
					}

					// Make a NodeConfig and copy parameter values from 'tree' node
					NodeConfig nodeConfig = combinedChild.AddConfiguration(configurationName);
					nodeConfig.CopyFrom(child.CommonConfig);

					// Apply the material override for this configuration if any
					if (materialOverrides != null && materialOverrides.ContainsKey(child.ComponentName))
					{
						nodeConfig.SetMaterial(materialOverrides[child.ComponentName]);
					}

					// Recurse to children
					MergeConfigurationTrees(combinedChild, child, configurationName, materialOverrides);
				}
			}
		}

		// Find the situation when configuration doesn't change anything, and put such data into CommonConfig
		private void CompressConfigurationData(NodeInfo node)
		{
			if (node.Configurations != null && node.Configurations.Count > 0)
			{
				// Check transform
				float[] transform = node.Configurations[0].RelativeTransform;
				bool bAllTransformsAreSame = true;
				if (transform != null)
				{
					// There could be components without a transform, so we're checking if for null
					for (int i = 1; i < node.Configurations.Count; i++)
					{
						if (!node.Configurations[i].RelativeTransform.SequenceEqual(transform))
						{
							bAllTransformsAreSame = false;
							break;
						}
					}
					if (bAllTransformsAreSame)
					{
						node.CommonConfig.RelativeTransform = transform;
						foreach (NodeConfig config in node.Configurations)
						{
							config.RelativeTransform = null;
						}
					}
				}

				// Check materials
				SwMaterial material = node.Configurations[0].GetMaterial();
				bool bAllMaterialsAreSame = true;
				for (int i = 1; i < node.Configurations.Count; i++)
				{
					if (node.Configurations[i].GetMaterial() != material)
					{
						bAllMaterialsAreSame = false;
						break;
					}
				}
				if (bAllMaterialsAreSame && material != null)
				{
					// We're explicitly checking for 'material != null' to not erase default
					// material when no overrides detected.
					node.CommonConfig.SetMaterial(material);
					foreach (NodeConfig config in node.Configurations)
					{
						config.SetMaterial(null);
					}
				}

				bool bVisible = node.Configurations[0].Visible;
				bool bSuppressed = node.Configurations[0].Suppressed;
				bool bVisibilitySame = true;
				bool bSuppressionSame = true;
				for (int i = 1; i < node.Configurations.Count; i++)
				{
					if (node.Configurations[i].Visible != bVisible)
						bVisibilitySame = false;
					if (node.Configurations[i].Suppressed != bSuppressed)
						bSuppressionSame = false;
				}
				// Propagate common values
				node.MaterialSame = bAllMaterialsAreSame;
				node.VisibilitySame = bVisibilitySame;
				node.SuppressionSame = bSuppressionSame;
				
				if (bVisibilitySame)
					node.CommonConfig.Visible = bVisible;
				if (bSuppressionSame)
					node.CommonConfig.Suppressed = bSuppressed;
				//todo: store bAll...Same in ConfigData

				// if EVERYTHING is same, just remove all configurations at all
				if (bAllTransformsAreSame && bAllMaterialsAreSame && bVisibilitySame && bSuppressionSame)
				{
					node.Configurations = null;
				}
			}

			// Recurse to children
			if (node.Children != null)
			{
				foreach (NodeInfo child in node.Children)
				{
					CompressConfigurationData(child);
				}
			}
		}

		private void FillConfigurationCommand(NodeInfo node, string configurationName, ConfigurationDataCommand.Configuration target)
		{
			NodeConfig nodeConfig = node.GetConfiguration(configurationName);

			// Visibility or suppression flags are set per node, and not propagated to childre.
			// Also, suppression doesn't mark node as invisible. We should process these separately
			// to exclude any variant information from invisible nodes and their children.
			if ((node.VisibilitySame && !node.CommonConfig.Visible) || (nodeConfig != null && !nodeConfig.Visible) ||
				(node.SuppressionSame && node.CommonConfig.Suppressed) || (nodeConfig != null && nodeConfig.Suppressed))
			{
				// This node is not visible
				target.ComponentVisibility.Add(node.ComponentName, false);
				// Do not process children of this node
				return;
			}

			// Process current configuration
			if (nodeConfig != null)
			{
				// Only add variant information if attribute is not the same in all configurations
				if (nodeConfig.RelativeTransform != null)
					target.ComponentTransform.Add(node.ComponentName, nodeConfig.RelativeTransform);
				if (!node.VisibilitySame)
					target.ComponentVisibility.Add(node.ComponentName, nodeConfig.Visible);
				if (!node.SuppressionSame)
					target.ComponentVisibility.Add(node.ComponentName, !nodeConfig.Suppressed);
				if (!node.MaterialSame)
				{
					SwMaterial material = nodeConfig.GetMaterial();
					if (material == null)
					{
						// There's no material set for the config, use default one
						material = node.CommonConfig.GetMaterial();
					}
					if (material != null)
					{
						target.ComponentMaterial.Add(node.ComponentName, material);
					}
				}
			}	

			// Recurse to children
			if (node.Children != null)
			{
				foreach (NodeInfo child in node.Children)
				{
					FillConfigurationCommand(child, configurationName, target);
				}
			}
		}

		private void CollectComponentsRecurse(Component2 component, NodeInfo parentNode, DisplayStateUsage DSU)
		{
			NodeInfo newNode = new NodeInfo();
			parentNode.Children.Add(newNode);

			// Basic properties
			newNode.ComponentName = component.Name2;
			newNode.ComponentID = component.GetID();
			newNode.CommonConfig.Visible = component.Visible != (int)swComponentVisibilityState_e.swComponentHidden;
			newNode.CommonConfig.Suppressed = component.IsSuppressed();

			// Read transform
			var componentTransform = component.GetTotalTransform(true);
			if (componentTransform == null)
				componentTransform = component.Transform2;

			if (componentTransform != null)
			{
				newNode.CommonConfig.Transform = MathUtil.ConvertFromSolidworksTransform(componentTransform);

				if (parentNode.CommonConfig.Transform != null)
				{
					// Convert transform to parent space (original transform value fetched from Solidworks
					// is in the root component's space). Datasmith wants relative transform for variants.
					Matrix4 parentTransform = new Matrix4(parentNode.CommonConfig.Transform);
					Matrix4 inverseParentTransform = parentTransform.Inverse();
					newNode.CommonConfig.RelativeTransform = Matrix4.Matrix4x4Multiply(inverseParentTransform, newNode.CommonConfig.Transform);
				}
				else
				{
					newNode.CommonConfig.RelativeTransform = newNode.CommonConfig.Transform;
				}
			}

			// Read part information
			object modelDoc = (object)component.GetModelDoc2();
			SwPartDoc part = null;
			if (modelDoc is PartDoc)
			{
				string referencedPart = (modelDoc as ModelDoc2).GetPathName();
				part = FindPart(referencedPart);
			}
			if (part != null)
			{
				newNode.PartName = part.Name;
				newNode.PartPath = part.PathName;
			}

			// Read material (appearance) information.

			if (DSU != null)
			{
				if (DSU.ComponentMaterails.ContainsKey(component))
				{
					newNode.CommonConfig.SetMaterial(DSU.ComponentMaterails[component]);
				}
			}
			else
			{
				// swThisDisplayState provides the most correct default material information.
				int displayStateOption = (int)swDisplayStateOpts_e.swThisDisplayState;
				int numMaterials = component.GetRenderMaterialsCount2(displayStateOption, null);
				if (numMaterials > 0)
				{
					object[] swMaterials = component.GetRenderMaterials2(displayStateOption, null);
					// Materials here are ones which assigned to parts, ordered by hierarchy.
					// Get the last one in the list as the prioritized override.
					RenderMaterial partMaterial = swMaterials[swMaterials.Length - 1] as RenderMaterial;
					SwMaterial swMaterial = MaterialMapper.FindOrAddMaterial(partMaterial, Doc.Extension);
					newNode.CommonConfig.SetMaterial(swMaterial);
				}
			}

			// Process children components
			var children = (Object[])component.GetChildren();
			if (children.Length > 0)
			{
				newNode.Children = new List<NodeInfo>();
				foreach (var obj in children)
				{
					Component2 child = (Component2)obj;
					CollectComponentsRecurse(child, newNode, DSU);
				}

				newNode.Children.Sort(delegate (NodeInfo a, NodeInfo b)
					{
						return a.ComponentID - b.ComponentID;
					});
			}
		}

		private void ExportRegularConfigurations(ModelDoc2 Doc)
		{
			string[] CfgNames = (Doc as ModelDoc2).GetConfigurationNames();

			if (CfgNames.Length <= 1)
			{
				return;
			}

			NodeInfo RootNode = new NodeInfo();
			RootNode.Children = new List<NodeInfo>();

			NodeInfo CombinedTree = new NodeInfo();
			CombinedTree.ComponentName = "CombinedTree";
			// Ensure recursion will not stop on the root node (this may happen if it is not explicitly marked as visible)
			CombinedTree.VisibilitySame = true;
			CombinedTree.CommonConfig.Visible = true;

			foreach (string CfgName in CfgNames)
			{
				IConfiguration swConfiguration = (Doc as ModelDoc2).GetConfigurationByName(CfgName) as IConfiguration;

				NodeInfo ConfigNode = new NodeInfo();
				ConfigNode.Children = new List<NodeInfo>();
				ConfigNode.ComponentName = CfgName;
				RootNode.Children.Add(ConfigNode);

				int DisplayStateCount = swConfiguration.GetDisplayStatesCount();
				string[] DisplayStates = null;
				if (DisplayStateCount > 0)
				{
					DisplayStates = swConfiguration.GetDisplayStates();
				}

				// Get per-configuration materials
				Dictionary<string, SwMaterial> ConfigurationMaterials = new Dictionary<string, SwMaterial>();
				CollectMaterialsForConfiguration(Doc, DisplayStates, ConfigurationMaterials);

				// Build the tree and get default materials (which aren't affected by any configuration)
				// Use GetRootComponent3() with Resolve = true to ensure suppressed components will be loaded
				CollectComponentsRecurse(swConfiguration.GetRootComponent3(true), ConfigNode, null);

				// Combine separate scene trees into the single one with configuration-specific data
				MergeConfigurationTrees(CombinedTree, ConfigNode, CfgName, ConfigurationMaterials);
			}

			SubmitConfigurationCommand("Configurations", CfgNames, CombinedTree);

#if WRITE_CONFIGS_TO_XML
			// Save configuration parameters to separate xml files for easier analysis
			string baseFilename = "C:\\TEMP\\objects-" + (System.Environment.TickCount / 10).ToString("X") + "-";
			/*foreach (NodeInfo cfgRoot in rootNode.Children)
			{
				StringBuilder sb = new StringBuilder();
				using (System.Xml.XmlWriter writer = System.Xml.XmlWriter.Create(sb, new System.Xml.XmlWriterSettings() { Indent = true, OmitXmlDeclaration = true }))
				{
					System.Xml.Serialization.XmlSerializer x = new System.Xml.Serialization.XmlSerializer(typeof(NodeInfo));
					x.Serialize(writer, cfgRoot);
				}
				System.IO.File.WriteAllText(baseFilename + cfgRoot.ComponentName + ".xml", sb.ToString());
			}*/
#endif // WRITE_CONFIGS_TO_XML


#if WRITE_CONFIGS_TO_XML
			// Save combined tree to a single xml file
			StringBuilder sb2 = new StringBuilder();
			using (System.Xml.XmlWriter writer = System.Xml.XmlWriter.Create(sb2, new System.Xml.XmlWriterSettings() { Indent = true, OmitXmlDeclaration = true }))
			{
				System.Xml.Serialization.XmlSerializer x = new System.Xml.Serialization.XmlSerializer(typeof(NodeInfo));
				x.Serialize(writer, combinedTree);
			}
			System.IO.File.WriteAllText(baseFilename + "combined.xml", sb2.ToString());
#endif // WRITE_CONFIGS_TO_XML

		}

		private void ExportDisplayStatesAsConfigurations(ModelDoc2 Doc, string[] DisplayStates)
		{
			Debug.Assert(DisplayStates != null && DisplayStates.Length > 0);

			NodeInfo CombinedTree = new NodeInfo();
			CombinedTree.ComponentName = "CombinedTree";
			// Ensure recursion will not stop on the root node (this may happen if it is not explicitly marked as visible)
			CombinedTree.VisibilitySame = true;
			CombinedTree.CommonConfig.Visible = true;

			IModelDocExtension Ext = Doc.Extension;

			DisplayStateUsage[] DisplayStatesData = new DisplayStateUsage[DisplayStates.Length];

			for(int DisplayStateIndex = 0; DisplayStateIndex < DisplayStates.Length; ++DisplayStateIndex)
			{
				string DisplayStateName = DisplayStates[DisplayStateIndex];
				string[] DisplayStateAsArray = new string[]{ DisplayStateName };
				int NumMaterials = Ext.GetRenderMaterialsCount2((int)swDisplayStateOpts_e.swSpecifyDisplayState, DisplayStateAsArray);

				if (NumMaterials > 0)
				{
					object[] Materials = Ext.GetRenderMaterials2((int)swDisplayStateOpts_e.swSpecifyDisplayState, DisplayStateAsArray);

					DisplayStateUsage DSU = new DisplayStateUsage(DisplayStateName);

					DisplayStatesData[DisplayStateIndex] = DSU;
					
					foreach (object Mat in Materials)
					{
						RenderMaterial RenderMat = Mat as RenderMaterial;
						int NumUsers = RenderMat.GetEntitiesCount();
						if (NumUsers > 0)
						{
							SwMaterial SwMat = MaterialMapper.FindOrAddMaterial(RenderMat, Ext);

							object[] Users = RenderMat.GetEntities();
							foreach (var User in Users)
							{
								if (User is IComponent2 Component)
								{
									DSU.ComponentMaterails[Component] = SwMat;
								}
							}
						}
					}
				}
			}

			Configuration ActiveConfig = (Configuration)Doc.GetActiveConfiguration();

			for (int DisplayStateIndex = 0; DisplayStateIndex < DisplayStates.Length; ++DisplayStateIndex)
			{
				string CfgName = DisplayStates[DisplayStateIndex];
				NodeInfo ConfigNode = new NodeInfo();
				ConfigNode.Children = new List<NodeInfo>();
				ConfigNode.ComponentName = CfgName;

				// Build the tree and get default materials (which aren't affected by any configuration)
				// Use GetRootComponent3() with Resolve = true to ensure suppressed components will be loaded
				CollectComponentsRecurse(ActiveConfig.GetRootComponent3(true), ConfigNode, DisplayStatesData[DisplayStateIndex]);

				// Combine separate scene trees into the single one with configuration-specific data
				MergeConfigurationTrees(CombinedTree, ConfigNode, CfgName, null);
			}

			SubmitConfigurationCommand("DisplayStates", DisplayStates, CombinedTree);
		}

		private void SubmitConfigurationCommand(string ConfigurationSetName, string[] Configurations, NodeInfo Tree)
		{
			// Remove configuration data when it's the same
			CompressConfigurationData(Tree);

			var Cmd = new ConfigurationDataCommand();
			Cmd.ConfigurationsSetName = ConfigurationSetName;

			// Add roots for configurations
			foreach (string CfgName in Configurations)
			{
				ConfigurationDataCommand.Configuration Cfg = new ConfigurationDataCommand.Configuration();
				Cfg.Name = CfgName;
				Cmd.Configurations.Add(Cfg);
				FillConfigurationCommand(Tree, CfgName, Cfg);
			}

			Processor.AddCommand(Cmd);
		}

		private void CollectConfigurationData(AssemblyDoc doc)
		{
			ModelDoc2 Doc2 = doc as ModelDoc2;

			// Check if display states are linked: if not, export them as variants (it they are, they'll be 
			// exported as part of their respective linked configs)
            ConfigurationManager ConfigManager = Doc2.ConfigurationManager;
            if (!ConfigManager.LinkDisplayStatesToConfigurations)
			{
				Configuration ActiveConfig = (Configuration)Doc2.GetActiveConfiguration();
				int DisplayStateCount = ActiveConfig.GetDisplayStatesCount();
				if (DisplayStateCount > 1)
				{
					string[] DisplayStates = ActiveConfig.GetDisplayStates();
					ExportDisplayStatesAsConfigurations(Doc2, DisplayStates);
				}
			}

			ExportRegularConfigurations(Doc2);
		}

		private void EvaluateSceneTransforms(AssemblyDoc doc)
		{
			var config = ((doc as ModelDoc2).GetActiveConfiguration() as Configuration);
			var root = config.GetRootComponent3(true);

			var cmd = new ComponentTransformMultiCommand();
			EvaluateSceneTransformsRecurse(root, cmd);
			Processor.AddCommand(cmd);
		}

		private void EvaluateScene(AssemblyDoc doc, bool fast = false)
		{
			var config = ((doc as ModelDoc2).GetActiveConfiguration() as Configuration);
			var root = config.GetRootComponent3(true);
			EvaluateSceneRecurse(root, null, fast);
			if (!fast)
				CollectConfigurationData(doc);
		}

		private void EvaluateScene(PartDoc doc, bool fast = false)
		{
			foreach (var pp in Parts)
			{
				var part = pp.Value;

				var cmd = new ComponentCommand();
				cmd.Name = part.Name;
				cmd.PartName = part.Name;
				cmd.PartPath = part.PathName;
				Processor.AddCommand(cmd);
				if (!fast)
					SendModelDocMetadataToProcessor(doc as ModelDoc2, part.Name, MetadataCommand.MetadataType.Actor);
			}
		}

		public void DirectLinkUpdate()
		{
			foreach (var pp in Parts)
			{
				pp.Value.Load(true);
			}

			CollectMaterials();
			EvaluateScene(true);
			var cmd = new LiveUpdateCommand();
			Processor.AddCommand(cmd);
			bIsDirty = false;
		}

		private void ExportCurrentViewOnly()
		{
			SwCamera currentviewcamera = new SwCamera(_doc.ActiveView, "CurrentView");
			if (currentviewcamera.IsValid)
			{
				var cmd = new CameraCommand() { camera = currentviewcamera };
				Processor.AddCommand(cmd);
			}
		}

		private void ExportAllUserViewsandCameras()
		{
			ExportAllUserViews();
			ExportAllCameras();
		}

		private void ExportAllUserViews()
		{
			SwCamera currentviewcamera = null;
			string viewname = "";
			ModelView beforeparsingview = _doc.ActiveView;
			string beforeparsingviewname = "";
			int nbmodelviews = _doc.GetModelViewCount();
			string[] modelviewnames = (string[])_doc.GetModelViewNames();

			for (int i = 0; i < nbmodelviews; i++)
			{
				viewname = modelviewnames[i];
				if (viewname.Contains("*")) //only way we found to tell a SW view from a user created view
				{
					continue;
				}
				_doc.ShowNamedView2(viewname, -1); //have to stage/"realize"/"set as current" the camera otherwise camera parameters are not good
				ModelView view = _doc.ActiveView;
				if (string.IsNullOrEmpty(beforeparsingviewname) && view == beforeparsingview)
				{
					beforeparsingviewname = viewname;
				}


				if (viewname != null)
				{
					currentviewcamera = new SwCamera(view, viewname);
					if (currentviewcamera.IsValid)
					{
						var cmd = new CameraCommand() { camera = currentviewcamera };
						Processor.AddCommand(cmd);
					}
				}
			}
			//trying and failing to restore the original and active model view before exporting to datasmith
			_doc.ShowNamedView2(beforeparsingviewname, -1);
			int[] emptyarray = new int[4];
			beforeparsingview.GraphicsRedraw(emptyarray);
		}

		private void ExportAllCameras()
		{
			ModelView currentmodelview = _doc.ActiveView;
			Camera originalcamera = currentmodelview.Camera;
			string originalcameraname = null;

			Feature feature = _doc.FirstFeature();
			bool[] suppressedarray = new bool[1];

			while (feature != null)
			{
				Feature subfeature = feature.GetFirstSubFeature();
				while (subfeature != null && suppressedarray[0] == false)
				{
					string subfeaturetype = subfeature.GetTypeName2();
					if (subfeaturetype != null && subfeaturetype == "CameraFeature")
					{
						Camera solidworksapicam = (Camera)subfeature.GetSpecificFeature2();
						suppressedarray = (bool[])subfeature.IsSuppressed2((int)swInConfigurationOpts_e.swThisConfiguration, null);
						if (solidworksapicam != null && suppressedarray[0] == false)
						{
							string cameraname = subfeature.Name;
							if (originalcamera == solidworksapicam)
							{
								originalcameraname = cameraname;
							}
							currentmodelview.SetCameraByName(cameraname); //have to stage/"realize"/"set as current" the camera otherwise camera parameters are not good
							SwCamera exportedcamera = new SwCamera(solidworksapicam, cameraname);
							if (exportedcamera.IsValid)
							{
								var cmd = new CameraCommand() { camera = exportedcamera };
								Processor.AddCommand(cmd);
							}
						}
					}
					subfeature = subfeature.GetNextSubFeature();
				}
				feature = feature.GetNextFeature();
			}

			if (string.IsNullOrEmpty(originalcameraname) == false)
			{
				currentmodelview.SetCameraByName(originalcameraname);
			}
			//No else as no found ways in the API to restore the original modelview
		}

		public void SendModelDocMetadataToProcessor(ModelDoc2 modeldoc2, string docname, MetadataCommand.MetadataType metadatatype)
		{
			try
			{
				MetadataCommand mc = new MetadataCommand(metadatatype);
				if (mc != null)
				{
					mc.MetadataOwnerName = docname;
					SwMetaDataManager.AddDocumentMetadataToCommand(modeldoc2, mc);
					Processor.AddCommand(mc);
				}
			}
			catch(Exception){ }
		}

		public void DeleteComponent(string itemName)
		{
			if (Doc is AssemblyDoc)
			{
				List<Component2> toDelete = new List<Component2>();
				object[] components = (Doc as AssemblyDoc).GetComponents(false);
				foreach (var cc in components)
				{
					if (cc is Component2)
					{
						var name = (cc as Component2).Name2;
						string[] names = name.Split('/');
						foreach (var n in names)
						{
							if (n == itemName)
							{
								toDelete.Add((cc as Component2));
								break;
							}
						}
					}
				}
				foreach (var cc in toDelete)
				{
					var cmd = new DeleteComponentCommand() { Name = cc.Name2 };
					Processor.AddCommand(cmd);
				}
			}
		}
	}
}
