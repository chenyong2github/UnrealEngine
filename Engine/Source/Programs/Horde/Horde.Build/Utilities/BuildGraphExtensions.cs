// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.BuildGraph;
using EpicGames.Perforce;
using Horde.Build.Collections;
using Horde.Build.Models;

namespace Horde.Build.Utilities
{
	/// <summary>
	/// Extension methods to construct graphs from BuildGraph scripts in Perforce
	/// </summary>
	static class BuildGraphExtensions
	{
		class PerforceScriptContext : IBgScriptReaderContext
		{
			readonly IPerforceConnection _perforceConnection;
			readonly string _stream;
			readonly int _changeNumber;

			public PerforceScriptContext(IPerforceConnection perforce, string stream, int changeNumber)
			{
				_perforceConnection = perforce;
				_stream = stream;
				_changeNumber = changeNumber;
			}

			public async Task<bool> ExistsAsync(string path)
			{
				List<FStatRecord> records = await _perforceConnection.FStatAsync($"{_stream}/{path}@{_changeNumber}").ToListAsync();
				return records.Count > 0;
			}

			public object GetNativePath(string path)
			{
				return $"{_stream}/{path}";
			}

			public async Task<byte[]?> ReadAsync(string path)
			{
				string tempFileName = System.IO.Path.GetTempFileName();
				try
				{
					await _perforceConnection.PrintAsync(tempFileName, $"{_stream}/{path}");
					return await File.ReadAllBytesAsync(tempFileName);
				}
				catch
				{
					return null;
				}
				finally
				{
					try
					{
						File.SetAttributes(tempFileName, FileAttributes.Normal);
						File.Delete(tempFileName);
					}
					catch { }
				}
			}

			public Task<string[]> FindAsync(string path)
			{
				throw new NotImplementedException();
			}
		}

		/// <summary>
		/// Create a graph from a script in Perforce
		/// </summary>
		/// <param name="graphCollection"></param>
		/// <param name="perforceConnection"></param>
		/// <param name="stream"></param>
		/// <param name="changeNumber"></param>
		/// <param name="codeChangeNumber"></param>
		/// <param name="arguments"></param>
		/// <returns></returns>
		/// <exception cref="NotImplementedException"></exception>
		public static async Task<IGraph> AddAsync(this IGraphCollection graphCollection, IPerforceConnection perforceConnection, IStream stream, int changeNumber, int codeChangeNumber, List<string> arguments)
		{
			PerforceScriptContext context = new PerforceScriptContext(perforceConnection, stream.Name, changeNumber);

			BgScriptSchema? schema = null!;

			Dictionary<string, string> properties = new Dictionary<string, string>();
			properties["HostPlatform"] = "";
			properties["RootDir"] = "";
			properties["IsBuildMachine"] = "true";
			properties["Branch"] = stream.Name;
			properties["Change"] = changeNumber.ToString(CultureInfo.InvariantCulture);
			properties["EscapedBranch"] = stream.Name.Replace('/', '+');
			properties["CodeChange"] = codeChangeNumber.ToString(CultureInfo.InvariantCulture);

			string? scriptFile = null;
			foreach (string argument in arguments)
			{
				const string ScriptPrefix = "-Script=";
				if (argument.StartsWith(ScriptPrefix, StringComparison.OrdinalIgnoreCase))
				{
					scriptFile = argument.Substring(ScriptPrefix.Length);
				}
			}

			List<string> targets = new List<string>();
			foreach (string argument in arguments)
			{
				const string TargetPrefix = "-Target=";
				if (argument.StartsWith(TargetPrefix, StringComparison.OrdinalIgnoreCase))
				{
					targets.AddRange(argument.Substring(TargetPrefix.Length).Split(';'));
				}
			}

			Dictionary<string, string> options = new Dictionary<string, string>();
			foreach (string argument in arguments)
			{
				const string SetPrefix = "-Set:";
				if (argument.StartsWith(SetPrefix, StringComparison.OrdinalIgnoreCase))
				{
					int equalsIdx = argument.IndexOf('=', StringComparison.Ordinal);
					if (equalsIdx != -1)
					{
						options[argument.Substring(SetPrefix.Length, equalsIdx - SetPrefix.Length)] = argument.Substring(equalsIdx + 1);
					}
				}
			}

			if(scriptFile == null)
			{
				throw new NotImplementedException();
			}

			BgGraph? script = await BgScriptReader.ReadAsync(context, scriptFile, options, properties, schema, EpicGames.Core.Log.Logger);
			if (script == null)
			{
				throw new NotImplementedException();
			}

			HashSet<BgNode> targetNodes = new HashSet<BgNode>();
			foreach (string target in targets)
			{
				if (script.NameToNode.TryGetValue(target, out BgNode? node))
				{
					targetNodes.Add(node);
				}
				else if (script.NameToAgent.TryGetValue(target, out BgAgent? agent))
				{
					targetNodes.UnionWith(agent.Nodes);
				}
				else if (script.NameToAggregate.TryGetValue(target, out BgAggregate? aggregate))
				{
					targetNodes.UnionWith(aggregate.RequiredNodes);
				}
				else
				{
					throw new NotImplementedException();
				}
			}
			script.Select(targetNodes);

			List<NewGroup> newGroups = new List<NewGroup>();
			foreach (BgAgent agent in script.Agents)
			{
				List<NewNode> newNodes = new List<NewNode>();
				foreach (BgNode node in agent.Nodes)
				{
					const bool allowRetry = true;
					NewNode newNode = new NewNode(node.Name, node.InputDependencies.Select(x => x.Name).ToList(), node.OrderDependencies.Select(x => x.Name).ToList(), HordeCommon.Priority.Normal, allowRetry, node.RunEarly, node.NotifyOnWarnings, new Dictionary<string, string>(), new Dictionary<string, string>());
					newNodes.Add(newNode);
				}

				string agentType = agent.PossibleTypes.FirstOrDefault(x => stream.AgentTypes.ContainsKey(x)) ?? agent.PossibleTypes.FirstOrDefault() ?? "Unknown";
				newGroups.Add(new NewGroup(agentType, newNodes));
			}

			List<NewAggregate> newAggregates = new List<NewAggregate>();
			foreach (BgAggregate aggregate in script.NameToAggregate.Values)
			{
				NewAggregate newAggregate = new NewAggregate(aggregate.Name, aggregate.RequiredNodes.Select(x => x.Name).ToList());
				newAggregates.Add(newAggregate);
			}

			List<NewLabel> newLabels = new List<NewLabel>();
			foreach (BgLabel label in script.Labels)
			{
				NewLabel newLabel = new NewLabel();
				newLabel.DashboardName = String.IsNullOrEmpty(label.DashboardName) ? null : label.DashboardName;
				newLabel.DashboardCategory = String.IsNullOrEmpty(label.DashboardCategory) ? null : label.DashboardCategory;
				newLabel.UgsName = String.IsNullOrEmpty(label.UgsBadge) ? null : label.UgsBadge;
				newLabel.UgsProject = String.IsNullOrEmpty(label.UgsProject) ? null : label.UgsProject;
				newLabel.Change = (label.Change == BgLabelChange.Code) ? HordeCommon.LabelChange.Code : HordeCommon.LabelChange.Current;
				newLabel.RequiredNodes = label.RequiredNodes.Select(x => x.Name).ToList();
				newLabel.IncludedNodes = label.IncludedNodes.Select(x => x.Name).ToList();
				newLabels.Add(newLabel);
			}

			return await graphCollection.AppendAsync(null, newGroups, newAggregates, newLabels);
		}
	}
}
