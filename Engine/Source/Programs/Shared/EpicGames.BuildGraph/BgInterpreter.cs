// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Exception thrown by the runtime
	/// </summary>
	public sealed class BgBytecodeException : Exception
	{
		/// <summary>
		/// Source file that the error was thrown from
		/// </summary>
		public string SourceFile { get; }

		/// <summary>
		/// Line number that threw the exception
		/// </summary>
		public int SourceLine { get; }

		/// <summary>
		/// Message to display
		/// </summary>
		public string Diagnostic { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="sourceFile"></param>
		/// <param name="sourceLine"></param>
		/// <param name="diagnostic"></param>
		public BgBytecodeException(string sourceFile, int sourceLine, string diagnostic)
		{
			SourceFile = sourceFile;
			SourceLine = sourceLine;
			Diagnostic = diagnostic;
		}

		/// <inheritdoc/>
		public override string ToString() => $"{SourceFile}({SourceLine}): {Diagnostic}";
	}

	/// <summary>
	/// Interprets compiled buildgraph bytecode
	/// </summary>
	public class BgInterpreter
	{
		class Frame
		{
			public int Offset { get; set; }
			public IReadOnlyList<object> Arguments { get; }
			public Dictionary<int, object> Objects { get; set; }

			public Frame(Frame other)
			{
				Offset = other.Offset;
				Arguments = other.Arguments;
				Objects = other.Objects;
			}

			public Frame(int offset, IReadOnlyList<object> arguments)
			{
				Offset = offset;
				Arguments = arguments;
				Objects = new Dictionary<int, object>();
			}
		}

		readonly byte[] _data;
		readonly BgMethod[] _methods;
		readonly IReadOnlyDictionary<string, string> _options;
		readonly BgBytecodeVersion _version;
		readonly int[] _fragments;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="data"></param>
		/// <param name="lambdas">Handlers for each node type</param>
		/// <param name="options">Options for evaluating the graph</param>
		public BgInterpreter(byte[] data, BgMethod[] lambdas, IReadOnlyDictionary<string, string> options)
		{
			_data = data;
			_methods = lambdas;
			_options = options;

			int offset = 0;

			_version = (BgBytecodeVersion)VarInt.ReadUnsigned(data[offset..], out int bytesRead);
			offset += bytesRead;

			int fragmentCount = (int)VarInt.ReadUnsigned(data[offset..], out bytesRead);
			offset += bytesRead;

			int[] lengths = new int[fragmentCount];
			for (int idx = 0; idx < fragmentCount; idx++)
			{
				lengths[idx] = (int)VarInt.ReadUnsigned(data[offset..], out bytesRead);
				offset += bytesRead;
			}

			_fragments = new int[fragmentCount];
			for (int idx = 0; idx < fragmentCount; idx++)
			{
				_fragments[idx] = offset;
				offset += lengths[idx];
			}
		}

		/// <summary>
		/// Evaluates the graph
		/// </summary>
		public object Evaluate()
		{
			// Take the given parameters, evaluate the graph that was produced (creating a map of all named entites)
			// Filter the map to the required target
			// Evaluate it fully into a BgGraph
			return Evaluate(new Frame(_fragments[0], Array.Empty<object>()));
		}

		object Evaluate(Frame frame)
		{
			BgOpcode opcode = ReadOpcode(frame);
			switch (opcode)
			{
				#region Bool opcodes

				case BgOpcode.BoolFalse:
					return false;
				case BgOpcode.BoolTrue:
					return true;
				case BgOpcode.BoolNot:
					return !(bool)Evaluate(frame);
				case BgOpcode.BoolAnd:
					{
						bool lhs = (bool)Evaluate(frame);
						bool rhs = (bool)Evaluate(frame);
						return lhs & rhs;
					}
				case BgOpcode.BoolOr:
					{
						bool lhs = (bool)Evaluate(frame);
						bool rhs = (bool)Evaluate(frame);
						return lhs | rhs;
					}
				case BgOpcode.BoolXor:
					{
						bool lhs = (bool)Evaluate(frame);
						bool rhs = (bool)Evaluate(frame);
						return lhs ^ rhs;
					}
				case BgOpcode.BoolEq:
					{
						bool lhs = (bool)Evaluate(frame);
						bool rhs = (bool)Evaluate(frame);
						return lhs == rhs;
					}
				case BgOpcode.BoolOption:
					{
						string name = (string)Evaluate(frame);
						string label = (string)Evaluate(frame);
						string description = (string)Evaluate(frame);
						bool defaultValue = (bool)Evaluate(frame);

						bool value = defaultValue;
						if (_options.TryGetValue(name, out string? str))
						{
							value = Boolean.Parse(str);
						}

						return value;
					}

				#endregion
				#region Integer opcodes

				case BgOpcode.IntLiteral:
					return ReadSignedInteger(frame);
				case BgOpcode.IntEq:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs == rhs;
					}
				case BgOpcode.IntLt:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs < rhs;
					}
				case BgOpcode.IntGt:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs > rhs;
					}
				case BgOpcode.IntAdd:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs + rhs;
					}
				case BgOpcode.IntMultiply:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs * rhs;
					}
				case BgOpcode.IntDivide:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs / rhs;
					}
				case BgOpcode.IntModulo:
					{
						int lhs = (int)Evaluate(frame);
						int rhs = (int)Evaluate(frame);
						return lhs % rhs;
					}
				case BgOpcode.IntNegate:
					return -(int)Evaluate(frame);

				#endregion
				#region String opcodes

				case BgOpcode.StrEmpty:
					return String.Empty;
				case BgOpcode.StrLiteral:
					return ReadString(frame);
				case BgOpcode.StrCompare:
					{
						string lhs = (string)Evaluate(frame);
						string rhs = (string)Evaluate(frame);
						StringComparison comparison = (StringComparison)ReadUnsignedInteger(frame);
						return String.Compare(lhs, rhs, comparison);
					}
				case BgOpcode.StrConcat:
					{
						string lhs = (string)Evaluate(frame);
						string rhs = (string)Evaluate(frame);
						return lhs + rhs;
					}
				case BgOpcode.StrFormat:
					{
						string format = (string)Evaluate(frame);

						int count = (int)ReadUnsignedInteger(frame);
						object[] arguments = new object[count];

						for (int idx = 0; idx < count; idx++)
						{
							object argument = Evaluate(frame);
							arguments[idx] = argument;
						}

						return String.Format(format, arguments);
					}
				case BgOpcode.StrSplit:
					{
						string source = (string)Evaluate(frame);
						string separator = (string)Evaluate(frame);
						return source.Split(separator, StringSplitOptions.RemoveEmptyEntries);
					}
				case BgOpcode.StrJoin:
					{
						string lhs = (string)Evaluate(frame);
						IEnumerable<object> rhs = (IEnumerable<object>)Evaluate(frame);
						return String.Join(lhs, rhs);
					}
				case BgOpcode.StrIsMatch:
					{
						string input = (string)Evaluate(frame);
						string pattern = ReadString(frame);
						return Regex.IsMatch(input, pattern);
					}
				case BgOpcode.StrReplace:
					{
						string input = (string)Evaluate(frame);
						string pattern = ReadString(frame);
						string replacement = (string)Evaluate(frame);
						return Regex.Replace(input, pattern, replacement);
					}
				case BgOpcode.StrOption:
					{
						string name = (string)Evaluate(frame);
						string label = (string)Evaluate(frame);
						string description = (string)Evaluate(frame);
						int style = (int)ReadUnsignedInteger(frame);
						string defaultValue = (string)Evaluate(frame);
						string pattern = (string)Evaluate(frame);
						string patternFailed = (string)Evaluate(frame);
						List<string> values = EvaluateList<string>(frame);
						List<string> valueDescriptions = EvaluateList<string>(frame);

						string value = defaultValue;
						if (_options.TryGetValue(name, out string? option))
						{
							value = option;
						}

						return value;
					}

				#endregion
				#region Enum opcodes

				case BgOpcode.EnumConstant:
					return ReadSignedInteger(frame);
				case BgOpcode.EnumParse:
					{
						string name = (string)Evaluate(frame);
						List<string> names = EvaluateList<string>(frame);
						List<int> values = EvaluateList<int>(frame);

						for (int idx = 0; idx < names.Count; idx++)
						{
							if (String.Equals(names[idx], name, StringComparison.OrdinalIgnoreCase))
							{
								return values[idx];
							}
						}

						throw new InvalidDataException($"Unable to parse enum '{name}'");
					}
				case BgOpcode.EnumToString:
					{
						int value = (int)Evaluate(frame);
						List<string> names = EvaluateList<string>(frame);
						List<int> values = EvaluateList<int>(frame);

						for (int idx = 0; idx < names.Count; idx++)
						{
							if (value == values[idx])
							{
								return names[idx];
							}
						}

						return $"{value}";
					}

				#endregion
				#region List opcodes

				case BgOpcode.ListEmpty:
					return Enumerable.Empty<object>();
				case BgOpcode.ListPush:
					{
						IEnumerable<object> list = (IEnumerable<object>)Evaluate(frame);
						object item = Evaluate(frame);

						return list.Concat(new[] { item });
					}
				case BgOpcode.ListPushLazy:
					{
						IEnumerable<object> list = (IEnumerable<object>)Evaluate(frame);
						int fragment = ReadFragment(frame);

						IEnumerable<object> item = LazyEvaluateItem(new Frame(frame), fragment);
						return list.Concat(item);
					}
				case BgOpcode.ListCount:
					{
						IEnumerable<object> list = (IEnumerable<object>)Evaluate(frame);
						return list.Count();
					}
				case BgOpcode.ListElement:
					{
						IEnumerable<object> list = (IEnumerable<object>)Evaluate(frame);
						int index = (int)Evaluate(frame);
						return list.ElementAt(index);
					}
				case BgOpcode.ListConcat:
					{
						IEnumerable<object> lhs = (IEnumerable<object>)Evaluate(frame);
						IEnumerable<object> rhs = (IEnumerable<object>)Evaluate(frame);
						return Enumerable.Concat(lhs, rhs);
					}
				case BgOpcode.ListUnion:
					{
						IEnumerable<object> lhs = (IEnumerable<object>)Evaluate(frame);
						IEnumerable<object> rhs = (IEnumerable<object>)Evaluate(frame);
						return lhs.Union(rhs);
					}
				case BgOpcode.ListExcept:
					{
						IEnumerable<object> lhs = (IEnumerable<object>)Evaluate(frame);
						IEnumerable<object> rhs = (IEnumerable<object>)Evaluate(frame);
						return lhs.Except(rhs);
					}
				case BgOpcode.ListSelect:
					{
						IEnumerable<object> source = (IEnumerable<object>)Evaluate(frame);
						int fragment = ReadFragment(frame);

						return source.Select(x => Call(fragment, new object[] { x }));
					}
				case BgOpcode.ListWhere:
					{
						IEnumerable<object> source = (IEnumerable<object>)Evaluate(frame);
						int fragment = ReadFragment(frame);

						return source.Where(x => (bool)Call(fragment, new object[] { x }));
					}
				case BgOpcode.ListDistinct:
					{
						IEnumerable<object> source = (IEnumerable<object>)Evaluate(frame);
						return source.Distinct();
					}
				case BgOpcode.ListContains:
					{
						IEnumerable<object> source = (IEnumerable<object>)Evaluate(frame);
						object item = Evaluate(frame);
						return source.Contains(item);
					}
				case BgOpcode.ListLazy:
					{
						int fragment = ReadFragment(frame);
						return LazyEvaluateList(new Frame(frame), fragment);
					}
				case BgOpcode.ListOption:
					{
						string name = (string)Evaluate(frame);
						string label = (string)Evaluate(frame);
						string description = (string)Evaluate(frame);
						BgListOptionStyle style = (BgListOptionStyle)ReadUnsignedInteger(frame);
						string defaultValue = (string)Evaluate(frame);
						string pattern = (string)Evaluate(frame);
						string patternFailed = (string)Evaluate(frame);
						List<string> values = EvaluateList<string>(frame);
						List<string> valueDescriptions = EvaluateList<string>(frame);

						string value = defaultValue;
						if (_options.TryGetValue(name, out string? option))
						{
							value = option;
						}

						return value.Split('+', ';').Select(x => (object)x).ToList();
					}

				#endregion
				#region Fileset opcodes

				case BgOpcode.FileSetFromNode:
					{
						BgBytecodeNode node = (BgBytecodeNode)Evaluate(frame);
						return node.InputDependencies.SelectMany(x => x.Outputs).Concat(node.Outputs).ToArray();
					}
				case BgOpcode.FileSetFromNodeOutput:
					{
						BgBytecodeNode node = (BgBytecodeNode)Evaluate(frame);
						int outputIndex = (int)ReadUnsignedInteger(frame);
						return new[] { node.Outputs[outputIndex] };
					}

				#endregion
				#region Object opcodes

				case BgOpcode.ObjEmpty:
					return ImmutableDictionary<string, object>.Empty.WithComparers(StringComparer.Ordinal);
				case BgOpcode.ObjGet:
					{
						ImmutableDictionary<string, object> obj = (ImmutableDictionary<string, object>)Evaluate(frame);
						string name = ReadString(frame);
						object defaultValue = Evaluate(frame);

						object value;
						if (!obj.TryGetValue(name, out value))
						{
							value = defaultValue;
						}

						return value;
					}
				case BgOpcode.ObjSet:
					{
						ImmutableDictionary<string, object> obj = (ImmutableDictionary<string, object>)Evaluate(frame);
						string name = ReadString(frame);
						object value = Evaluate(frame);
						return obj.SetItem(name, value);
					}

				#endregion
				#region Function opcodes

				case BgOpcode.Call:
					{
						int count = (int)ReadUnsignedInteger(frame);

						object[] arguments = new object[count];
						for (int idx = 0; idx < count; idx++)
						{
							arguments[idx] = Evaluate(frame);
						}

						int function = (int)ReadUnsignedInteger(frame);
						return Evaluate(new Frame(_fragments[function], arguments));
					}
				case BgOpcode.Argument:
					{
						int index = (int)ReadUnsignedInteger(frame);
						return frame.Arguments[index];
					}
				case BgOpcode.Jump:
					{
						int fragment = (int)ReadUnsignedInteger(frame);
						return Jump(frame, fragment);
					}

				#endregion
				#region Generic opcodes

				case BgOpcode.Choose:
					{
						bool condition = (bool)Evaluate(frame);

						int fragmentIfTrue = (int)ReadUnsignedInteger(frame);
						int fragmentIfFalse = (int)ReadUnsignedInteger(frame);

						return Jump(frame, condition ? fragmentIfTrue : fragmentIfFalse);
					}
				case BgOpcode.Throw:
					{
						string sourceFile = ReadString(frame);
						int sourceLine = (int)ReadUnsignedInteger(frame);
						string message = (string)Evaluate(frame);
						throw new BgBytecodeException(sourceFile, sourceLine, message);
					}
				case BgOpcode.Null:
					return null!;

				#endregion
				#region Graph opcodes

				case BgOpcode.Node:
					{
						string name = (string)Evaluate(frame);
						BgAgent agent = (BgAgent)Evaluate(frame);
						BgMethod method = _methods[(int)ReadUnsignedInteger(frame)];

						int argumentCount = (int)ReadUnsignedInteger(frame);
						object[] arguments = new object[argumentCount];

						for (int idx = 0; idx < argumentCount; idx++)
						{
							arguments[idx] = Evaluate(frame);
						}

						List<BgNodeOutput> inputs = ReadNodeOutputList(frame);
						List<BgNodeOutput> fences = ReadNodeOutputList(frame);
						int taggedOutputCount = (int)ReadUnsignedInteger(frame);
						bool runEarly = (bool)Evaluate(frame);
						List<BgLabel> labels = ((IEnumerable<object>)Evaluate(frame)).Select(x => (BgLabel)x).ToList();

						HashSet<BgNode> inputDependencies = new HashSet<BgNode>();
						foreach (BgNodeOutput input in inputs)
						{
							inputDependencies.Add(input.ProducingNode);
							inputDependencies.UnionWith(input.ProducingNode.InputDependencies);
						}

						BgNode[] orderDependencies = fences.Select(x => x.ProducingNode).Distinct().ToArray();

						string[] outputNames = Enumerable.Range(0, taggedOutputCount).Select(x => BgNodeSpec.GetDefaultTagName(name, x)).ToArray();

						BgBytecodeNode node = new BgBytecodeNode(agent, name, method, arguments, inputs, outputNames, inputDependencies.ToArray(), orderDependencies.ToArray(), Array.Empty<FileReference>(), labels);
						return node;
					}
				case BgOpcode.Agent:
					{
						string name = (string)Evaluate(frame);
						string[] types = ((IEnumerable<object>)Evaluate(frame)).Select(x => (string)x).ToArray();
						return new BgAgent(name, types);
					}
				case BgOpcode.Aggregate:
					{
						string name = (string)Evaluate(frame);
						List<BgNodeOutput> requires = ReadNodeOutputList(frame);
						BgLabel? label = (BgLabel?)Evaluate(frame);

						return new BgBytecodeAggregate(name, requires.Select(x => x.ProducingNode), label);
					}
				case BgOpcode.Label:
					{
						string? dashboardName = NullForEmptyString((string)Evaluate(frame));
						string? dashboardCategory = NullForEmptyString((string)Evaluate(frame));
						string? ugsBadge = NullForEmptyString((string)Evaluate(frame));
						string? ugsProject = NullForEmptyString((string)Evaluate(frame));
						string change = (string)Evaluate(frame);

						BgLabelChange labelChange = BgLabelChange.Current;
						if (change.Length > 0)
						{
							labelChange = Enum.Parse<BgLabelChange>(change);
						}

						return new BgLabel(dashboardName, dashboardCategory, ugsBadge, ugsProject, labelChange);
					}
				case BgOpcode.Graph:
					{
						List<BgBytecodeNode> nodes = EvaluateList<BgBytecodeNode>(frame);
						List<BgBytecodeAggregate> aggregates = EvaluateList<BgBytecodeAggregate>(frame);

						BgGraph graph = new BgGraph();
						foreach (BgBytecodeAggregate aggregate in aggregates)
						{
							graph.NameToAggregate[aggregate.Name] = aggregate;
							nodes.AddRange(aggregate.RequiredNodes.Select(x => (BgBytecodeNode)x));
						}

						HashSet<BgNode> uniqueNodes = new HashSet<BgNode>();
						HashSet<BgAgent> uniqueAgents = new HashSet<BgAgent>();
						foreach (BgBytecodeNode node in nodes)
						{
							RegisterNode(graph, node, uniqueNodes, uniqueAgents);
						}

						HashSet<BgLabel> labels = new HashSet<BgLabel>();
						foreach (BgBytecodeNode node in nodes)
						{
							foreach (BgLabel label in node.Labels)
							{
								labels.Add(label);
								label.RequiredNodes.Add(node);
							}
						}
						foreach (BgBytecodeAggregate aggregate in aggregates)
						{
							if (aggregate.Label != null)
							{
								aggregate.Label.RequiredNodes.UnionWith(aggregate.RequiredNodes);
								labels.Add(aggregate.Label);
							}
						}
						graph.Labels.AddRange(labels);

						return graph;
					}
				#endregion

				default:
					throw new InvalidDataException($"Invalid opcode: {opcode}");
			}
		}

		void RegisterNode(BgGraph graph, BgBytecodeNode node, HashSet<BgNode> uniqueNodes, HashSet<BgAgent> uniqueAgents)
		{
			if (uniqueNodes.Add(node))
			{
				foreach (BgBytecodeNode inputNode in node.InputDependencies)
				{
					RegisterNode(graph, inputNode, uniqueNodes, uniqueAgents);
				}

				BgAgent agent = node.Agent;
				if (uniqueAgents.Add(agent))
				{
					graph.Agents.Add(agent);
					graph.NameToAgent.Add(agent.Name, agent);
				}

				agent.Nodes.Add(node);
				graph.NameToNode.Add(node.Name, node);
			}
		}

		static string? NullForEmptyString(string str) => (str.Length == 0) ? null : str;

		List<T> EvaluateList<T>(Frame frame)
		{
			return ((IEnumerable<object>)Evaluate(frame)).Select(x => (T)x).ToList();
		}

		List<BgNodeOutput> ReadNodeOutputList(Frame frame)
		{
			IEnumerable<object> list = (IEnumerable<object>)Evaluate(frame);

			List<BgNodeOutput> outputs = new List<BgNodeOutput>();
			foreach (object item in list)
			{
				outputs.AddRange((BgNodeOutput[])item);
			}
			return outputs;
		}

		IEnumerable<object> LazyEvaluateItem(Frame frame, int fragment)
		{
			yield return Jump(frame, fragment);
		}

		IEnumerable<object> LazyEvaluateList(Frame frame, int fragment)
		{
			IEnumerable<object> result = (IEnumerable<object>)Jump(frame, fragment);
			foreach (object item in result)
			{
				yield return item;
			}
		}

		object Jump(Frame frame, int fragment)
		{
			object? result;
			if (!frame.Objects.TryGetValue(fragment, out result))
			{
				int prevOffset = frame.Offset;

				frame.Offset = _fragments[fragment];

				result = Evaluate(frame);
				frame.Objects.Add(fragment, result);

				frame.Offset = prevOffset;
			}
			return result;
		}

		object Call(int fragment, object[] arguments)
		{
			int offset = _fragments[fragment];
			return Evaluate(new Frame(offset, arguments));
		}

		int ReadFragment(Frame frame)
		{
			return (int)ReadUnsignedInteger(frame);
		}

		/// <summary>
		/// Reads an opcode from the input stream
		/// </summary>
		/// <returns>The opcode that was read</returns>
		BgOpcode ReadOpcode(Frame frame)
		{
			BgOpcode opcode = (BgOpcode)_data[frame.Offset++];
			return opcode;
		}

		/// <summary>
		/// Reads a string from the input stream
		/// </summary>
		string ReadString(Frame frame)
		{
			ReadOnlySpan<byte> buffer = _data[frame.Offset..];

			int length = (int)VarInt.ReadUnsigned(buffer, out int bytesRead);
			frame.Offset += bytesRead;

			string text = Encoding.UTF8.GetString(buffer.Slice(bytesRead, length));
			frame.Offset += length;

			return text;
		}

		/// <summary>
		/// Writes a signed integer value to the output
		/// </summary>
		/// <returns>the value that was read</returns>
		int ReadSignedInteger(Frame frame)
		{
			ulong encoded = ReadUnsignedInteger(frame);
			return DecodeSignedInteger(encoded);
		}

		/// <summary>
		/// Read an unsigned integer value from the input
		/// </summary>
		/// <returns>The value that was read</returns>
		ulong ReadUnsignedInteger(Frame frame)
		{
			ulong value = VarInt.ReadUnsigned(_data[frame.Offset..], out int bytesRead);
			frame.Offset += bytesRead;
			return value;
		}

		/// <summary>
		/// Decode a signed integer using the lower bit for the sign flag, allowing us to encode it more efficiently as a <see cref="VarInt"/>
		/// </summary>
		/// <param name="value">Value to be decoded</param>
		/// <returns>The decoded value</returns>
		static int DecodeSignedInteger(ulong value)
		{
			if ((value & 1) != 0)
			{
				return -(int)(value >> 1);
			}
			else
			{
				return (int)(value >> 1);
			}
		}

		/// <summary>
		/// Disassemble the current script to a logger
		/// </summary>
		/// <param name="logger"></param>
		public void Disassemble(ILogger logger)
		{
			logger.LogInformation("Version: {Version}", _version);
			for (int idx = 0; idx < _fragments.Length; idx++)
			{
				logger.LogInformation("");
				logger.LogInformation("Fragment {Idx}:", idx);
				Disassemble(new Frame(_fragments[idx], Array.Empty<object>()), logger);
			}
		}

		void Disassemble(Frame frame, ILogger logger)
		{
			BgOpcode opcode = Trace(frame, null, ReadOpcode, logger);
			switch (opcode)
			{
				#region Bool opcodes

				case BgOpcode.BoolFalse:
				case BgOpcode.BoolTrue:
					break;
				case BgOpcode.BoolNot:
					Disassemble(frame, logger);
					break;
				case BgOpcode.BoolAnd:
				case BgOpcode.BoolOr:
				case BgOpcode.BoolXor:
				case BgOpcode.BoolEq:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.BoolOption:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;

				#endregion
				#region Integer opcodes

				case BgOpcode.IntLiteral:
					Trace(frame, null, ReadSignedInteger, logger);
					break;
				case BgOpcode.IntEq:
				case BgOpcode.IntLt:
				case BgOpcode.IntGt:
				case BgOpcode.IntAdd:
				case BgOpcode.IntMultiply:
				case BgOpcode.IntDivide:
				case BgOpcode.IntModulo:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.IntNegate:
					Disassemble(frame, logger);
					break;

				#endregion
				#region String opcodes

				case BgOpcode.StrEmpty:
					break;
				case BgOpcode.StrLiteral:
					Trace(frame, ReadString, x => $"\"{x}\"", logger);
					break;
				case BgOpcode.StrCompare:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Trace(frame, "type", ReadUnsignedInteger, logger);
					break;
				case BgOpcode.StrConcat:
				case BgOpcode.StrSplit:
				case BgOpcode.StrJoin:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.StrFormat:
					{
						Disassemble(frame, logger);

						int count = (int)Trace(frame, "count", ReadUnsignedInteger, logger);
						for (int idx = 0; idx < count; idx++)
						{
							Disassemble(frame, logger);
						}

						break;
					}
				case BgOpcode.StrIsMatch:
					Disassemble(frame, logger);
					Trace(frame, "pattern", ReadString, logger);
					break;
				case BgOpcode.StrReplace:
					Disassemble(frame, logger);
					Trace(frame, "pattern", ReadString, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.StrOption:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Trace(frame, "style", ReadUnsignedInteger, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;

				#endregion
				#region Enum opcodes

				case BgOpcode.EnumConstant:
					Trace(frame, "value", ReadSignedInteger, logger);
					break;
				case BgOpcode.EnumParse:
				case BgOpcode.EnumToString:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;

				#endregion
				#region List opcodes

				case BgOpcode.ListEmpty:
					break;
				case BgOpcode.ListPush:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.ListPushLazy:
				case BgOpcode.ListSelect:
				case BgOpcode.ListWhere:
					Disassemble(frame, logger);
					TraceFragment(frame, logger);
					break;
				case BgOpcode.ListCount:
				case BgOpcode.ListDistinct:
					Disassemble(frame, logger);
					break;
				case BgOpcode.ListElement:
				case BgOpcode.ListConcat:
				case BgOpcode.ListUnion:
				case BgOpcode.ListExcept:
				case BgOpcode.ListContains:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.ListLazy:
					TraceFragment(frame, logger);
					break;
				case BgOpcode.ListOption:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Trace(frame, "style", ReadUnsignedInteger, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;

				#endregion
				#region Fileset opcodes

				case BgOpcode.FileSetFromNode:
					Disassemble(frame, logger);
					break;
				case BgOpcode.FileSetFromNodeOutput:
					Disassemble(frame, logger);
					TraceFragment(frame, logger);
					break;

				#endregion
				#region Object opcodes

				case BgOpcode.ObjEmpty:
					break;
				case BgOpcode.ObjGet:
				case BgOpcode.ObjSet:
					Disassemble(frame, logger);
					Trace(frame, "name", ReadString, logger);
					Disassemble(frame, logger);
					break;

				#endregion
				#region Function opcodes

				case BgOpcode.Call:
					{
						int count = (int)Trace(frame, "count", ReadUnsignedInteger, logger);
						for (int idx = 0; idx < count; idx++)
						{
							Disassemble(frame, logger);
						}

						TraceFragment(frame, logger);
						break;
					}
				case BgOpcode.Argument:
					Trace(frame, "arg", ReadUnsignedInteger, logger);
					break;
				case BgOpcode.Jump:
					TraceFragment(frame, logger);
					break;

				#endregion
				#region Generic opcodes

				case BgOpcode.Choose:
					Disassemble(frame, logger);
					TraceFragment(frame, logger);
					TraceFragment(frame, logger);
					break;
				case BgOpcode.Throw:
					Trace(frame, "file", ReadString, logger);
					Trace(frame, "line", ReadUnsignedInteger, logger);
					Trace(frame, "message", ReadString, logger);
					break;
				case BgOpcode.Null:
					break;

				#endregion
				#region Graph opcodes

				case BgOpcode.Node:
					{
						Disassemble(frame, logger);
						Disassemble(frame, logger);
						Trace(frame, "handler", ReadUnsignedInteger, logger);

						int argCount = (int)Trace(frame, "args", ReadUnsignedInteger, logger);
						for (int idx = 0; idx < argCount; idx++)
						{
							Disassemble(frame, logger);
						}

						Disassemble(frame, logger);
						Disassemble(frame, logger);
						Disassemble(frame, logger);
						Disassemble(frame, logger);
						break;
					}
				case BgOpcode.Agent:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.Aggregate:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.Label:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				case BgOpcode.Graph:
					Disassemble(frame, logger);
					Disassemble(frame, logger);
					break;
				#endregion

				default:
					throw new InvalidDataException($"Invalid opcode: {opcode}");
			}
		}

		int TraceFragment(Frame frame, ILogger logger)
		{
			return Trace(frame, "-> Fragment", ReadFragment, logger);
		}

		T Trace<T>(Frame frame, string? type, Func<Frame, T> readValue, ILogger logger)
		{
			int offset = frame.Offset;
			T value = readValue(frame);
			int length = frame.Offset - offset;

			string valueAndType = (type == null) ? $"{value}" : $"{type} {value}";
			Trace<T>(offset, length, valueAndType, logger);
			return value;
		}

		T Trace<T>(Frame frame, Func<Frame, T> readValue, Func<T, object> formatValue, ILogger logger)
		{
			int offset = frame.Offset;
			T value = readValue(frame);
			int length = frame.Offset - offset;

			Trace<T>(offset, length, formatValue(value), logger);
			return value;
		}

		void Trace<T>(int offset, int length, object value, ILogger logger)
		{
			string bytes = String.Join(" ", _data.AsSpan(offset, length).ToArray().Select(x => $"{x:x2}"));
			logger.LogInformation("{Offset,6}: {Value,-20} {Bytes}", offset, value, bytes);
		}
	}
}
