// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.DocObjects;
using Rhino.Geometry;
using System;
using System.Collections;
using System.Collections.Generic;

namespace DatasmithRhino
{
	public class RhinoSceneHierarchyNodeInfo
	{
		public bool bHasRhinoObject { get { return RhinoModelComponent is RhinoObject; } }
		public ModelComponent RhinoModelComponent { get; private set; } = null;
		public string Name { get; private set; }
		public string Label { get; private set; }
		public Transform InstanceWorldTransform { get; private set; }
		public List<string> Tags { get; private set; }

		public RhinoSceneHierarchyNodeInfo(ModelComponent InModelComponent, string InName, string InLabel, Transform InTransform)
		{
			RhinoModelComponent = InModelComponent;
			Name = InName;
			Label = InLabel;

			//We can't know the exact position of the actor before it's associated mesh is generated.
			//However, we can get the transform offset when instancing blocks. 
			//This is okay because RhinoObjects having geometry don't have children.
			InstanceObject Instance = InModelComponent as InstanceObject;
			if (Instance != null)
			{
				InstanceWorldTransform = Transform.Multiply(InTransform, Instance.InstanceXform);
			}
			else
			{
				InstanceWorldTransform = InTransform;
			}
		}

		public RhinoSceneHierarchyNodeInfo(ModelComponent InModelComponent, string InName, string InLabel) 
			: this(InModelComponent, InName, InLabel, Transform.Identity)
		{
		}
	}

	public class RhinoSceneHierarchyNode : IEnumerable<RhinoSceneHierarchyNode>
	{
		public bool bIsRoot { get; private set; } = true;
		public bool bIsInstanceDefinition { get; private set; }
		public RhinoSceneHierarchyNode Parent { get; private set; }
		public RhinoSceneHierarchyNode LinkedNode { get; private set; }
		public FDatasmithFacadeActor DatasmithActor { get; private set; }
		private List<RhinoSceneHierarchyNode> Children;

		public RhinoSceneHierarchyNodeInfo Info { get; private set; }

		//No Parent, this is a root node.
		public RhinoSceneHierarchyNode(bool bInIsInstanceDefinition)
		{
			Children = new List<RhinoSceneHierarchyNode>();
			bIsInstanceDefinition = bInIsInstanceDefinition;
		}

		private RhinoSceneHierarchyNode(RhinoSceneHierarchyNode InParent, RhinoSceneHierarchyNodeInfo InNodeInfo) : this(InParent.bIsInstanceDefinition)
		{
			bIsRoot = false;
			Parent = InParent;
			Info = InNodeInfo;
		}

		public void SetDatasmithActor(FDatasmithFacadeActor InActor)
		{
			if(bIsRoot || DatasmithActor != null)
			{
				RhinoApp.WriteLine(string.Format("Error: Generating a datasmith actor for a Hierarchy node that doesn't need one!"));
				return;
			}

			DatasmithActor = InActor;
		}

		public void LinkToNode(RhinoSceneHierarchyNode InLinkedNode)
		{
			LinkedNode = InLinkedNode;
			InLinkedNode.bIsInstanceDefinition = true;
		}

		public RhinoSceneHierarchyNode AddChild(RhinoSceneHierarchyNodeInfo InNodeInfo)
		{
			RhinoSceneHierarchyNode ChildNode = new RhinoSceneHierarchyNode(this, InNodeInfo);
			Children.Add(ChildNode);

			return ChildNode;
		}

		public int GetChildrenCount(bool bImmediateChildrenOnly = true)
		{
			int DescendantCount = 0;

			if(!bImmediateChildrenOnly)
			{
				for(int ChildIndex = 0, ChildrenCount = Children.Count; ChildIndex < ChildrenCount; ++ChildIndex)
				{
					DescendantCount += Children[ChildIndex].GetChildrenCount(/*bImmediateChildrenOnly=*/false);
				}
			}

			return DescendantCount + Children.Count;
		}

		public RhinoSceneHierarchyNode GetChild(int ChildIndex)
		{
			return Children[ChildIndex];
		}

		//IEnumerable interface begin
		public IEnumerator<RhinoSceneHierarchyNode> GetEnumerator()
		{
			yield return this;

			foreach (var Child in Children)
			{
				var ChildEnumerator = Child.GetEnumerator();
				while(ChildEnumerator.MoveNext())
				{
					yield return ChildEnumerator.Current;
				}
			}
		}

		IEnumerator IEnumerable.GetEnumerator()
		{
			return GetEnumerator();
		}
		//IEnumerable interface end
	}

	public class DatasmithRhinoSceneParser
	{
		public RhinoDoc RhinoDocument { get; private set; }
		public RhinoSceneHierarchyNode SceneRoot = new RhinoSceneHierarchyNode(/*bInIsInstanceDefinition=*/false);
		public Dictionary<InstanceDefinition, RhinoSceneHierarchyNode> InstanceNodeMap = new Dictionary<InstanceDefinition, RhinoSceneHierarchyNode>();
		public Dictionary<Guid, RhinoSceneHierarchyNode> GuidToHierarchyNodeDictionary = new Dictionary<Guid, RhinoSceneHierarchyNode>();
		public FUniqueNameGenerator LabelGenerator = new FUniqueNameGenerator();

		public DatasmithRhinoSceneParser(RhinoDoc InDoc)
		{
			RhinoDocument = InDoc;
		}

		public void ParseDocument()
		{
			ParseRhinoHierarchy();
			//TODO ParseMaterials() && Instances, etc.;
		}

		private void ParseRhinoHierarchy()
		{
			foreach(var CurrentLayer in RhinoDocument.Layers)
			{
				//Only add Layers directly under root, the recursion will do the rest.
				if (CurrentLayer.ParentLayerId == Guid.Empty)
				{
					RecursivelyParseLayerHierarchy(CurrentLayer, SceneRoot);
				}
			}
		}

		private void RecursivelyParseLayerHierarchy(Layer CurrentLayer, RhinoSceneHierarchyNode ParentNode)
		{
			RhinoSceneHierarchyNodeInfo NodeInfo = GenerateNodeInfo(CurrentLayer, !ParentNode.bIsInstanceDefinition);
			RhinoSceneHierarchyNode CurrentNode = ParentNode.AddChild(NodeInfo);
			GuidToHierarchyNodeDictionary.Add(CurrentLayer.Id, CurrentNode);

			RhinoObject[] ObjectsInLayer = RhinoDocument.Objects.FindByLayer(CurrentLayer);
			RecursivelyParseObjectInstance(ObjectsInLayer, CurrentNode);

			Layer[] ChildrenLayer = CurrentLayer.GetChildren();
			if(ChildrenLayer != null)
			{
				foreach (var ChildLayer in ChildrenLayer)
				{
					RecursivelyParseLayerHierarchy(ChildLayer, ParentNode);
				}
			}
		}

		private void RecursivelyParseObjectInstance(RhinoObject[] InObjects, RhinoSceneHierarchyNode ParentNode)
		{
			foreach (RhinoObject CurrentObject in InObjects)
			{
				RhinoSceneHierarchyNodeInfo ObjectNodeInfo = GenerateNodeInfo(CurrentObject, !ParentNode.bIsInstanceDefinition);
				RhinoSceneHierarchyNode ObjectNode = ParentNode.AddChild(ObjectNodeInfo);
				GuidToHierarchyNodeDictionary.Add(CurrentObject.Id, ObjectNode);

				if(CurrentObject.ObjectType == ObjectType.InstanceReference)
				{
					InstanceObject CurrentInstance = CurrentObject as InstanceObject;
					RhinoSceneHierarchyNode InstanceRootNode = GetOrCreateInstanceRootNode(CurrentInstance.InstanceDefinition);

					InstanciateDefinition(ObjectNode, InstanceRootNode);
				}
			}
		}

		private void InstanciateDefinition(RhinoSceneHierarchyNode TargetNode, RhinoSceneHierarchyNode DefinitionNode)
		{
			TargetNode.LinkToNode(DefinitionNode);

			for(int ChildIndex = 0; ChildIndex < DefinitionNode.GetChildrenCount(); ++ChildIndex)
			{
				RhinoSceneHierarchyNode DefinitionChildNode = DefinitionNode.GetChild(ChildIndex);
				RhinoSceneHierarchyNodeInfo ChildNodeInfo = GenerateNodeInfo(TargetNode.Info, DefinitionChildNode.Info);
				RhinoSceneHierarchyNode InstanceChildNode = TargetNode.AddChild(ChildNodeInfo);

				InstanciateDefinition(InstanceChildNode, DefinitionChildNode);
			}
		}

		private RhinoSceneHierarchyNode GetOrCreateInstanceRootNode(InstanceDefinition InInstanceDefinition)
		{
			RhinoSceneHierarchyNode InstanceRootNode;

			//If a hierachy node does not exist for this instance definition, create one.
			if (!InstanceNodeMap.TryGetValue(InInstanceDefinition, out InstanceRootNode))
			{
				InstanceRootNode = new RhinoSceneHierarchyNode(/*bInIsInstanceDefinition=*/true);
				InstanceNodeMap.Add(InInstanceDefinition, InstanceRootNode);

				RhinoObject[] InstanceObjects = InInstanceDefinition.GetObjects();
				RecursivelyParseObjectInstance(InstanceObjects, InstanceRootNode);
			}

			return InstanceRootNode;
		}

		/// <summary>
		/// Creates a new hierarchy node info by using the DefinitionNodeInfo as a base, and using InstanceParentNodeInfo for creating a unique name. Used when instancing block definitions.
		/// </summary>
		/// <param name="InstanceParentNodeInfo"></param>
		/// <param name="DefinitionNodeInfo"></param>
		/// <returns></returns>
		private RhinoSceneHierarchyNodeInfo GenerateNodeInfo(RhinoSceneHierarchyNodeInfo InstanceParentNodeInfo, RhinoSceneHierarchyNodeInfo DefinitionNodeInfo)
		{
			string Name = string.Format("{0}_{1}", InstanceParentNodeInfo.Name, DefinitionNodeInfo.Name);
			string Label = LabelGenerator.GenerateUniqueNameFromBaseName(DefinitionNodeInfo.Label);

			return new RhinoSceneHierarchyNodeInfo(DefinitionNodeInfo.RhinoModelComponent, Name, Label, InstanceParentNodeInfo.InstanceWorldTransform);
		}

		/// <summary>
		/// Creates a new hierarchy node info for a given Rhino Model Component, used to determine names and labels as well as linking.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <param name="bRequireUniqueLabel">When set to true, the label will be generated using the UniqueNameGenerator. 
		///									Turning this to false is useful when creating a hierarchy graph for an instance definition.</param>
		/// <returns></returns>
		private RhinoSceneHierarchyNodeInfo GenerateNodeInfo(ModelComponent InModelComponent, bool bRequireUniqueLabel)
		{
			string Name = InModelComponent.Id.ToString();
			string Label = bRequireUniqueLabel
				? LabelGenerator.GenerateUniqueName(InModelComponent)
				: FUniqueNameGenerator.GetTargetName(InModelComponent);

			return new RhinoSceneHierarchyNodeInfo(InModelComponent, Name, Label);
		}
	}

	public class FUniqueNameGenerator
	{
		/// <summary>
		/// Gives a human readable name based on the type of the given model component. 
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <returns></returns>
		public static string GetDefaultName(ModelComponent InModelComponent)
		{
			switch(InModelComponent)
			{
				case RhinoObject InRhinoObject:
					return InRhinoObject.ObjectType.ToString();
				case Layer InLayer:
					return "Layer";
				default:
					return "Object";
			}
		}

		/// <summary>
		/// Gives the non-unique "base" name for the given model component.
		/// </summary>
		/// <param name="InModelComponent"></param>
		/// <returns></returns>
		public static string GetTargetName(ModelComponent InModelComponent)
		{
			string TargetName = (InModelComponent.Name != null && InModelComponent.Name != "")
				? InModelComponent.Name
				: GetDefaultName(InModelComponent);

			return TargetName;
		}

		public string GenerateUniqueName(ModelComponent InModelComponent)
		{
			string TargetName = GetTargetName(InModelComponent);
			return GenerateUniqueNameFromBaseName(TargetName);
		}

		public string GenerateUniqueNameFromBaseName(string BaseName)
		{
			string UniqueName = GetFirstUniqueNameFromBaseName(BaseName);
			UniqueNameSet.Add(UniqueName);

			return UniqueName;
		}

		private string GetFirstUniqueNameFromBaseName(string BaseName)
		{
			if (!UniqueNameSet.Contains(BaseName))
			{
				return BaseName;
			}

			string UniqueName;
			int NameIndex = 1;
			do
			{
				UniqueName = string.Format("{0}_{1}", BaseName, NameIndex++);
			} while (UniqueNameSet.Contains(UniqueName));

			return UniqueName;
		}

		private HashSet<string> UniqueNameSet = new HashSet<string>();
	}
}