// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

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
		/// <param name="message"></param>
		public BgNodeException(string message) : base(message)
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
		/// <param name="template">Format string for the name</param>
		public BgNodeNameAttribute(string template)
		{
			Template = template;
		}
	}

	/// <summary>
	/// Specification for a node to execute
	/// </summary>
	public class BgNodeSpec
	{
		internal BgMethod _method;
		internal object?[] _arguments;
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
		internal BgNodeSpec(MethodCallExpression call)
		{
			_method = new BgMethod(call.Method);

			try
			{
				_arguments = CreateArgumentExprs(call);
				Name = GetDefaultNodeName(call.Method, _arguments);
				ResultExprs = CreateReturnExprs(call.Method, Name, call.Method.ReturnType);
			}
			catch (Exception ex)
			{
				ExceptionUtils.AddContext(ex, $"while calling method {_method}");
				throw;
			}

			Output = new BgFileSetTagFromStringExpr(BgString.Format("#{0}", Name));
		}

		/// <summary>
		/// Creates a node specification
		/// </summary>
		internal static BgNodeSpec Create(Expression<Func<BgContext, Task>> action)
		{
			MethodCallExpression call = (MethodCallExpression)action.Body;
			return new BgNodeSpec(call);
		}

		/// <summary>
		/// Creates a node specification
		/// </summary>
		internal static BgNodeSpec<T> Create<T>(Expression<Func<BgContext, Task<T>>> function)
		{
			MethodCallExpression call = (MethodCallExpression)function.Body;
			return new BgNodeSpec<T>(call);
		}

		static object?[] CreateArgumentExprs(MethodCallExpression call)
		{
			object?[] args = new object?[call.Arguments.Count];
			for (int idx = 0; idx < call.Arguments.Count; idx++)
			{
				Expression expr = call.Arguments[idx];
				if (expr is ParameterExpression parameterExpr)
				{
					if (parameterExpr.Type != typeof(BgContext))
					{
						throw new BgNodeException($"Unable to determine type of parameter '{parameterExpr.Name}'");
					}
				}
				else
				{
					Delegate compiled = Expression.Lambda(expr).Compile();
					args[idx] = compiled.DynamicInvoke();
				}
			}
			return args;
		}

		static BgFileSet[] CreateReturnExprs(MethodInfo methodInfo, BgString name, Type type)
		{
			if (type == typeof(Task))
			{
				return Array.Empty<BgFileSet>();
			}
			else if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(Task<>))
			{
				return CreateReturnExprsInner(methodInfo, name, type.GetGenericArguments()[0]);
			}
			else
			{
				throw new NotImplementedException();
			}
		}

		static BgFileSet[] CreateReturnExprsInner(MethodInfo methodInfo, BgString name, Type type)
		{
			Type[] outputTypes;
			if (IsValueTuple(type))
			{
				outputTypes = type.GetGenericArguments();
			}
			else
			{
				outputTypes = new[] { type };
			}

			BgFileSet[] outputExprs = new BgFileSet[outputTypes.Length];
			for (int idx = 0; idx < outputTypes.Length; idx++)
			{
				outputExprs[idx] = CreateOutputExpr(methodInfo, name, idx, outputTypes[idx]);
			}
			return outputExprs;
		}

		static BgFileSet CreateOutputExpr(MethodInfo methodInfo, BgString name, int index, Type type)
		{
			if (type == typeof(BgFileSet))
			{
				return new BgFileSetTagFromStringExpr(BgString.Format("#{0}${1}", name, index));
			}
			else
			{
				throw new BgNodeException($"Unsupported return type for {methodInfo.Name}: {type.Name}");
			}
		}

		internal static bool IsValueTuple(Type returnType)
		{
			if (returnType.IsGenericType)
			{
				Type genericType = returnType.GetGenericTypeDefinition();
				if (genericType.FullName != null && genericType.FullName.StartsWith("System.ValueTuple`", StringComparison.Ordinal))
				{
					return true;
				}
			}
			return false;
		}

		static BgString GetDefaultNodeName(MethodInfo methodInfo, object?[] args)
		{
			// Check if it's got an attribute override for the node name
			BgNodeNameAttribute? nameAttr = methodInfo.GetCustomAttribute<BgNodeNameAttribute>();
			if (nameAttr != null)
			{
				return GetNodeNameFromTemplate(nameAttr.Template, methodInfo.GetParameters(), args);
			}
			else
			{
				return GetNodeNameFromMethodName(methodInfo.Name);
			}
		}

		static BgString GetNodeNameFromTemplate(string template, ParameterInfo[] parameters, object?[] args)
		{
			// Create a list of lazily computed string fragments which comprise the evaluated name
			List<BgString> fragments = new List<BgString>();

			int lastIdx = 0;
			for (int nextIdx = 0; nextIdx < template.Length; nextIdx++)
			{
				if (template[nextIdx] == '{')
				{
					if (nextIdx + 1 < template.Length && template[nextIdx + 1] == '{')
					{
						fragments.Add(template.Substring(lastIdx, nextIdx - lastIdx));
						lastIdx = ++nextIdx;
					}
					else
					{
						fragments.Add(template.Substring(lastIdx, nextIdx - lastIdx));
						nextIdx++;

						int endIdx = template.IndexOf('}', nextIdx);
						if (endIdx == -1)
						{
							throw new BgNodeException($"Unterminated parameter expression for {nameof(BgNodeNameAttribute)} in {template}");
						}

						StringView paramName = new StringView(template, nextIdx, endIdx - nextIdx);

						int paramIdx = Array.FindIndex(parameters, x => x.Name != null && paramName.Equals(x.Name, StringComparison.Ordinal));
						if (paramIdx == -1)
						{
							throw new BgNodeException($"Unable to find parameter named {paramName} in {template}");
						}

						object? arg = args[paramIdx];
						if (arg != null)
						{
							if (arg is IBgExpr expr)
							{
								fragments.Add(expr.ToBgString());
							}
							else
							{
								fragments.Add(arg.ToString() ?? String.Empty);
							}
						}

						lastIdx = nextIdx = endIdx + 1;
					}
				}
				else if (template[nextIdx] == '}')
				{
					if (nextIdx + 1 < template.Length && template[nextIdx + 1] == '{')
					{
						fragments.Add(template.Substring(lastIdx, nextIdx - lastIdx));
						lastIdx = ++nextIdx;
					}
				}
			}
			fragments.Add(template.Substring(lastIdx, template.Length - lastIdx));

			return BgString.Join(BgString.Empty, fragments);
		}

		/// <summary>
		/// Inserts spaces into a PascalCase method name to create a node name
		/// </summary>
		public static string GetNodeNameFromMethodName(string methodName)
		{
			StringBuilder name = new StringBuilder();
			name.Append(methodName[0]);

			int length = methodName.Length;
			if (length > 5 && methodName.EndsWith("Async", StringComparison.Ordinal))
			{
				length -= 5;
			}

			bool bIsAcronym = false;
			for (int idx = 1; idx < length; idx++)
			{
				bool bLastIsUpper = Char.IsUpper(methodName[idx - 1]);
				bool bNextIsUpper = Char.IsUpper(methodName[idx]);
				if (bLastIsUpper && bNextIsUpper)
				{
					bIsAcronym = true;
				}
				else if (bIsAcronym)
				{
					name.Insert(name.Length - 2, ' ');
					bIsAcronym = false;
				}
				else if (!bLastIsUpper && bNextIsUpper)
				{
					name.Append(' ');
				}
				name.Append(methodName[idx]);
			}

			return name.ToString();
		}

		/// <summary>
		/// Gets the signature for a method
		/// </summary>
		public static string GetSignature(MethodInfo methodInfo)
		{
			StringBuilder arguments = new StringBuilder();
			foreach (ParameterInfo parameterInfo in methodInfo.GetParameters())
			{
				arguments.AppendLine(parameterInfo.ParameterType.FullName);
			}
			return $"{methodInfo.Name}:{Digest.Compute<Sha1>(arguments.ToString())}";
		}

		/// <summary>
		/// Creates a concrete node
		/// </summary>
		/// <param name="context"></param>
		/// <param name="graph"></param>
		/// <param name="agent"></param>
		/// <returns></returns>
		internal void AddToGraph(BgExprContext context, BgGraph graph, BgAgent agent)
		{
			HashSet<string> inputTags = new HashSet<string>();
			inputTags.UnionWith(InputDependencies.ComputeTags(context));
			inputTags.UnionWith(_arguments.OfType<BgFileSet>().Select(x => x.ComputeTag(context)));
			inputTags.UnionWith(_arguments.OfType<BgList<BgFileSet>>().SelectMany(x => x.GetEnumerable(context)).Select(x => x.ComputeTag(context)));

			HashSet<string> afterTags = new HashSet<string>(inputTags);
			afterTags.UnionWith(AfterDependencies.ComputeTags(context));

			string name = Name.Compute(context);
			BgNodeOutput[] inputDependencies = inputTags.Select(x => graph.TagNameToNodeOutput[x]).Distinct().ToArray();
			BgNodeOutput[] afterDependencies = afterTags.Select(x => graph.TagNameToNodeOutput[x]).Distinct().ToArray();
			string[] outputNames = Array.ConvertAll(ResultExprs, x => x.ComputeTag(context));
			BgNode[] inputNodes = inputDependencies.Select(x => x.ProducingNode).Distinct().ToArray();
			BgNode[] afterNodes = afterDependencies.Select(x => x.ProducingNode).Distinct().ToArray();
			bool runEarly = CanRunEarly.Compute(context);

			List<object?> arguments = _arguments.ConvertAll(x => (x is IBgExpr expr) ? expr.Compute(context) : x);
			BgExpressionNode node = new BgExpressionNode(name, _method, arguments, inputDependencies, outputNames, inputNodes, afterNodes, Array.Empty<FileReference>());
			node.RunEarly = runEarly;

			agent.Nodes.Add(node);
			graph.NameToNode.Add(name, node);

			foreach (BgDiagnosticSpec precondition in Diagnostics.GetEnumerable(context))
			{
				precondition.AddToGraph(context, graph, agent, node);
			}

			foreach (BgNodeOutput output in node.Outputs)
			{
				graph.TagNameToNodeOutput.Add(output.TagName, output);
			}
		}

		/// <summary>
		/// Allows using the 
		/// </summary>
		/// <param name="nodeSpec"></param>
		public static implicit operator BgList<BgFileSet>(BgNodeSpec nodeSpec)
		{
			return nodeSpec.InputsAndOutputs.Add(nodeSpec.Output);
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
		internal BgNodeSpec(MethodCallExpression call)
			: base(call)
		{
			Type returnType = call.Method.ReturnType;
			if (IsValueTuple(returnType))
			{
				Output = (T)Activator.CreateInstance(returnType, ResultExprs)!;
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
		/// <param name="nodeSpec">The node specification</param>
		/// <param name="tokens">Tokens to add dependencies on</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T Requires<T>(this T nodeSpec, params BgList<BgFileSet>[] tokens) where T : BgNodeSpec
		{
			foreach (BgList<BgFileSet> tokenSet in tokens)
			{
				nodeSpec.InputDependencies = nodeSpec.InputDependencies.Add(tokenSet);
			}
			return nodeSpec;
		}

		/// <summary>
		/// Add weak dependencies onto other nodes or outputs. The producing nodes must complete successfully if they are part of the graph, but outputs from them will not be 
		/// transferred to the machine running this node.
		/// </summary>
		/// <param name="nodeSpec">The node specification</param>
		/// <param name="tokens">Tokens to add dependencies on</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T After<T>(this T nodeSpec, params BgList<BgFileSet>[] tokens) where T : BgNodeSpec
		{
			foreach (BgList<BgFileSet> tokenSet in tokens)
			{
				nodeSpec.AfterDependencies = nodeSpec.AfterDependencies.Add(tokenSet);
			}
			return nodeSpec;
		}

		/// <summary>
		/// Adds a warning when executing this node
		/// </summary>
		/// <param name="nodeSpec">The node specification</param>
		/// <param name="condition">Condition for writing the message</param>
		/// <param name="message">Message to output</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static BgNodeSpec WarnIf<T>(this T nodeSpec, BgBool condition, BgString message) where T : BgNodeSpec
		{
			nodeSpec.Diagnostics = nodeSpec.Diagnostics.AddIf(condition, new BgDiagnosticSpec(LogLevel.Warning, message));
			return nodeSpec;
		}

		/// <summary>
		/// Adds an error when executing this node
		/// </summary>
		/// <param name="nodeSpec">The node specification</param>
		/// <param name="condition">Condition for writing the message</param>
		/// <param name="message">Message to output</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static BgNodeSpec ErrorIf<T>(this T nodeSpec, BgBool condition, BgString message) where T : BgNodeSpec
		{
			nodeSpec.Diagnostics = nodeSpec.Diagnostics.AddIf(condition, new BgDiagnosticSpec(LogLevel.Error, message));
			return nodeSpec;
		}
	}
}
