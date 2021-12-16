// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Exception for constructing nodes
	/// </summary>
	public sealed class BgNodeException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message"></param>
		public BgNodeException(string Message) : base(Message)
		{
		}
	}

	/// <summary>
	/// Speecifies the node name for a method. Parameters from the method may be embedded in the name using the {ParamName} syntax.
	/// </summary>
	[AttributeUsage(AttributeTargets.Method)]
	public class BgNodeNameAttribute : Attribute
	{
		/// <summary>
		/// The format string
		/// </summary>
		public string Template { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Template">Format string for the name</param>
		public BgNodeNameAttribute(string Template)
		{
			this.Template = Template;
		}
	}

	/// <summary>
	/// Specification for a node to execute
	/// </summary>
	public class BgNodeSpec
	{
		internal string ClassName;
		internal string MethodName;
		internal IBgExpr?[] ArgumentExprs;
		internal BgFileSet[] ResultExprs { get; }

		/// <summary>
		/// Name of the node
		/// </summary>
		public BgString Name { get; set; }

		/// <summary>
		/// Tokens for inputs of this node
		/// </summary>
		public BgList<BgFileSet> InputDependencies { get; set; } = BgList<BgFileSet>.Empty;

		/// <summary>
		/// Weak dependency on outputs that must be generated for the node to run, without making those dependencies inputs.
		/// </summary>
		public BgList<BgFileSet> AfterDependencies { get; set; } = BgList<BgFileSet>.Empty;

		/// <summary>
		/// Token for the output of this node
		/// </summary>
		BgFileSet Output { get; }

		/// <summary>
		/// All inputs and outputs of this node
		/// </summary>
		BgList<BgFileSet> InputsAndOutputs => InputDependencies.Add(Output);

		/// <summary>
		/// Whether this node should start running as soon as its dependencies are ready, even if other nodes in the same agent are not.
		/// </summary>
		public BgBool CanRunEarly { get; set; } = BgBool.False;

		/// <summary>
		/// Diagnostics for running this node
		/// </summary>
		public BgList<BgDiagnosticSpec> Diagnostics { get; set; } = BgList<BgDiagnosticSpec>.Empty;

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgNodeSpec(MethodCallExpression Call)
		{
			string MethodSignature = $"{Call.Method.DeclaringType?.Name}.{Call.Method.Name}";
			if (!Call.Method.IsStatic)
			{
				throw new BgNodeException($"Node {Call.Method.DeclaringType?.Name}.{Call.Method.Name} must be static");
			}

			this.ClassName = Call.Method.DeclaringType!.AssemblyQualifiedName!;
			this.MethodName = Call.Method.Name;

			try
			{
				ArgumentExprs = CreateArgumentExprs(Call);
				Name = GetDefaultNodeName(Call.Method, ArgumentExprs);
				ResultExprs = CreateReturnExprs(Call.Method, Name, Call.Method.ReturnType);
			}
			catch (Exception Ex)
			{
				ExceptionUtils.AddContext(Ex, $"while calling method {Call.Method.DeclaringType?.Name}.{Call.Method.Name}");
				throw;
			}

			Output = new BgFileSetTagFromStringExpr(BgString.Format("#{0}", Name));
		}

		/// <summary>
		/// Creates a node specification
		/// </summary>
		internal static BgNodeSpec Create(Expression<Func<BgContext, Task>> Action)
		{
			MethodCallExpression Call = (MethodCallExpression)Action.Body;
			return new BgNodeSpec(Call);
		}

		/// <summary>
		/// Creates a node specification
		/// </summary>
		internal static BgNodeSpec<T> Create<T>(Expression<Func<BgContext, Task<T>>> Function)
		{
			MethodCallExpression Call = (MethodCallExpression)Function.Body;
			return new BgNodeSpec<T>(Call);
		}

		static IBgExpr?[] CreateArgumentExprs(MethodCallExpression Call)
		{
			IBgExpr?[] Args = new IBgExpr?[Call.Arguments.Count];
			for (int Idx = 0; Idx < Call.Arguments.Count; Idx++)
			{
				Expression Expr = Call.Arguments[Idx];
				if (Expr is ParameterExpression ParameterExpr)
				{
					if (ParameterExpr.Type != typeof(BgContext))
					{
						throw new BgNodeException($"Unable to determine type of parameter '{ParameterExpr.Name}'");
					}
				}
				else
				{
					Delegate Compiled = Expression.Lambda(Expr).Compile();

					object? Result = Compiled.DynamicInvoke();
					if (Result is IBgExpr Computable)
					{
						Args[Idx] = Computable;
					}
					else
					{
						Args[Idx] = (BgString)(Result?.ToString() ?? String.Empty);
					}
				}
			}
			return Args;
		}

		static BgFileSet[] CreateReturnExprs(MethodInfo MethodInfo, BgString Name, Type Type)
		{
			if (Type == typeof(Task))
			{
				return Array.Empty<BgFileSet>();
			}
			else if (Type.IsGenericType && Type.GetGenericTypeDefinition() == typeof(Task<>))
			{
				return CreateReturnExprsInner(MethodInfo, Name, Type.GetGenericArguments()[0]);
			}
			else
			{
				throw new NotImplementedException();
			}
		}

		static BgFileSet[] CreateReturnExprsInner(MethodInfo MethodInfo, BgString Name, Type Type)
		{
			Type[] OutputTypes;
			if (IsValueTuple(Type))
			{
				OutputTypes = Type.GetGenericArguments();
			}
			else
			{
				OutputTypes = new[] { Type };
			}

			BgFileSet[] OutputExprs = new BgFileSet[OutputTypes.Length];
			for (int Idx = 0; Idx < OutputTypes.Length; Idx++)
			{
				OutputExprs[Idx] = CreateOutputExpr(MethodInfo, Name, Idx, OutputTypes[Idx]);
			}
			return OutputExprs;
		}

		static BgFileSet CreateOutputExpr(MethodInfo MethodInfo, BgString Name, int Index, Type Type)
		{
			if (Type == typeof(BgFileSet))
			{
				return new BgFileSetTagFromStringExpr(BgString.Format("#{0}${1}", Name, Index));
			}
			else
			{
				throw new BgNodeException($"Unsupported return type for {MethodInfo.Name}: {Type.Name}");
			}
		}

		internal static bool IsValueTuple(Type ReturnType)
		{
			if (ReturnType.IsGenericType)
			{
				Type GenericType = ReturnType.GetGenericTypeDefinition();
				if(GenericType.FullName != null && GenericType.FullName.StartsWith("System.ValueTuple`", StringComparison.Ordinal))
				{
					return true;
				}
			}
			return false;
		}

		static BgString GetDefaultNodeName(MethodInfo MethodInfo, IBgExpr?[] Args)
		{
			// Check if it's got an attribute override for the node name
			BgNodeNameAttribute? NameAttr = MethodInfo.GetCustomAttribute<BgNodeNameAttribute>();
			if (NameAttr != null)
			{
				return GetNodeNameFromTemplate(NameAttr.Template, MethodInfo.GetParameters(), Args);
			}
			else
			{
				return GetNodeNameFromMethodName(MethodInfo.Name);
			}
		}

		static BgString GetNodeNameFromTemplate(string Template, ParameterInfo[] Parameters, IBgExpr?[] Args)
		{
			// Create a list of lazily computed string fragments which comprise the evaluated name
			List<BgString> Fragments = new List<BgString>();

			int LastIdx = 0;
			for (int NextIdx = 0; NextIdx < Template.Length; NextIdx++)
			{
				if (Template[NextIdx] == '{')
				{
					if (NextIdx + 1 < Template.Length && Template[NextIdx + 1] == '{')
					{
						Fragments.Add(Template.Substring(LastIdx, NextIdx - LastIdx));
						LastIdx = ++NextIdx;
					}
					else
					{
						Fragments.Add(Template.Substring(LastIdx, NextIdx - LastIdx));
						NextIdx++;

						int EndIdx = Template.IndexOf('}', NextIdx);
						if (EndIdx == -1)
						{
							throw new BgNodeException($"Unterminated parameter expression for {nameof(BgNodeNameAttribute)} in {Template}");
						}

						StringView ParamName = new StringView(Template, NextIdx, EndIdx - NextIdx);

						int ParamIdx = Array.FindIndex(Parameters, x => x.Name != null && ParamName.Equals(x.Name, StringComparison.Ordinal));
						if (ParamIdx == -1)
						{
							throw new BgNodeException($"Unable to find parameter named {ParamName} in {Template}");
						}

						IBgExpr? Arg = Args[ParamIdx];
						if(Arg != null)
						{
							Fragments.Add(Arg.ToBgString());
						}

						LastIdx = NextIdx = EndIdx + 1;
					}
				}
				else if (Template[NextIdx] == '}')
				{
					if (NextIdx + 1 < Template.Length && Template[NextIdx + 1] == '{')
					{
						Fragments.Add(Template.Substring(LastIdx, NextIdx - LastIdx));
						LastIdx = ++NextIdx;
					}
				}
			}
			Fragments.Add(Template.Substring(LastIdx, Template.Length - LastIdx));

			return BgString.Join(BgString.Empty, Fragments);
		}

		/// <summary>
		/// Inserts spaces into a PascalCase method name to create a node name
		/// </summary>
		public static string GetNodeNameFromMethodName(string MethodName)
		{
			StringBuilder Name = new StringBuilder();
			Name.Append(MethodName[0]);

			int Length = MethodName.Length;
			if (Length > 5 && MethodName.EndsWith("Async", StringComparison.Ordinal))
			{
				Length -= 5;
			}

			bool bIsAcronym = false;
			for (int Idx = 1; Idx < Length; Idx++)
			{
				bool bLastIsUpper = Char.IsUpper(MethodName[Idx - 1]);
				bool bNextIsUpper = Char.IsUpper(MethodName[Idx]);
				if (bLastIsUpper && bNextIsUpper)
				{
					bIsAcronym = true;
				}
				else if (bIsAcronym)
				{
					Name.Insert(Name.Length - 2, ' ');
					bIsAcronym = false;
				}
				else if (!bLastIsUpper && bNextIsUpper)
				{
					Name.Append(' ');
				}
				Name.Append(MethodName[Idx]);
			}

			return Name.ToString();
		}

		/// <summary>
		/// Gets the signature for a method
		/// </summary>
		public static string GetSignature(MethodInfo MethodInfo)
		{
			StringBuilder Arguments = new StringBuilder();
			foreach (ParameterInfo ParameterInfo in MethodInfo.GetParameters())
			{
				Arguments.AppendLine(ParameterInfo.ParameterType.FullName);
			}
			return $"{MethodInfo.Name}:{Digest.Compute<Sha1>(Arguments.ToString())}";
		}

		/// <summary>
		/// Creates a concrete node
		/// </summary>
		/// <param name="Context"></param>
		/// <param name="Graph"></param>
		/// <param name="Agent"></param>
		/// <returns></returns>
		internal void AddToGraph(BgExprContext Context, BgGraph Graph, BgAgent Agent)
		{
			HashSet<string> InputTags = new HashSet<string>();
			InputTags.UnionWith(this.InputDependencies.ComputeTags(Context));
			InputTags.UnionWith(ArgumentExprs.OfType<BgFileSet>().Select(x => x.ComputeTag(Context)));
			InputTags.UnionWith(ArgumentExprs.OfType<BgList<BgFileSet>>().SelectMany(x => x.GetEnumerable(Context)).Select(x => x.ComputeTag(Context)));

			HashSet<string> AfterTags = new HashSet<string>(InputTags);
			AfterTags.UnionWith(this.AfterDependencies.ComputeTags(Context));

			string Name = this.Name.Compute(Context);
			BgNodeOutput[] InputDependencies = InputTags.Select(x => Graph.TagNameToNodeOutput[x]).Distinct().ToArray();
			BgNodeOutput[] AfterDependencies = AfterTags.Select(x => Graph.TagNameToNodeOutput[x]).Distinct().ToArray();
			string[] OutputNames = Array.ConvertAll(ResultExprs, x => x.ComputeTag(Context));
			BgNode[] InputNodes = InputDependencies.Select(x => x.ProducingNode).Distinct().ToArray();
			BgNode[] AfterNodes = AfterDependencies.Select(x => x.ProducingNode).Distinct().ToArray();
			bool RunEarly = CanRunEarly.Compute(Context);

			BgNode Node = new BgNode(Name, InputDependencies, OutputNames, InputNodes, AfterNodes, Array.Empty<FileReference>());
			Node.bRunEarly = RunEarly;

			BgScriptLocation Location = new BgScriptLocation("(unknown)", "(unknown)", 1);

			BgTask Task = new BgTask(Location, "CallMethod");
			Task.Arguments["Class"] = ClassName;
			Task.Arguments["Method"] = MethodName;
			for (int Idx = 0; Idx < ArgumentExprs.Length; Idx++)
			{
				IBgExpr? ArgumentExpr = ArgumentExprs[Idx];
				if (ArgumentExpr != null)
				{
					Task.Arguments[$"Arg{Idx + 1}"] = BgType.Get(ArgumentExpr.GetType()).SerializeArgument(ArgumentExpr, Context);
				}
			}
			if (OutputNames != null)
			{
				Task.Arguments["Tags"] = String.Join(";", OutputNames);
			}
			Node.Tasks.Add(Task);

			Agent.Nodes.Add(Node);
			Graph.NameToNode.Add(Name, Node);

			foreach (BgDiagnosticSpec Precondition in Diagnostics.GetEnumerable(Context))
			{
				Precondition.AddToGraph(Context, Graph, Agent, Node);
			}

			foreach(BgNodeOutput Output in Node.Outputs)
			{
				Graph.TagNameToNodeOutput.Add(Output.TagName, Output);
			}
		}

		/// <summary>
		/// Allows using the 
		/// </summary>
		/// <param name="NodeSpec"></param>
		public static implicit operator BgList<BgFileSet>(BgNodeSpec NodeSpec)
		{
			return NodeSpec.InputsAndOutputs.Add(NodeSpec.Output);
		}
	}

	/// <summary>
	/// Specification for a node to execute that returns one or more tagged outputs
	/// </summary>
	/// <typeparam name="T">Return type from the node. Must consist of latent Bg* types (eg. BgToken)</typeparam>
	public class BgNodeSpec<T> : BgNodeSpec
	{
		/// <summary>
		/// Output value from this node
		/// </summary>
		public T Output { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		internal BgNodeSpec(MethodCallExpression Call)
			: base(Call)
		{
			Type ReturnType = Call.Method.ReturnType;
			if (IsValueTuple(ReturnType))
			{
				Output = (T)Activator.CreateInstance(ReturnType, ResultExprs)!;
			}
			else
			{
				Output = (T)(object)ResultExprs[0];
			}
		}
	}

	/// <summary>
	/// Extension methods for BgNode types
	/// </summary>
	public static class BgNodeExtensions
	{
		/// <summary>
		/// Add dependencies onto other nodes or outputs. Outputs from the given tokens will be copied to the current machine before execution of the node.
		/// </summary>
		/// <param name="NodeSpec">The node specification</param>
		/// <param name="Tokens">Tokens to add dependencies on</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T Requires<T>(this T NodeSpec, params BgList<BgFileSet>[] Tokens) where T : BgNodeSpec
		{
			foreach (BgList<BgFileSet> TokenSet in Tokens)
			{
				NodeSpec.InputDependencies = NodeSpec.InputDependencies.Add(TokenSet);
			}
			return NodeSpec;
		}

		/// <summary>
		/// Add weak dependencies onto other nodes or outputs. The producing nodes must complete successfully if they are part of the graph, but outputs from them will not be 
		/// transferred to the machine running this node.
		/// </summary>
		/// <param name="NodeSpec">The node specification</param>
		/// <param name="Tokens">Tokens to add dependencies on</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T After<T>(this T NodeSpec, params BgList<BgFileSet>[] Tokens) where T : BgNodeSpec
		{
			foreach (BgList<BgFileSet> TokenSet in Tokens)
			{
				NodeSpec.AfterDependencies = NodeSpec.AfterDependencies.Add(TokenSet);
			}
			return NodeSpec;
		}

		/// <summary>
		/// Adds a warning when executing this node
		/// </summary>
		/// <param name="NodeSpec">The node specification</param>
		/// <param name="Condition">Condition for writing the message</param>
		/// <param name="Message">Message to output</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static BgNodeSpec WarnIf<T>(this T NodeSpec, BgBool Condition, BgString Message) where T : BgNodeSpec
		{
			NodeSpec.Diagnostics = NodeSpec.Diagnostics.AddIf(Condition, new BgDiagnosticSpec(LogLevel.Warning, Message));
			return NodeSpec;
		}

		/// <summary>
		/// Adds an error when executing this node
		/// </summary>
		/// <param name="NodeSpec">The node specification</param>
		/// <param name="Condition">Condition for writing the message</param>
		/// <param name="Message">Message to output</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static BgNodeSpec ErrorIf<T>(this T NodeSpec, BgBool Condition, BgString Message) where T : BgNodeSpec
		{
			NodeSpec.Diagnostics = NodeSpec.Diagnostics.AddIf(Condition, new BgDiagnosticSpec(LogLevel.Error, Message));
			return NodeSpec;
		}
	}
}
