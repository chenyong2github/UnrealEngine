// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.BuildGraph.Expressions
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
	/// Information about the method bound to execute a node
	/// </summary>
	public class BgMethod
	{
		/// <summary>
		/// Method to call
		/// </summary>
		public MethodInfo Method { get; }

		/// <summary>
		/// Arguments to the method
		/// </summary>
		public IReadOnlyList<object?> Arguments { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgMethod(MethodInfo method, IReadOnlyList<object?> arguments)
		{
			Method = method;
			Arguments = arguments;
		}
	}

	/// <summary>
	/// Specification for a node to execute
	/// </summary>
	public class BgNode : BgExpr
	{
		/// <summary>
		/// Name of the node
		/// </summary>
		public BgString Name { get; }

		/// <summary>
		/// Method to execute
		/// </summary>
		protected BgMethod Method { get; }

		/// <summary>
		/// Expression arguments from the bound function.
		/// </summary>
		protected BgExpr[] ExpressionArguments { get; }

		/// <summary>
		/// The default output of this node. Includes all other outputs. 
		/// </summary>
		public BgFileSet DefaultOutput { get; }

		/// <summary>
		/// Explicitly tagged outputs from this node
		/// </summary>
		public IReadOnlyList<BgExpr> TaggedOutputs { get; protected set; } = Array.Empty<BgExpr>();

		/// <summary>
		/// Agent for the node to be run on
		/// </summary>
		public BgAgent Agent { get; }

		/// <summary>
		/// Tokens for inputs of this node
		/// </summary>
		public BgList<BgFileSet> Inputs { get; }

		/// <summary>
		/// Weak dependency on outputs that must be generated for the node to run, without making those dependencies inputs.
		/// </summary>
		public BgList<BgFileSet> Fences { get; }

		/// <summary>
		/// Whether this node should start running as soon as its dependencies are ready, even if other nodes in the same agent are not.
		/// </summary>
		public BgBool RunEarly { get; } = BgBool.False;

		/// <summary>
		/// Labels that this node contributes to
		/// </summary>
		public BgList<BgLabel> Labels { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgNode(BgString name, BgMethod method, BgAgent agent, BgList<BgFileSet> inputs, BgList<BgFileSet> fences, BgBool runEarly, BgList<BgLabel> labels)
			: base(BgExprFlags.ForceFragment)
		{
			Name = name;

			ParameterInfo[] parameters = method.Method.GetParameters();
			ExpressionArguments = new BgExpr[parameters.Length];

			object?[] constantArguments = new object?[method.Arguments.Count];
			for (int idx = 0; idx < parameters.Length; idx++)
			{
				if (typeof(BgExpr).IsAssignableFrom(parameters[idx].ParameterType))
				{
					ExpressionArguments[idx] = (BgExpr)method.Arguments[idx]!;
					constantArguments[idx] = null;
				}
				else
				{
					ExpressionArguments[idx] = BgBool.False;
					constantArguments[idx] = method.Arguments[idx];
				}
			}
			Method = new BgMethod(method.Method, constantArguments);

			Agent = agent;
			DefaultOutput = new BgFileSetFromNodeOutputExpr(this, 0);
			Inputs = inputs;
			Fences = fences;
			RunEarly = runEarly;
			Labels = labels;
		}

		int written = 0;

		/// <inheritdoc/>
		public override void Write(BgBytecodeWriter writer)
		{
			writer.WriteOpcode(BgOpcode.Node);

			writer.WriteExpr(Name ?? BgString.Empty);
			writer.WriteExpr(Agent);
			writer.WriteMethod(Method);

			writer.WriteUnsignedInteger((uint)ExpressionArguments.Length);
			foreach (BgExpr argument in ExpressionArguments)
			{
				writer.WriteExpr(argument);
			}

			writer.WriteExpr(Inputs);
			writer.WriteExpr(Fences);
			writer.WriteUnsignedInteger(TaggedOutputs.Count);
			writer.WriteExpr(RunEarly);
			writer.WriteExpr(Labels);
			written++;
		}

		/// <summary>
		/// Gets the default tag name for the numbered output index
		/// </summary>
		/// <param name="name">Name of the node</param>
		/// <param name="index">Index of the output. Index zero is the default, others are explicit.</param>
		/// <returns></returns>
		internal static string GetDefaultTagName(string name, int index)
		{
			return $"#{name}${index}";
		}

		/// <summary>
		/// Implicit conversion to a fileset
		/// </summary>
		/// <param name="node"></param>
		public static implicit operator BgFileSet(BgNode node)
		{
			return new BgFileSetFromNodeExpr(node);
		}

		/// <summary>
		/// Implicit conversion to a fileset
		/// </summary>
		/// <param name="node"></param>
		public static implicit operator BgList<BgFileSet>(BgNode node)
		{
			return (BgFileSet)node;
		}

		/// <inheritdoc/>
		public override BgString ToBgString() => Name ?? BgString.Empty;
	}

	/// <summary>
	/// Nodespec with a typed return value
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class BgNode<T> : BgNode
	{
		/// <summary>
		/// Output from this node
		/// </summary>
		public T Output { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BgNode(BgString name, BgMethod handler, BgAgent agent, BgList<BgFileSet> inputs, BgList<BgFileSet> fences, BgBool runEarly, BgList<BgLabel> labels)
			: base(name, handler, agent, inputs, fences, runEarly, labels)
		{
			Output = CreateOutput();
		}

		T CreateOutput()
		{
			Type type = typeof(T);
			if (IsValueTuple(type))
			{
				BgExpr[] outputs = CreateOutputExprs(type.GetGenericArguments());
				TaggedOutputs = outputs;
				return (T)Activator.CreateInstance(type, outputs)!;
			}
			else
			{
				BgExpr[] outputs = CreateOutputExprs(new[] { type });
				TaggedOutputs = outputs;
				return (T)(object)outputs[0];
			}
		}

		BgExpr[] CreateOutputExprs(Type[] types)
		{
			BgExpr[] outputs = new BgExpr[types.Length];
			for (int idx = 0; idx < types.Length; idx++)
			{
				outputs[idx] = CreateOutputExpr(types[idx], idx);
			}
			return outputs;
		}

		BgExpr CreateOutputExpr(Type type, int index)
		{
			if (type == typeof(BgFileSet))
			{
				return new BgFileSetFromNodeOutputExpr(this, index);
			}
			else
			{
				throw new NotImplementedException();
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
	}

	/// <summary>
	/// Class that allows constructing a nodespec
	/// </summary>
	public class BgNodeSpecBuilder
	{
		/// <inheritdoc cref="BgNode.Name"/>
		public BgString Name { get; }

		/// <inheritdoc cref="BgNode.Method"/>
		protected BgMethod Handler { get; }

		/// <inheritdoc cref="BgNode.Agent"/>
		public BgAgent Agent { get; }

		/// <inheritdoc cref="BgNode.Inputs"/>
		public BgList<BgFileSet> Inputs { get; set; } = BgList<BgFileSet>.Empty;

		/// <inheritdoc cref="BgNode.Fences"/>
		public BgList<BgFileSet> Fences { get; set; } = BgList<BgFileSet>.Empty;

		/// <inheritdoc cref="BgNode.RunEarly"/>
		public BgBool RunEarly { get; set; } = BgBool.False;

		/// <inheritdoc cref="BgNode.Labels"/>
		public BgList<BgLabel> Labels { get; set; } = BgList<BgLabel>.Empty;

		/// <summary>
		/// Constructor
		/// </summary>
		public BgNodeSpecBuilder(BgString? name, MethodCallExpression call, BgAgent agent)
		{
			try
			{
				object?[] arguments = CreateArgumentExprs(call);

				Name = name ?? GetDefaultNodeName(call.Method, arguments);
				Handler = new BgMethod(call.Method, arguments);
				Agent = agent;
			}
			catch (Exception ex)
			{
				ExceptionUtils.AddContext(ex, $"while calling method {call.Method}");
				throw;
			}
		}

		static object?[] CreateArgumentExprs(MethodCallExpression call)
		{
			object?[] args = new object?[call.Arguments.Count];
			for (int idx = 0; idx < call.Arguments.Count; idx++)
			{
				Expression argumentExpr = call.Arguments[idx];
				if (argumentExpr is ParameterExpression parameterExpr)
				{
					if (parameterExpr.Type != typeof(BgContext))
					{
						throw new BgNodeException($"Unable to determine type of parameter '{parameterExpr.Name}'");
					}
				}
				else
				{
					Delegate compiled = Expression.Lambda(argumentExpr).Compile();
					args[idx] = compiled.DynamicInvoke();
				}
			}
			return args;
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
							if (arg is BgExpr expr)
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

			if (fragments.Count == 1)
			{
				return fragments[0];
			}
			else
			{
				return BgString.Join(BgString.Empty, fragments);
			}
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
		/// Construct a nodespec from the current parameters
		/// </summary>
		/// <returns>New nodespec</returns>
		public BgNode Construct()
		{
			return new BgNode(Name, Handler, Agent, Inputs, Fences, RunEarly, Labels);
		}
	}

	/// <summary>
	/// Specification for a node to execute that returns one or more tagged outputs
	/// </summary>
	/// <typeparam name="T">Return type from the node. Must consist of latent Bg* types (eg. BgToken)</typeparam>
	public class BgNodeSpecBuilder<T> : BgNodeSpecBuilder
	{
		/// <summary>
		/// Constructor
		/// </summary>
		internal BgNodeSpecBuilder(BgString? name, MethodCallExpression call, BgAgent agent)
			: base(name, call, agent)
		{
		}

		/// <summary>
		/// Construct a nodespec from the current parameters
		/// </summary>
		/// <returns>New nodespec</returns>
		public new BgNode<T> Construct()
		{
			return new BgNode<T>(Name, Handler, Agent, Inputs, Fences, RunEarly, Labels);
		}
	}

	/// <summary>
	/// Extension methods for BgNode types
	/// </summary>
	public static class BgNodeExtensions
	{
		/// <summary>
		/// Creates a node builder for the given agent
		/// </summary>
		/// <param name="agent">Agent to run the node</param>
		/// <param name="func">Function to execute</param>
		/// <returns>Node builder</returns>
		public static BgNodeSpecBuilder AddNode(this BgAgent agent, Expression<Func<BgContext, Task>> func)
		{
			MethodCallExpression call = (MethodCallExpression)func.Body;
			return new BgNodeSpecBuilder(null, call, agent);
		}

		/// <summary>
		/// Creates a node builder for the given agent
		/// </summary>
		/// <param name="agent">Agent to run the node</param>
		/// <param name="func">Function to execute</param>
		/// <returns>Node builder</returns>
		public static BgNodeSpecBuilder<T> AddNode<T>(this BgAgent agent, Expression<Func<BgContext, Task<T>>> func)
		{
			MethodCallExpression call = (MethodCallExpression)func.Body;
			return new BgNodeSpecBuilder<T>(null, call, agent);
		}

		/// <summary>
		/// Add dependencies onto other nodes or outputs. Outputs from the given tokens will be copied to the current machine before execution of the node.
		/// </summary>
		/// <param name="builder">The node builder</param>
		/// <param name="inputs">Files to add as inputs</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T Requires<T>(this T builder, params BgNode[] inputs) where T : BgNodeSpecBuilder
		{
			builder.Inputs = builder.Inputs.Add(inputs.Select(x => (BgFileSet)x));
			return builder;
		}

		/// <summary>
		/// Add dependencies onto other nodes or outputs. Outputs from the given tokens will be copied to the current machine before execution of the node.
		/// </summary>
		/// <param name="builder">The node builder</param>
		/// <param name="inputs">Files to add as inputs</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T Requires<T>(this T builder, params BgFileSet[] inputs) where T : BgNodeSpecBuilder
		{
			builder.Inputs = builder.Inputs.Add(inputs);
			return builder;
		}

		/// <summary>
		/// Add dependencies onto other nodes or outputs. Outputs from the given tokens will be copied to the current machine before execution of the node.
		/// </summary>
		/// <param name="builder">The node builder</param>
		/// <param name="inputs">Files to add as inputs</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T Requires<T>(this T builder, BgList<BgFileSet> inputs) where T : BgNodeSpecBuilder
		{
			builder.Inputs = builder.Inputs.Add(inputs);
			return builder;
		}

		/// <summary>
		/// Add weak dependencies onto other nodes or outputs. The producing nodes must complete successfully if they are part of the graph, but outputs from them will not be 
		/// transferred to the machine running this node.
		/// </summary>
		/// <param name="builder">The node builder</param>
		/// <param name="inputs">Files to add as inputs</param>
		/// <returns>The current node spec, to allow chaining calls</returns>
		public static T After<T>(this T builder, params BgFileSet[] inputs) where T : BgNodeSpecBuilder
		{
			builder.Fences = builder.Fences.Add(inputs);
			return builder;
		}
	}
}
