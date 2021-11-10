// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace DatasmithSolidworks
{
	public class FConfigurationData
	{
		public string Name;
		public Dictionary<string, float[]> ComponentTransform = new Dictionary<string, float[]>();
		public Dictionary<string, bool> ComponentVisibility = new Dictionary<string, bool>();
		public Dictionary<string, FObjectMaterials> ComponentMaterials = new Dictionary<string, FObjectMaterials>();
	};

	[ComVisible(false)]
	public class FConfigurationManager
	{
		// Note: "public" is used here just to make class's reflection accessible for XmlSerializer
		private class FComponentConfig
		{
			public string ConfigName = null;

			public bool bVisible;
			public bool bSuppressed;
			// Node transform. Null value if transform is not changed in this configuration.
			public float[] Transform = null;
			public float[] RelativeTransform = null;

			// Protected, just for correct XML serialization.
			// Null value if configuration doesn't override the material.
			public FObjectMaterials Materials = null;

			public void CopyFrom(FComponentConfig InOther)
			{
				bVisible = InOther.bVisible;
				bSuppressed = InOther.bSuppressed;
				Materials = InOther.Materials;
				Transform = InOther.Transform;
				RelativeTransform = InOther.RelativeTransform;
			}
		};

		private class FComponentTreeNode
		{
			public string ComponentName;
			public int ComponentID;

			// Common configuration data
			public FComponentConfig CommonConfig = new FComponentConfig();

			// Per-configuration data
			public List<FComponentConfig> Configurations = null;

			// True when configurations doesn't change the node visibility
			public bool VisibilitySame = true;
			// True when configurations doesn't change the node suppression
			public bool SuppressionSame = true;
			// True when configurations doesn't change the node material
			public bool MaterialSame = true;

			public string PartName;
			public string PartPath;

			public List<FComponentTreeNode> Children;

			public FComponentConfig AddConfiguration(string InConfigurationName)
			{
				if (Configurations == null)
				{
					Configurations = new List<FComponentConfig>();
				}

				FComponentConfig Result = new FComponentConfig();
				Result.ConfigName = InConfigurationName;
				Configurations.Add(Result);

				return Result;
			}

			public FComponentConfig GetConfiguration(string InConfigurationName)
			{
				if (Configurations == null)
				{
					return null;
				}
				
				foreach (FComponentConfig Config in Configurations)
				{
					if (Config.ConfigName == InConfigurationName)
					{
						return Config;
					}
				}
				return null;
			}

			// Add all parameters except those which could be configuration-specific
			public void AddParametersFrom(FComponentTreeNode InInput)
			{
				ComponentName = InInput.ComponentName;
				ComponentID = InInput.ComponentID;
				PartName = InInput.PartName;
				PartPath = InInput.PartPath;
			}
		};

		public List<FConfigurationData> ExportConfigurationData(AssemblyDoc InDoc)
		{
			ModelDoc2 Doc2 = InDoc as ModelDoc2;

			List<FConfigurationData> Configurations = new List<FConfigurationData>();

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
					List<FConfigurationData> DisplayStateConfigurations = ExportDisplayStatesAsConfigurations(Doc2, DisplayStates);

					if (DisplayStateConfigurations != null)
					{
						Configurations.AddRange(DisplayStateConfigurations);
					}
				}
			}

			List<FConfigurationData> RegularConfigurations = ExportRegularConfigurations(Doc2);

			if (RegularConfigurations != null)
			{
				Configurations.AddRange(RegularConfigurations);
			}

			return Configurations;
		}

		private void MergeConfigurationTrees(FComponentTreeNode OutCombined, FComponentTreeNode InTree, string InConfigurationName, Dictionary<string, FObjectMaterials> InMaterialOverrides)
		{
			if (InTree.Children == null)
			{
				return;
			}

			if (OutCombined.Children == null)
			{
				OutCombined.Children = new List<FComponentTreeNode>();
			}

			foreach (FComponentTreeNode Child in InTree.Children)
			{
				// Find the same node in 'combined', merge parameters
				FComponentTreeNode CombinedChild = OutCombined.Children.FirstOrDefault(X => X.ComponentName == Child.ComponentName);

				if (CombinedChild == null)
				{
					// The node doesn't exist yet, so add it
					CombinedChild = new FComponentTreeNode();
					CombinedChild.AddParametersFrom(Child);
					OutCombined.Children.Add(CombinedChild);

					// Copy common materials, which should be "default" for the component. Do not propagate these
					// material to configurations, so configuration will only have material when it is changed.
					OutCombined.CommonConfig.Materials = Child.CommonConfig.Materials;
				}

				// Make a NodeConfig and copy parameter values from 'tree' node
				FComponentConfig NodeConfig = CombinedChild.AddConfiguration(InConfigurationName);
				NodeConfig.CopyFrom(Child.CommonConfig);

				// Apply the material override for this configuration if any
				if (InMaterialOverrides != null && InMaterialOverrides.ContainsKey(Child.ComponentName))
				{
					NodeConfig.Materials = InMaterialOverrides[Child.ComponentName];
				}

				// Recurse to children
				MergeConfigurationTrees(CombinedChild, Child, InConfigurationName, InMaterialOverrides);
			}
		}

		// Find the situation when configuration doesn't change anything, and put such data into CommonConfig
		private void CompressConfigurationData(FComponentTreeNode InNode)
		{
			if (InNode.Configurations != null && InNode.Configurations.Count > 0)
			{
				// Check transform
				float[] Transform = InNode.Configurations[0].RelativeTransform;
				bool bAllTransformsAreSame = true;
				if (Transform != null)
				{
					// There could be components without a transform, so we're checking if for null
					for (int Idx = 1; Idx < InNode.Configurations.Count; Idx++)
					{
						if (!InNode.Configurations[Idx].RelativeTransform.SequenceEqual(Transform))
						{
							bAllTransformsAreSame = false;
							break;
						}
					}
					if (bAllTransformsAreSame)
					{
						InNode.CommonConfig.RelativeTransform = Transform;
						foreach (FComponentConfig Config in InNode.Configurations)
						{
							Config.RelativeTransform = null;
						}
					}
				}

				// Check materials
				FObjectMaterials Materials = InNode.Configurations[0].Materials;
				bool bAllMaterialsAreSame = true;
				for (int Idx = 1; Idx < InNode.Configurations.Count; Idx++)
				{
					if (Materials != null && (InNode.Configurations[Idx].Materials?.EqualMaterials(Materials) ?? false))
					{
						bAllMaterialsAreSame = false;
						break;
					}
				}
				if (bAllMaterialsAreSame && Materials != null)
				{
					// We're explicitly checking for 'material != null' to not erase default
					// material when no overrides detected.
					InNode.CommonConfig.Materials = Materials;
					foreach (FComponentConfig Config in InNode.Configurations)
					{
						Config.Materials = null;
					}
				}

				bool bVisible = InNode.Configurations[0].bVisible;
				bool bSuppressed = InNode.Configurations[0].bSuppressed;
				bool bVisibilitySame = true;
				bool bSuppressionSame = true;
				for (int Idx = 1; Idx < InNode.Configurations.Count; Idx++)
				{
					if (InNode.Configurations[Idx].bVisible != bVisible)
					{
						bVisibilitySame = false;
					}

					if (InNode.Configurations[Idx].bSuppressed != bSuppressed)
					{
						bSuppressionSame = false;
					}
				}
				// Propagate common values
				InNode.MaterialSame = bAllMaterialsAreSame;
				InNode.VisibilitySame = bVisibilitySame;
				InNode.SuppressionSame = bSuppressionSame;
				
				if (bVisibilitySame)
				{
					InNode.CommonConfig.bVisible = bVisible;
				}
				if (bSuppressionSame)
				{
					InNode.CommonConfig.bSuppressed = bSuppressed;
				}
				
				//todo: store bAll...Same in ConfigData

				// If EVERYTHING is same, just remove all configurations at all
				if (bAllTransformsAreSame && bAllMaterialsAreSame && bVisibilitySame && bSuppressionSame)
				{
					InNode.Configurations = null;
				}
			}

			// Recurse to children
			if (InNode.Children != null)
			{
				foreach (FComponentTreeNode Child in InNode.Children)
				{
					CompressConfigurationData(Child);
				}
			}
		}

		private void FillConfigurationData(FComponentTreeNode InNode, string InConfigurationName, FConfigurationData OutData)
		{
			FComponentConfig NodeConfig = InNode.GetConfiguration(InConfigurationName);

			// Visibility or suppression flags are set per node, and not propagated to childre.
			// Also, suppression doesn't mark node as invisible. We should process these separately
			// to exclude any variant information from invisible nodes and their children.
			if ((InNode.VisibilitySame && !InNode.CommonConfig.bVisible) || (NodeConfig != null && !NodeConfig.bVisible) ||
				(InNode.SuppressionSame && InNode.CommonConfig.bSuppressed) || (NodeConfig != null && NodeConfig.bSuppressed))
			{
				// This node is not visible
				OutData.ComponentVisibility.Add(InNode.ComponentName, false);
				// Do not process children of this node
				return;
			}

			// Process current configuration
			if (NodeConfig != null)
			{
				// Only add variant information if attribute is not the same in all configurations
				if (NodeConfig.RelativeTransform != null)
				{
					OutData.ComponentTransform.Add(InNode.ComponentName, NodeConfig.RelativeTransform);
				}
				if (!InNode.VisibilitySame)
				{
					OutData.ComponentVisibility.Add(InNode.ComponentName, NodeConfig.bVisible);
				}
				if (!InNode.SuppressionSame)
				{
					OutData.ComponentVisibility.Add(InNode.ComponentName, !NodeConfig.bSuppressed);
				}
				if (!InNode.MaterialSame)
				{
					FObjectMaterials Materials = NodeConfig.Materials;
					if (Materials == null)
					{
						// There's no material set for the config, use default one
						Materials = InNode.CommonConfig.Materials;
					}
					if (Materials != null)
					{
						OutData.ComponentMaterials.Add(InNode.ComponentName, Materials);
					}
				}
			}	

			// Recurse to children
			if (InNode.Children != null)
			{
				foreach (FComponentTreeNode Child in InNode.Children)
				{
					FillConfigurationData(Child, InConfigurationName, OutData);
				}
			}
		}

		private void CollectComponentsRecurse(ModelDoc2 InDoc, Component2 InComponent, FComponentTreeNode InParentNode, Dictionary<string, FObjectMaterials> InConfigurationMaterials, string[] InDisplayStates)
		{
			FComponentTreeNode NewNode = new FComponentTreeNode();
			InParentNode.Children.Add(NewNode);

			// Basic properties
			NewNode.ComponentName = InComponent.Name2;
			NewNode.ComponentID = InComponent.GetID();
			NewNode.CommonConfig.bVisible = InComponent.Visible != (int)swComponentVisibilityState_e.swComponentHidden;
			NewNode.CommonConfig.bSuppressed = InComponent.IsSuppressed();

			// Read transform
			MathTransform ComponentTransform = InComponent.GetTotalTransform(true);
			if (ComponentTransform == null)
			{
				ComponentTransform = InComponent.Transform2;
			}

			if (ComponentTransform != null)
			{
				NewNode.CommonConfig.Transform = MathUtils.ConvertFromSolidworksTransform(ComponentTransform, 100f /*GeomScale*/);

				if (InParentNode.CommonConfig.Transform != null)
				{
					// Convert transform to parent space (original transform value fetched from Solidworks
					// is in the root component's space). Datasmith wants relative transform for variants.
					FMatrix4 ParentTransform = new FMatrix4(InParentNode.CommonConfig.Transform);
					FMatrix4 InverseParentTransform = ParentTransform.Inverse();
					NewNode.CommonConfig.RelativeTransform = FMatrix4.FMatrix4x4Multiply(InverseParentTransform, NewNode.CommonConfig.Transform);
				}
				else
				{
					NewNode.CommonConfig.RelativeTransform = NewNode.CommonConfig.Transform;
				}
			}

			// Read material (appearance) information.
			if (!InConfigurationMaterials.ContainsKey(InComponent.Name2))
			{
				// No per component material, so try to get per face/body/feature material.
				FObjectMaterials CompMaterials = null; // TODO FObjectMaterials.LoadComponentMaterials(InDoc, InComponent, swDisplayStateOpts_e.swSpecifyDisplayState, InDisplayStates);
				if (CompMaterials != null)
				{
					InConfigurationMaterials[InComponent.Name2] = CompMaterials;
				}
			}
			if (InConfigurationMaterials.ContainsKey(InComponent.Name2))
			{
				NewNode.CommonConfig.Materials = InConfigurationMaterials[InComponent.Name2];
			}

			// Process children components
			var Children = (Object[])InComponent.GetChildren();
			if (Children.Length > 0)
			{
				NewNode.Children = new List<FComponentTreeNode>();
				foreach (object ObjChild in Children)
				{
					Component2 Child = (Component2)ObjChild;
					CollectComponentsRecurse(InDoc, Child, NewNode, InConfigurationMaterials, InDisplayStates);
				}

				NewNode.Children.Sort(delegate (FComponentTreeNode InA, FComponentTreeNode InB)
				{
					return InA.ComponentID - InB.ComponentID;
				});
			}
		}

		private List<FConfigurationData> ExportRegularConfigurations(ModelDoc2 InDoc)
		{
			string[] CfgNames = (InDoc as ModelDoc2).GetConfigurationNames();

			if (CfgNames.Length <= 1)
			{
				return null;
			}

			FComponentTreeNode RootNode = new FComponentTreeNode();
			RootNode.Children = new List<FComponentTreeNode>();

			FComponentTreeNode CombinedTree = new FComponentTreeNode();
			CombinedTree.ComponentName = "CombinedTree";
			// Ensure recursion will not stop on the root node (this may happen if it is not explicitly marked as visible)
			CombinedTree.VisibilitySame = true;
			CombinedTree.CommonConfig.bVisible = true;

			foreach (string CfgName in CfgNames)
			{
				IConfiguration swConfiguration = (InDoc as ModelDoc2).GetConfigurationByName(CfgName) as IConfiguration;

				FComponentTreeNode ConfigNode = new FComponentTreeNode();
				ConfigNode.Children = new List<FComponentTreeNode>();
				ConfigNode.ComponentName = CfgName;
				RootNode.Children.Add(ConfigNode);

				int DisplayStateCount = swConfiguration.GetDisplayStatesCount();
				string[] DisplayStates = null;
				if (DisplayStateCount > 0)
				{
					DisplayStates = swConfiguration.GetDisplayStates();
				}

				// Get per-component materials
				Dictionary<string, FObjectMaterials> ConfigurationMaterials = null; // TODO FObjectMaterials.LoadDocumentMaterials(InDoc, swDisplayStateOpts_e.swSpecifyDisplayState, DisplayStates);

				// Build the tree and get default materials (which aren't affected by any configuration)
				// Use GetRootComponent3() with Resolve = true to ensure suppressed components will be loaded
				CollectComponentsRecurse(InDoc, swConfiguration.GetRootComponent3(true), ConfigNode, ConfigurationMaterials, DisplayStates);

				// Combine separate scene trees into the single one with configuration-specific data
				MergeConfigurationTrees(CombinedTree, ConfigNode, CfgName, ConfigurationMaterials);
			}

			return CompileConfigurations(CfgNames, CombinedTree);
		}

		private List<FConfigurationData> ExportDisplayStatesAsConfigurations(ModelDoc2 InDoc, string[] InDisplayStates)
		{
			Debug.Assert(InDisplayStates != null && InDisplayStates.Length > 0);

			FComponentTreeNode CombinedTree = new FComponentTreeNode();
			CombinedTree.ComponentName = "CombinedTree";
			// Ensure recursion will not stop on the root node (this may happen if it is not explicitly marked as visible)
			CombinedTree.VisibilitySame = true;
			CombinedTree.CommonConfig.bVisible = true;

			IModelDocExtension Ext = InDoc.Extension;

			Dictionary<string, FObjectMaterials> DocMaterials = null;

			for(int DisplayStateIndex = 0; DisplayStateIndex < InDisplayStates.Length; ++DisplayStateIndex)
			{
				string DisplayStateName = InDisplayStates[DisplayStateIndex];
				string[] DisplayStateAsArray = new string[]{ DisplayStateName };
				DocMaterials = null; // TODO FObjectMaterials.LoadDocumentMaterials(InDoc, swDisplayStateOpts_e.swSpecifyDisplayState, DisplayStateAsArray);
			}

			Configuration ActiveConfig = (Configuration)InDoc.GetActiveConfiguration();

			for (int DisplayStateIndex = 0; DisplayStateIndex < InDisplayStates.Length; ++DisplayStateIndex)
			{
				string CfgName = InDisplayStates[DisplayStateIndex];
				string[] DisplayStateAsArray = new string[]{ CfgName };
				FComponentTreeNode ConfigNode = new FComponentTreeNode();
				ConfigNode.Children = new List<FComponentTreeNode>();
				ConfigNode.ComponentName = CfgName;

				// Build the tree and get default materials (which aren't affected by any configuration)
				// Use GetRootComponent3() with Resolve = true to ensure suppressed components will be loaded
				CollectComponentsRecurse(InDoc, ActiveConfig.GetRootComponent3(true), ConfigNode, DocMaterials, DisplayStateAsArray);

				// Combine separate scene trees into the single one with configuration-specific data
				MergeConfigurationTrees(CombinedTree, ConfigNode, CfgName, DocMaterials);
			}

			return CompileConfigurations(InDisplayStates, CombinedTree);
		}

		private List<FConfigurationData> CompileConfigurations(string[] InConfigurations, FComponentTreeNode InTree)
		{
			// Remove configuration data when it's the same
			CompressConfigurationData(InTree);

			List<FConfigurationData> CfgDataList = new List<FConfigurationData>();

			// Add roots for configurations
			foreach (string CfgName in InConfigurations)
			{
				FConfigurationData CfgData = new FConfigurationData();
				CfgData.Name = CfgName;
				FillConfigurationData(InTree, CfgName, CfgData);
				CfgDataList.Add(CfgData);
			}

			return CfgDataList;
		}
	}
}