// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Xml;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Options for how the graph should be printed
	/// </summary>
	public enum GraphPrintOptions
	{
		/// <summary>
		/// Includes a list of the graph options
		/// </summary>
		ShowCommandLineOptions = 0x1,

		/// <summary>
		/// Includes the list of dependencies for each node
		/// </summary>
		ShowDependencies = 0x2,

		/// <summary>
		/// Includes the list of notifiers for each node
		/// </summary>
		ShowNotifications = 0x4,
	}

	/// <summary>
	/// Definition of a graph.
	/// </summary>
	public class BgGraph
	{
		/// <summary>
		/// List of options, in the order they were specified
		/// </summary>
		public List<BgOption> Options { get; } = new List<BgOption>();

		/// <summary>
		/// List of agents containing nodes to execute
		/// </summary>
		public List<BgAgent> Agents { get; } = new List<BgAgent>();

		/// <summary>
		/// Mapping from name to agent
		/// </summary>
		public Dictionary<string, BgAgent> NameToAgent { get; set; } = new Dictionary<string, BgAgent>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Mapping of names to the corresponding node.
		/// </summary>
		public Dictionary<string, BgNode> NameToNode { get; set; } = new Dictionary<string, BgNode>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Mapping of names to the corresponding report.
		/// </summary>
		public Dictionary<string, BgReport> NameToReport { get; private set; } = new Dictionary<string, BgReport>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Mapping of names to their corresponding node output.
		/// </summary>
		public Dictionary<string, BgNodeOutput> TagNameToNodeOutput { get; private set; } = new Dictionary<string, BgNodeOutput>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Mapping of aggregate names to their respective nodes
		/// </summary>
		public Dictionary<string, BgAggregate> NameToAggregate { get; private set; } = new Dictionary<string, BgAggregate>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// List of badges that can be displayed for this build
		/// </summary>
		public List<BgBadge> Badges { get; } = new List<BgBadge>();

		/// <summary>
		/// List of labels that can be displayed for this build
		/// </summary>
		public List<BgLabel> Labels { get; } = new List<BgLabel>();

		/// <summary>
		/// Diagnostics at graph scope
		/// </summary>
		public List<BgDiagnostic> Diagnostics { get; } = new List<BgDiagnostic>();

		/// <summary>
		/// Default constructor
		/// </summary>
		public BgGraph()
		{
		}

		/// <summary>
		/// Checks whether a given name already exists
		/// </summary>
		/// <param name="name">The name to check.</param>
		/// <returns>True if the name exists, false otherwise.</returns>
		public bool ContainsName(string name)
		{
			return NameToNode.ContainsKey(name) || NameToReport.ContainsKey(name) || NameToAggregate.ContainsKey(name);
		}

		/// <summary>
		/// Gets diagnostics from all graph structures
		/// </summary>
		/// <returns>List of diagnostics</returns>
		public List<BgDiagnostic> GetAllDiagnostics()
		{
			List<BgDiagnostic> diagnostics = new List<BgDiagnostic>(Diagnostics);
			foreach (BgAgent agent in Agents)
			{
				diagnostics.AddRange(agent.Diagnostics);
				foreach (BgNode node in agent.Nodes)
				{
					diagnostics.AddRange(node.Diagnostics);
				}
			}
			return diagnostics;
		}

		/// <summary>
		/// Tries to resolve the given name to one or more nodes. Checks for aggregates, and actual nodes.
		/// </summary>
		/// <param name="name">The name to search for</param>
		/// <param name="outNodes">If the name is a match, receives an array of nodes and their output names</param>
		/// <returns>True if the name was found, false otherwise.</returns>
		public bool TryResolveReference(string name, [NotNullWhen(true)] out BgNode[]? outNodes)
		{
			// Check if it's a tag reference or node reference
			if (name.StartsWith("#"))
			{
				// Check if it's a regular node or output name
				BgNodeOutput? output;
				if (TagNameToNodeOutput.TryGetValue(name, out output))
				{
					outNodes = new BgNode[] { output.ProducingNode };
					return true;
				}
			}
			else
			{
				// Check if it's a regular node or output name
				BgNode? node;
				if (NameToNode.TryGetValue(name, out node))
				{
					outNodes = new BgNode[] { node };
					return true;
				}

				// Check if it's an aggregate name
				BgAggregate? aggregate;
				if (NameToAggregate.TryGetValue(name, out aggregate))
				{
					outNodes = aggregate.RequiredNodes.ToArray();
					return true;
				}

				// Check if it's a group name
				BgAgent? agent;
				if (NameToAgent.TryGetValue(name, out agent))
				{
					outNodes = agent.Nodes.ToArray();
					return true;
				}
			}

			// Otherwise fail
			outNodes = null;
			return false;
		}

		/// <summary>
		/// Tries to resolve the given name to one or more node outputs. Checks for aggregates, and actual nodes.
		/// </summary>
		/// <param name="name">The name to search for</param>
		/// <param name="outOutputs">If the name is a match, receives an array of nodes and their output names</param>
		/// <returns>True if the name was found, false otherwise.</returns>
		public bool TryResolveInputReference(string name, [NotNullWhen(true)] out BgNodeOutput[]? outOutputs)
		{
			// Check if it's a tag reference or node reference
			if (name.StartsWith("#"))
			{
				// Check if it's a regular node or output name
				BgNodeOutput? output;
				if (TagNameToNodeOutput.TryGetValue(name, out output))
				{
					outOutputs = new BgNodeOutput[] { output };
					return true;
				}
			}
			else
			{
				// Check if it's a regular node or output name
				BgNode? node;
				if (NameToNode.TryGetValue(name, out node))
				{
					outOutputs = node.Outputs.Union(node.Inputs).ToArray();
					return true;
				}

				// Check if it's an aggregate name
				BgAggregate? aggregate;
				if (NameToAggregate.TryGetValue(name, out aggregate))
				{
					outOutputs = aggregate.RequiredNodes.SelectMany(x => x.Outputs.Union(x.Inputs)).Distinct().ToArray();
					return true;
				}
			}

			// Otherwise fail
			outOutputs = null;
			return false;
		}

		static void AddDependencies(BgNode node, HashSet<BgNode> retainNodes)
		{
			if (retainNodes.Add(node))
			{
				foreach (BgNode inputDependency in node.InputDependencies)
				{
					AddDependencies(inputDependency, retainNodes);
				}
			}
		}

		/// <summary>
		/// Cull the graph to only include the given nodes and their dependencies
		/// </summary>
		/// <param name="targetNodes">A set of target nodes to build</param>
		public void Select(IEnumerable<BgNode> targetNodes)
		{
			// Find this node and all its dependencies
			HashSet<BgNode> retainNodes = new HashSet<BgNode>();
			foreach (BgNode targetNode in targetNodes)
			{
				AddDependencies(targetNode, retainNodes);
			}

			// Remove all the nodes which are not marked to be kept
			foreach (BgAgent agent in Agents)
			{
				agent.Nodes = agent.Nodes.Where(x => retainNodes.Contains(x)).ToList();
			}

			// Remove all the empty agents
			Agents.RemoveAll(x => x.Nodes.Count == 0);

			// Trim down the list of nodes for each report to the ones that are being built
			foreach (BgReport report in NameToReport.Values)
			{
				report.Nodes.RemoveWhere(x => !retainNodes.Contains(x));
			}

			// Remove all the empty reports
			NameToReport = NameToReport.Where(x => x.Value.Nodes.Count > 0).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.InvariantCultureIgnoreCase);

			// Remove all the order dependencies which are no longer part of the graph. Since we don't need to build them, we don't need to wait for them
			foreach (BgNode node in retainNodes)
			{
				node.OrderDependencies = node.OrderDependencies.Where(x => retainNodes.Contains(x)).ToArray();
			}

			// Create a new list of aggregates for everything that's left
			Dictionary<string, BgAggregate> newNameToAggregate = new Dictionary<string, BgAggregate>(NameToAggregate.Comparer);
			foreach (BgAggregate aggregate in NameToAggregate.Values)
			{
				if (aggregate.RequiredNodes.All(x => retainNodes.Contains(x)))
				{
					newNameToAggregate[aggregate.Name] = aggregate;
				}
			}
			NameToAggregate = newNameToAggregate;

			// Remove any labels that are no longer value
			foreach (BgLabel label in Labels)
			{
				label.RequiredNodes.RemoveWhere(x => !retainNodes.Contains(x));
				label.IncludedNodes.RemoveWhere(x => !retainNodes.Contains(x));
			}
			Labels.RemoveAll(x => x.RequiredNodes.Count == 0);

			// Remove any badges which do not have all their dependencies
			Badges.RemoveAll(x => x.Nodes.Any(y => !retainNodes.Contains(y)));
		}

		/// <summary>
		/// Writes a preprocessed build graph to a script file
		/// </summary>
		/// <param name="file">The file to load</param>
		/// <param name="schemaFile">Schema file for validation</param>
		public void Write(FileReference file, FileReference schemaFile)
		{
			XmlWriterSettings settings = new XmlWriterSettings();
			settings.Indent = true;
			settings.IndentChars = "\t";

			using (XmlWriter writer = XmlWriter.Create(file.FullName, settings))
			{
				writer.WriteStartElement("BuildGraph", "http://www.epicgames.com/BuildGraph");

				if (schemaFile != null)
				{
					writer.WriteAttributeString("schemaLocation", "http://www.w3.org/2001/XMLSchema-instance", "http://www.epicgames.com/BuildGraph " + schemaFile.MakeRelativeTo(file.Directory));
				}

				foreach (BgAgent agent in Agents)
				{
					agent.Write(writer);
				}

				foreach (BgAggregate aggregate in NameToAggregate.Values)
				{
					// If the aggregate has no required elements, skip it.
					if (aggregate.RequiredNodes.Count == 0)
					{
						continue;
					}

					writer.WriteStartElement("Aggregate");
					writer.WriteAttributeString("Name", aggregate.Name);
					writer.WriteAttributeString("Requires", String.Join(";", aggregate.RequiredNodes.Select(x => x.Name)));
					writer.WriteEndElement();
				}

				foreach (BgLabel label in Labels)
				{
					writer.WriteStartElement("Label");
					if (label.DashboardCategory != null)
					{
						writer.WriteAttributeString("Category", label.DashboardCategory);
					}
					writer.WriteAttributeString("Name", label.DashboardName);
					writer.WriteAttributeString("Requires", String.Join(";", label.RequiredNodes.Select(x => x.Name)));

					HashSet<BgNode> includedNodes = new HashSet<BgNode>(label.IncludedNodes);
					includedNodes.ExceptWith(label.IncludedNodes.SelectMany(x => x.InputDependencies));
					includedNodes.ExceptWith(label.RequiredNodes);
					if (includedNodes.Count > 0)
					{
						writer.WriteAttributeString("Include", String.Join(";", includedNodes.Select(x => x.Name)));
					}

					HashSet<BgNode> excludedNodes = new HashSet<BgNode>(label.IncludedNodes);
					excludedNodes.UnionWith(label.IncludedNodes.SelectMany(x => x.InputDependencies));
					excludedNodes.ExceptWith(label.IncludedNodes);
					excludedNodes.ExceptWith(excludedNodes.ToArray().SelectMany(x => x.InputDependencies));
					if (excludedNodes.Count > 0)
					{
						writer.WriteAttributeString("Exclude", String.Join(";", excludedNodes.Select(x => x.Name)));
					}
					writer.WriteEndElement();
				}

				foreach (BgReport report in NameToReport.Values)
				{
					writer.WriteStartElement("Report");
					writer.WriteAttributeString("Name", report.Name);
					writer.WriteAttributeString("Requires", String.Join(";", report.Nodes.Select(x => x.Name)));
					writer.WriteEndElement();
				}

				foreach (BgBadge badge in Badges)
				{
					writer.WriteStartElement("Badge");
					writer.WriteAttributeString("Name", badge.Name);
					if (badge.Project != null)
					{
						writer.WriteAttributeString("Project", badge.Project);
					}
					if (badge.Change != 0)
					{
						writer.WriteAttributeString("Change", badge.Change.ToString());
					}
					writer.WriteAttributeString("Requires", String.Join(";", badge.Nodes.Select(x => x.Name)));
					writer.WriteEndElement();
				}

				writer.WriteEndElement();
			}
		}

		/// <summary>
		/// Export the build graph to a Json file, for parallel execution by the build system
		/// </summary>
		/// <param name="file">Output file to write</param>
		/// <param name="completedNodes">Set of nodes which have been completed</param>
		public void Export(FileReference file, HashSet<BgNode> completedNodes)
		{
			// Find all the nodes which we're actually going to execute. We'll use this to filter the graph.
			HashSet<BgNode> nodesToExecute = new HashSet<BgNode>();
			foreach (BgNode node in Agents.SelectMany(x => x.Nodes))
			{
				if (!completedNodes.Contains(node))
				{
					nodesToExecute.Add(node);
				}
			}

			// Open the output file
			using (JsonWriter jsonWriter = new JsonWriter(file.FullName))
			{
				jsonWriter.WriteObjectStart();

				// Write all the agents
				jsonWriter.WriteArrayStart("Groups");
				foreach (BgAgent agent in Agents)
				{
					BgNode[] nodes = agent.Nodes.Where(x => nodesToExecute.Contains(x)).ToArray();
					if (nodes.Length > 0)
					{
						jsonWriter.WriteObjectStart();
						jsonWriter.WriteValue("Name", agent.Name);
						jsonWriter.WriteArrayStart("Agent Types");
						foreach (string agentType in agent.PossibleTypes)
						{
							jsonWriter.WriteValue(agentType);
						}
						jsonWriter.WriteArrayEnd();
						jsonWriter.WriteArrayStart("Nodes");
						foreach (BgNode node in nodes)
						{
							jsonWriter.WriteObjectStart();
							jsonWriter.WriteValue("Name", node.Name);
							jsonWriter.WriteValue("DependsOn", String.Join(";", node.GetDirectOrderDependencies().Where(x => nodesToExecute.Contains(x))));
							jsonWriter.WriteValue("RunEarly", node.RunEarly);
							jsonWriter.WriteObjectStart("Notify");
							jsonWriter.WriteValue("Default", String.Join(";", node.NotifyUsers));
							jsonWriter.WriteValue("Submitters", String.Join(";", node.NotifySubmitters));
							jsonWriter.WriteValue("Warnings", node.NotifyOnWarnings);
							jsonWriter.WriteObjectEnd();
							jsonWriter.WriteObjectEnd();
						}
						jsonWriter.WriteArrayEnd();
						jsonWriter.WriteObjectEnd();
					}
				}
				jsonWriter.WriteArrayEnd();

				// Write all the badges
				jsonWriter.WriteArrayStart("Badges");
				foreach (BgBadge badge in Badges)
				{
					BgNode[] dependencies = badge.Nodes.Where(x => nodesToExecute.Contains(x)).ToArray();
					if (dependencies.Length > 0)
					{
						// Reduce that list to the smallest subset of direct dependencies
						HashSet<BgNode> directDependencies = new HashSet<BgNode>(dependencies);
						foreach (BgNode dependency in dependencies)
						{
							directDependencies.ExceptWith(dependency.OrderDependencies);
						}

						jsonWriter.WriteObjectStart();
						jsonWriter.WriteValue("Name", badge.Name);
						if (!String.IsNullOrEmpty(badge.Project))
						{
							jsonWriter.WriteValue("Project", badge.Project);
						}
						if (badge.Change != 0)
						{
							jsonWriter.WriteValue("Change", badge.Change);
						}
						jsonWriter.WriteValue("AllDependencies", String.Join(";", Agents.SelectMany(x => x.Nodes).Where(x => dependencies.Contains(x)).Select(x => x.Name)));
						jsonWriter.WriteValue("DirectDependencies", String.Join(";", directDependencies.Select(x => x.Name)));
						jsonWriter.WriteObjectEnd();
					}
				}
				jsonWriter.WriteArrayEnd();

				// Write all the triggers and reports. 
				jsonWriter.WriteArrayStart("Reports");
				foreach (BgReport report in NameToReport.Values)
				{
					BgNode[] dependencies = report.Nodes.Where(x => nodesToExecute.Contains(x)).ToArray();
					if (dependencies.Length > 0)
					{
						// Reduce that list to the smallest subset of direct dependencies
						HashSet<BgNode> directDependencies = new HashSet<BgNode>(dependencies);
						foreach (BgNode dependency in dependencies)
						{
							directDependencies.ExceptWith(dependency.OrderDependencies);
						}

						jsonWriter.WriteObjectStart();
						jsonWriter.WriteValue("Name", report.Name);
						jsonWriter.WriteValue("AllDependencies", String.Join(";", Agents.SelectMany(x => x.Nodes).Where(x => dependencies.Contains(x)).Select(x => x.Name)));
						jsonWriter.WriteValue("DirectDependencies", String.Join(";", directDependencies.Select(x => x.Name)));
						jsonWriter.WriteValue("Notify", String.Join(";", report.NotifyUsers));
						jsonWriter.WriteValue("IsTrigger", false);
						jsonWriter.WriteObjectEnd();
					}
				}
				jsonWriter.WriteArrayEnd();

				jsonWriter.WriteObjectEnd();
			}
		}

		/// <summary>
		/// Export the build graph to a Json file for parsing by Horde
		/// </summary>
		/// <param name="file">Output file to write</param>
		public void ExportForHorde(FileReference file)
		{
			DirectoryReference.CreateDirectory(file.Directory);
			using (JsonWriter jsonWriter = new JsonWriter(file.FullName))
			{
				jsonWriter.WriteObjectStart();
				jsonWriter.WriteArrayStart("Groups");
				foreach (BgAgent agent in Agents)
				{
					jsonWriter.WriteObjectStart();
					jsonWriter.WriteArrayStart("Types");
					foreach (string possibleType in agent.PossibleTypes)
					{
						jsonWriter.WriteValue(possibleType);
					}
					jsonWriter.WriteArrayEnd();
					jsonWriter.WriteArrayStart("Nodes");
					foreach (BgNode node in agent.Nodes)
					{
						jsonWriter.WriteObjectStart();
						jsonWriter.WriteValue("Name", node.Name);
						jsonWriter.WriteValue("RunEarly", node.RunEarly);
						jsonWriter.WriteValue("Warnings", node.NotifyOnWarnings);

						jsonWriter.WriteArrayStart("InputDependencies");
						foreach (string inputDependency in node.GetDirectInputDependencies().Select(x => x.Name))
						{
							jsonWriter.WriteValue(inputDependency);
						}
						jsonWriter.WriteArrayEnd();

						jsonWriter.WriteArrayStart("OrderDependencies");
						foreach (string orderDependency in node.GetDirectOrderDependencies().Select(x => x.Name))
						{
							jsonWriter.WriteValue(orderDependency);
						}
						jsonWriter.WriteArrayEnd();

						jsonWriter.WriteObjectStart("Annotations");
						foreach ((string key, string value) in node.Annotations)
						{
							jsonWriter.WriteValue(key, value);
						}
						jsonWriter.WriteObjectEnd();

						jsonWriter.WriteObjectEnd();
					}
					jsonWriter.WriteArrayEnd();
					jsonWriter.WriteObjectEnd();
				}
				jsonWriter.WriteArrayEnd();

				jsonWriter.WriteArrayStart("Aggregates");
				foreach (BgAggregate aggregate in NameToAggregate.Values)
				{
					jsonWriter.WriteObjectStart();
					jsonWriter.WriteValue("Name", aggregate.Name);
					jsonWriter.WriteArrayStart("Nodes");
					foreach (BgNode requiredNode in aggregate.RequiredNodes.OrderBy(x => x.Name))
					{
						jsonWriter.WriteValue(requiredNode.Name);
					}
					jsonWriter.WriteArrayEnd();
					jsonWriter.WriteObjectEnd();
				}
				jsonWriter.WriteArrayEnd();

				jsonWriter.WriteArrayStart("Labels");
				foreach (BgLabel label in Labels)
				{
					jsonWriter.WriteObjectStart();
					if (!String.IsNullOrEmpty(label.DashboardName))
					{
						jsonWriter.WriteValue("Name", label.DashboardName);
					}
					if (!String.IsNullOrEmpty(label.DashboardCategory))
					{
						jsonWriter.WriteValue("Category", label.DashboardCategory);
					}
					if (!String.IsNullOrEmpty(label.UgsBadge))
					{
						jsonWriter.WriteValue("UgsBadge", label.UgsBadge);
					}
					if (!String.IsNullOrEmpty(label.UgsProject))
					{
						jsonWriter.WriteValue("UgsProject", label.UgsProject);
					}
					if (label.Change != BgLabelChange.Current)
					{
						jsonWriter.WriteValue("Change", label.Change.ToString());
					}

					jsonWriter.WriteArrayStart("RequiredNodes");
					foreach (BgNode requiredNode in label.RequiredNodes.OrderBy(x => x.Name))
					{
						jsonWriter.WriteValue(requiredNode.Name);
					}
					jsonWriter.WriteArrayEnd();
					jsonWriter.WriteArrayStart("IncludedNodes");
					foreach (BgNode includedNode in label.IncludedNodes.OrderBy(x => x.Name))
					{
						jsonWriter.WriteValue(includedNode.Name);
					}
					jsonWriter.WriteArrayEnd();
					jsonWriter.WriteObjectEnd();
				}
				jsonWriter.WriteArrayEnd();

				jsonWriter.WriteArrayStart("Badges");
				foreach (BgBadge badge in Badges)
				{
					HashSet<BgNode> dependencies = badge.Nodes;
					if (dependencies.Count > 0)
					{
						// Reduce that list to the smallest subset of direct dependencies
						HashSet<BgNode> directDependencies = new HashSet<BgNode>(dependencies);
						foreach (BgNode dependency in dependencies)
						{
							directDependencies.ExceptWith(dependency.OrderDependencies);
						}

						jsonWriter.WriteObjectStart();
						jsonWriter.WriteValue("Name", badge.Name);
						if (!String.IsNullOrEmpty(badge.Project))
						{
							jsonWriter.WriteValue("Project", badge.Project);
						}
						if (badge.Change != 0)
						{
							jsonWriter.WriteValue("Change", badge.Change);
						}
						jsonWriter.WriteValue("Dependencies", String.Join(";", directDependencies.Select(x => x.Name)));
						jsonWriter.WriteObjectEnd();
					}
				}
				jsonWriter.WriteArrayEnd();

				jsonWriter.WriteObjectEnd();
			}
		}

		/// <summary>
		/// Print the contents of the graph
		/// </summary>
		/// <param name="completedNodes">Set of nodes which are already complete</param>
		/// <param name="printOptions">Options for how to print the graph</param>
		/// <param name="logger"></param>
		public void Print(HashSet<BgNode> completedNodes, GraphPrintOptions printOptions, ILogger logger)
		{
			// Print the options
			if ((printOptions & GraphPrintOptions.ShowCommandLineOptions) != 0)
			{
				// Get the list of messages
				List<KeyValuePair<string, string>> parameters = new List<KeyValuePair<string, string>>();
				foreach (BgOption option in Options)
				{
					string name = String.Format("-set:{0}=...", option.Name);

					StringBuilder description = new StringBuilder(option.Description);
					if (!String.IsNullOrEmpty(option.DefaultValue))
					{
						description.AppendFormat(" (Default: {0})", option.DefaultValue);
					}

					parameters.Add(new KeyValuePair<string, string>(name, description.ToString()));
				}

				// Format them to the log
				if (parameters.Count > 0)
				{
					logger.LogInformation("");
					logger.LogInformation("Options:");
					logger.LogInformation("");

					List<string> lines = new List<string>();
					HelpUtils.FormatTable(parameters, 4, 24, HelpUtils.WindowWidth - 20, lines);

					foreach (string line in lines)
					{
						logger.Log(LogLevel.Information, line);
					}
				}
			}

			// Output all the triggers in order
			logger.LogInformation("");
			logger.LogInformation("Graph:");
			foreach (BgAgent agent in Agents)
			{
				logger.LogInformation("        Agent: {0} ({1})", agent.Name, String.Join(";", agent.PossibleTypes));
				foreach (BgNode node in agent.Nodes)
				{
					logger.LogInformation("            Node: {0}{1}", node.Name, completedNodes.Contains(node) ? " (completed)" : node.RunEarly ? " (early)" : "");
					if (printOptions.HasFlag(GraphPrintOptions.ShowDependencies))
					{
						HashSet<BgNode> inputDependencies = new HashSet<BgNode>(node.GetDirectInputDependencies());
						foreach (BgNode inputDependency in inputDependencies)
						{
							logger.LogInformation("                input> {0}", inputDependency.Name);
						}
						HashSet<BgNode> orderDependencies = new HashSet<BgNode>(node.GetDirectOrderDependencies());
						foreach (BgNode orderDependency in orderDependencies.Except(inputDependencies))
						{
							logger.LogInformation("                after> {0}", orderDependency.Name);
						}
					}
					if (printOptions.HasFlag(GraphPrintOptions.ShowNotifications))
					{
						string label = node.NotifyOnWarnings ? "warnings" : "errors";
						foreach (string user in node.NotifyUsers)
						{
							logger.LogInformation("                {0}> {1}", label, user);
						}
						foreach (string submitter in node.NotifySubmitters)
						{
							logger.LogInformation("                {0}> submitters to {1}", label, submitter);
						}
					}
				}
			}
			logger.LogInformation("");

			// Print out all the non-empty aggregates
			BgAggregate[] aggregates = NameToAggregate.Values.OrderBy(x => x.Name).ToArray();
			if (aggregates.Length > 0)
			{
				logger.LogInformation("Aggregates:");
				foreach (string aggregateName in aggregates.Select(x => x.Name))
				{
					logger.LogInformation("    {0}", aggregateName);
				}
				logger.LogInformation("");
			}
		}
	}
}
