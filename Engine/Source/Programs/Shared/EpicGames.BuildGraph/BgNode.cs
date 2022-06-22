// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Threading.Tasks;
using System.Xml;
using EpicGames.Core;

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
		/// <param name="inProducingNode">Node which produces the given output</param>
		/// <param name="inTagName">Name of the tag</param>
		public BgNodeOutput(BgNode inProducingNode, string inTagName)
		{
			ProducingNode = inProducingNode;
			TagName = inTagName;
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
	/// Method to be executed for a node
	/// </summary>
	public class BgMethod
	{
		/// <summary>
		/// Full name of the class containing the method to execute
		/// </summary>
		public string ClassName { get; }

		/// <summary>
		/// Name of the method to execute
		/// </summary>
		public string MethodName { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgMethod(string className, string methodName)
		{
			ClassName = className;
			MethodName = methodName;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BgMethod(MethodInfo method)
			: this(method.DeclaringType!.AssemblyQualifiedName!, method.Name)
		{
		}

		/// <summary>
		/// Bind this method name to a method instance
		/// </summary>
		/// <returns>The resolved method instance</returns>
		public MethodInfo Bind()
		{
			Type? Type = Type.GetType(ClassName);
			if (Type == null)
			{
				throw new BgNodeException($"Unable to find class '{ClassName}'");
			}

			MethodInfo? Method = Type.GetMethod(MethodName, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
			if (Method == null)
			{
				throw new BgNodeException($"Unable to find method '{Type.FullName}.{MethodName}'");
			}

			return Method;
		}

		/// <inheritdoc/>
		public override string ToString() => $"{MethodName}";
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
		public IReadOnlyList<BgNodeOutput> Inputs { get; }

		/// <summary>
		/// Array of outputs produced by this node
		/// </summary>
		public IReadOnlyList<BgNodeOutput> Outputs { get; }

		/// <summary>
		/// Nodes which this node has input dependencies on
		/// </summary>
		public IReadOnlyList<BgNode> InputDependencies { get; set; }

		/// <summary>
		/// Nodes which this node needs to run after
		/// </summary>
		public IReadOnlyList<BgNode> OrderDependencies { get; set; }

		/// <summary>
		/// Tokens which must be acquired for this node to run
		/// </summary>
		public IReadOnlyList<FileReference> RequiredTokens { get; }

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
		public bool RunEarly { get; set; } = false;

		/// <summary>
		/// Whether to ignore warnings produced by this node
		/// </summary>
		public bool NotifyOnWarnings { get; set; } = true;

		/// <summary>
		/// Custom annotations for this node
		/// </summary>
		public Dictionary<string, string> Annotations { get; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Diagnostics for this node
		/// </summary>
		public List<BgDiagnostic> Diagnostics { get; } = new List<BgDiagnostic>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="name">The name of this node</param>
		/// <param name="inputs">Inputs that this node depends on</param>
		/// <param name="outputNames">Names of the outputs that this node produces</param>
		/// <param name="inputDependencies">Nodes which this node is dependent on for its inputs</param>
		/// <param name="orderDependencies">Nodes which this node needs to run after. Should include all input dependencies.</param>
		/// <param name="requiredTokens">Optional tokens which must be required for this node to run</param>
		public BgNode(string name, IReadOnlyList<BgNodeOutput> inputs, IReadOnlyList<string> outputNames, IReadOnlyList<BgNode> inputDependencies, IReadOnlyList<BgNode> orderDependencies, IReadOnlyList<FileReference> requiredTokens)
		{
			Name = name;
			Inputs = inputs;

			List<BgNodeOutput> allOutputs = new List<BgNodeOutput>();
			allOutputs.Add(new BgNodeOutput(this, "#" + Name));
			allOutputs.AddRange(outputNames.Where(x => String.Compare(x, Name, StringComparison.InvariantCultureIgnoreCase) != 0).Select(x => new BgNodeOutput(this, x)));
			Outputs = allOutputs.ToArray();

			InputDependencies = inputDependencies;
			OrderDependencies = orderDependencies;
			RequiredTokens = requiredTokens;
		}

		/// <summary>
		/// Returns the default output for this node, which includes all build products
		/// </summary>
		public BgNodeOutput DefaultOutput => Outputs[0];

		/// <summary>
		/// Determines the minimal set of direct input dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct inputs to this node</returns>
		public IEnumerable<BgNode> GetDirectInputDependencies()
		{
			HashSet<BgNode> directDependencies = new HashSet<BgNode>(InputDependencies);
			foreach (BgNode inputDependency in InputDependencies)
			{
				directDependencies.ExceptWith(inputDependency.InputDependencies);
			}
			return directDependencies;
		}

		/// <summary>
		/// Determines the minimal set of direct order dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct order dependencies of this node</returns>
		public IEnumerable<BgNode> GetDirectOrderDependencies()
		{
			HashSet<BgNode> directDependencies = new HashSet<BgNode>(OrderDependencies);
			foreach (BgNode orderDependency in OrderDependencies)
			{
				directDependencies.ExceptWith(orderDependency.OrderDependencies);
			}
			return directDependencies;
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

	/// <summary>
	/// Node constructed from a bytecode expression
	/// </summary>
	public class BgExpressionNode : BgNode
	{
		/// <summary>
		/// The method to execute for this node
		/// </summary>
		public BgMethod Method { get; }

		/// <summary>
		/// Arguments for invoking the method
		/// </summary>
		public IReadOnlyList<object?> Arguments { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgExpressionNode(string name, BgMethod method, IReadOnlyList<object?> arguments, IReadOnlyList<BgNodeOutput> inputs, IReadOnlyList<string> outputNames, IReadOnlyList<BgNode> inputDependencies, IReadOnlyList<BgNode> orderDependencies, IReadOnlyList<FileReference> requiredTokens)
			: base(name, inputs, outputNames, inputDependencies, orderDependencies, requiredTokens)
		{
			Method = method;
			Arguments = arguments;
		}
	}
}
