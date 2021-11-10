// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using EpicGames.Core;
using OpenTracing;
using OpenTracing.Util;
using Microsoft.Extensions.Logging;
using System.Reflection;
using System.ComponentModel;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Reference to an output tag from a particular node
	/// </summary>
	public class BgNodeOutput
	{
		/// <summary>
		/// The node which produces the given output
		/// </summary>
		public BgNode ProducingNode { get; }

		/// <summary>
		/// Name of the tag
		/// </summary>
		public string TagName { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InProducingNode">Node which produces the given output</param>
		/// <param name="InTagName">Name of the tag</param>
		public BgNodeOutput(BgNode InProducingNode, string InTagName)
		{
			ProducingNode = InProducingNode;
			TagName = InTagName;
		}

		/// <summary>
		/// Returns a string representation of this output for debugging purposes
		/// </summary>
		/// <returns>The name of this output</returns>
		public override string ToString()
		{
			return String.Format("{0}: {1}", ProducingNode.Name, TagName);
		}
	}

	/// <summary>
	/// Defines a node, a container for tasks and the smallest unit of execution that can be run as part of a build graph.
	/// </summary>
	public class BgNode
	{
		/// <summary>
		/// The node's name
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Array of inputs which this node requires to run
		/// </summary>
		public BgNodeOutput[] Inputs { get; }

		/// <summary>
		/// Array of outputs produced by this node
		/// </summary>
		public BgNodeOutput[] Outputs { get; }

		/// <summary>
		/// Nodes which this node has input dependencies on
		/// </summary>
		public BgNode[] InputDependencies { get; set; }

		/// <summary>
		/// Nodes which this node needs to run after
		/// </summary>
		public BgNode[] OrderDependencies { get; set; }

		/// <summary>
		/// Tokens which must be acquired for this node to run
		/// </summary>
		public FileReference[] RequiredTokens { get; }

		/// <summary>
		/// List of tasks to execute
		/// </summary>
		public List<BgTask> Tasks { get; } = new List<BgTask>();

		/// <summary>
		/// List of email addresses to notify if this node fails.
		/// </summary>
		public HashSet<string> NotifyUsers { get; set; } = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// If set, anyone that has submitted to one of the given paths will be notified on failure of this node
		/// </summary>
		public HashSet<string> NotifySubmitters { get; set; } = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Whether to start this node as soon as its dependencies are satisfied, rather than waiting for all of its agent's dependencies to be met.
		/// </summary>
		public bool bRunEarly { get; set; } = false;

		/// <summary>
		/// Whether to ignore warnings produced by this node
		/// </summary>
		public bool bNotifyOnWarnings { get; set; } = true;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">The name of this node</param>
		/// <param name="InInputs">Inputs that this node depends on</param>
		/// <param name="InOutputNames">Names of the outputs that this node produces</param>
		/// <param name="InInputDependencies">Nodes which this node is dependent on for its inputs</param>
		/// <param name="InOrderDependencies">Nodes which this node needs to run after. Should include all input dependencies.</param>
		/// <param name="InRequiredTokens">Optional tokens which must be required for this node to run</param>
		public BgNode(string InName, BgNodeOutput[] InInputs, string[] InOutputNames, BgNode[] InInputDependencies, BgNode[] InOrderDependencies, FileReference[] InRequiredTokens)
		{
			Name = InName;
			Inputs = InInputs;

			List<BgNodeOutput> AllOutputs = new List<BgNodeOutput>();
			AllOutputs.Add(new BgNodeOutput(this, "#" + Name));
			AllOutputs.AddRange(InOutputNames.Where(x => String.Compare(x, Name, StringComparison.InvariantCultureIgnoreCase) != 0).Select(x => new BgNodeOutput(this, x)));
			Outputs = AllOutputs.ToArray();

			InputDependencies = InInputDependencies;
			OrderDependencies = InOrderDependencies;
			RequiredTokens = InRequiredTokens;
		}

		/// <summary>
		/// Returns the default output for this node, which includes all build products
		/// </summary>
		public BgNodeOutput DefaultOutput
		{
			get { return Outputs[0]; }
		}

		/// <summary>
		/// Determines the minimal set of direct input dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct inputs to this node</returns>
		public IEnumerable<BgNode> GetDirectInputDependencies()
		{
			HashSet<BgNode> DirectDependencies = new HashSet<BgNode>(InputDependencies);
			foreach(BgNode InputDependency in InputDependencies)
			{
				DirectDependencies.ExceptWith(InputDependency.InputDependencies);
			}
			return DirectDependencies;
		}

		/// <summary>
		/// Determines the minimal set of direct order dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct order dependencies of this node</returns>
		public IEnumerable<BgNode> GetDirectOrderDependencies()
		{
			HashSet<BgNode> DirectDependencies = new HashSet<BgNode>(OrderDependencies);
			foreach(BgNode OrderDependency in OrderDependencies)
			{
				DirectDependencies.ExceptWith(OrderDependency.OrderDependencies);
			}
			return DirectDependencies;
		}

		/// <summary>
		/// Write this node to an XML writer
		/// </summary>
		/// <param name="Writer">The writer to output the node to</param>
		public void Write(XmlWriter Writer)
		{
			Writer.WriteStartElement("Node");
			Writer.WriteAttributeString("Name", Name);

			string[] RequireNames = Inputs.Select(x => x.TagName).ToArray();
			if (RequireNames.Length > 0)
			{
				Writer.WriteAttributeString("Requires", String.Join(";", RequireNames));
			}

			string[] ProducesNames = Outputs.Where(x => x != DefaultOutput).Select(x => x.TagName).ToArray();
			if (ProducesNames.Length > 0)
			{
				Writer.WriteAttributeString("Produces", String.Join(";", ProducesNames));
			}

			string[] AfterNames = GetDirectOrderDependencies().Except(InputDependencies).Select(x => x.Name).ToArray();
			if (AfterNames.Length > 0)
			{
				Writer.WriteAttributeString("After", String.Join(";", AfterNames));
			}

			if (!bNotifyOnWarnings)
			{
				Writer.WriteAttributeString("NotifyOnWarnings", bNotifyOnWarnings.ToString());
			}

			if(bRunEarly)
			{
				Writer.WriteAttributeString("RunEarly", bRunEarly.ToString());
			}

			foreach (BgTask Task in Tasks)
			{
				Task.Write(Writer);
			}
			Writer.WriteEndElement();
		}

		/// <summary>
		/// Returns the name of this node
		/// </summary>
		/// <returns>The name of this node</returns>
		public override string ToString()
		{
			return Name;
		}
	}
}
