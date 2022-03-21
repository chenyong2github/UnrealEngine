// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;
using System.Collections.Concurrent;

namespace DatasmithSolidworks
{
	public class FConfigurationData
	{
		public string Name;
		public bool bIsDisplayStateConfiguration = false;
		public Dictionary<string, float[]> ComponentTransform = new Dictionary<string, float[]>();
		public Dictionary<string, bool> ComponentVisibility = new Dictionary<string, bool>();
		public Dictionary<string, FObjectMaterials> ComponentMaterials = new Dictionary<string, FObjectMaterials>();

		public bool IsEmpty()
		{
			return (
				ComponentTransform.Count == 0 && 
				ComponentVisibility.Count == 0 && 
				ComponentMaterials.Count == 0);
		}
	};

	[ComVisible(false)]
	public class FConfigurationExporter
	{
		public static List<FConfigurationData> ExportConfigurations(FDocument InDoc)
		{
			string[] CfgNames = InDoc.SwDoc?.GetConfigurationNames();

			if (CfgNames == null || CfgNames.Length <= 1)
			{
				return null;
			}

			FConfigurationTree.FComponentTreeNode CombinedTree = new FConfigurationTree.FComponentTreeNode();
			CombinedTree.ComponentName = "CombinedTree";

			// Ensure recursion will not stop on the root node (this may happen if it is not explicitly marked as visible)
			CombinedTree.bVisibilitySame = true;
			CombinedTree.CommonConfig.bVisible = true;

			ConfigurationManager ConfigManager = InDoc.SwDoc.ConfigurationManager;
			IConfiguration OriginalConfiguration = ConfigManager.ActiveConfiguration;

			List<string> ExportedConfigurationNames = new List<string>();

			foreach (string CfgName in CfgNames)
			{
				IConfiguration swConfiguration = InDoc.SwDoc.GetConfigurationByName(CfgName) as IConfiguration;

				InDoc.SwDoc.ShowConfiguration2(CfgName);

				string ConfigName = CfgName;

				int DisplayStateCount = swConfiguration.GetDisplayStatesCount();
				string[] DisplayStates = null;

				if (ConfigManager.LinkDisplayStatesToConfigurations && DisplayStateCount > 1)
				{
					DisplayStates = swConfiguration.GetDisplayStates();
					ConfigName = $"{CfgName}_{DisplayStates[0]}";
				}

				FConfigurationTree.FComponentTreeNode ConfigNode = new FConfigurationTree.FComponentTreeNode();
				ConfigNode.Children = new List<FConfigurationTree.FComponentTreeNode>();
				ConfigNode.ComponentName = ConfigName;

				// Build the tree and get default materials (which aren't affected by any configuration)
				// Use GetRootComponent3() with Resolve = true to ensure suppressed components will be loaded
				CollectComponentsRecursive(InDoc, swConfiguration.GetRootComponent3(true), ConfigNode);

				ExportedConfigurationNames.Add(ConfigName);

				Dictionary<string, FObjectMaterials> MaterialsMap = GetComponentMaterials(InDoc, DisplayStates != null ? DisplayStates[0] : null, swConfiguration);
				SetComponentTreeMaterials(ConfigNode, MaterialsMap, null, false);

				if (DisplayStates != null)
				{
					// Export display states here: they are linked to configurations.
					// Skip first as it was already exported above.
					for (int Index = 1; Index < DisplayStateCount; ++Index)
					{
						string DisplayState = DisplayStates[Index];
						MaterialsMap = GetComponentMaterials(InDoc, DisplayState, swConfiguration);
						string DisplayStateTreeName = $"{CfgName}_DisplayState_{FDatasmithExporter.SanitizeName(DisplayState)}";
						SetComponentTreeMaterials(ConfigNode, MaterialsMap, DisplayStateTreeName, false);
					}
				}

				// Combine separate scene trees into the single one with configuration-specific data
				FConfigurationTree.Merge(CombinedTree, ConfigNode, CfgName);
			}

			if (OriginalConfiguration != null)
			{
				InDoc.SwDoc.ShowConfiguration2(OriginalConfiguration.Name);
			}

			List<string> DisplayStateConfigurations = new List<string>();

			if (!ConfigManager.LinkDisplayStatesToConfigurations)
			{
				// Export display states as separate configurations
				string[] DisplayStates = OriginalConfiguration.GetDisplayStates();

				foreach (string DisplayState in DisplayStates)
				{
					string DisplayStateConfigName = $"DisplayState_{FDatasmithExporter.SanitizeName(DisplayState)}";
					DisplayStateConfigurations.Add(DisplayStateConfigName);
					Dictionary<string, FObjectMaterials> MaterialsMap = GetComponentMaterials(InDoc, DisplayState, OriginalConfiguration);

					SetComponentTreeMaterials(CombinedTree, MaterialsMap, DisplayStateConfigName, true);
				}
			}

			// Remove configuration data when it's the same
			FConfigurationTree.Compress(CombinedTree);

			List<FConfigurationData> FlatConfigurationData = new List<FConfigurationData>();

			// Add roots for configurations
			foreach (string CfgName in ExportedConfigurationNames)
			{
				FConfigurationData CfgData = new FConfigurationData();
				CfgData.Name = CfgName;
				CfgData.bIsDisplayStateConfiguration = false;
				FConfigurationTree.FillConfigurationData(CombinedTree, CfgName, CfgData, false);

				if (!CfgData.IsEmpty())
				{
					FlatConfigurationData.Add(CfgData);
				}
			}
			foreach (string DisplayStateConfigName in DisplayStateConfigurations)
			{
				FConfigurationData CfgData = new FConfigurationData();
				CfgData.Name = DisplayStateConfigName;
				CfgData.bIsDisplayStateConfiguration = true;
				FConfigurationTree.FillConfigurationData(CombinedTree, DisplayStateConfigName, CfgData, true);

				if (!CfgData.IsEmpty())
				{
					FlatConfigurationData.Add(CfgData);
				}
			}

			return FlatConfigurationData;
		}

		private static Dictionary<string, FObjectMaterials> GetComponentMaterials(FDocument InDoc, string InDisplayState, IConfiguration InConfiguration)
		{
			Dictionary<string, FObjectMaterials> MaterialsMap = new Dictionary<string, FObjectMaterials>();

			swDisplayStateOpts_e Option = InDisplayState != null ? swDisplayStateOpts_e.swSpecifyDisplayState : swDisplayStateOpts_e.swThisDisplayState;
			string[] DisplayStates = InDisplayState != null ? new string[] { InDisplayState } : null;

			if (InDoc is FAssemblyDocument)
			{
				FAssemblyDocument AsmDoc = InDoc as FAssemblyDocument;

				HashSet<string> InComponentsSet = new HashSet<string>();

				foreach (string CompName in AsmDoc.SyncState.ExportedComponentsMap.Keys)
				{
					InComponentsSet.Add(CompName);
				}

				ConcurrentDictionary<string, FObjectMaterials> ComponentMaterials =
					FObjectMaterials.LoadAssemblyMaterials(AsmDoc, InComponentsSet, Option, DisplayStates);

				if (ComponentMaterials != null)
				{
					foreach (var KVP in ComponentMaterials)
					{
						MaterialsMap.Add(KVP.Key, KVP.Value);
					}
				}
			}
			else
			{
				FPartDocument PartDocument = InDoc as FPartDocument;
				FObjectMaterials PartMaterials = FObjectMaterials.LoadPartMaterials(InDoc, PartDocument.SwPartDoc, Option, DisplayStates);
				Component2 Comp = InConfiguration.GetRootComponent3(true);
				MaterialsMap.Add(Comp?.Name2 ?? "", PartMaterials);
			}

			return MaterialsMap;
		}

		private static void SetComponentTreeMaterials(FConfigurationTree.FComponentTreeNode InComponentTree, Dictionary<string, FObjectMaterials> InComponentMaterialsMap, string InConfigurationName, bool bIsDisplayState)
		{
			FConfigurationTree.FComponentConfig TargetConfig = null;

			if (InConfigurationName != null)
			{
				TargetConfig = InComponentTree.GetConfiguration(InConfigurationName, bIsDisplayState);
				if (TargetConfig == null)
				{
					TargetConfig = InComponentTree.AddConfiguration(InConfigurationName, bIsDisplayState);
				}
			}
			else
			{
				TargetConfig = InComponentTree.CommonConfig;
			}

			if (InComponentMaterialsMap.ContainsKey(InComponentTree.ComponentName))
			{
				FObjectMaterials Materials = InComponentMaterialsMap[InComponentTree.ComponentName];
				TargetConfig.Materials = Materials;
			}

			if (InComponentTree.Children != null)
			{
				foreach (FConfigurationTree.FComponentTreeNode Child in InComponentTree.Children)
				{
					SetComponentTreeMaterials(Child, InComponentMaterialsMap, InConfigurationName, bIsDisplayState);
				}
			}
		}

		private static void CollectComponentsRecursive(FDocument InDoc, Component2 InComponent, FConfigurationTree.FComponentTreeNode InParentNode)
		{
			FConfigurationTree.FComponentTreeNode NewNode = new FConfigurationTree.FComponentTreeNode();
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

			// Process children components
			var Children = (Object[])InComponent.GetChildren();
			if (Children != null && Children.Length > 0)
			{
				NewNode.Children = new List<FConfigurationTree.FComponentTreeNode>();
				foreach (object ObjChild in Children)
				{
					Component2 Child = (Component2)ObjChild;
					CollectComponentsRecursive(InDoc, Child, NewNode);
				}

				NewNode.Children.Sort(delegate (FConfigurationTree.FComponentTreeNode InA, FConfigurationTree.FComponentTreeNode InB)
				{
					return InA.ComponentID - InB.ComponentID;
				});
			}
		}
	}
}