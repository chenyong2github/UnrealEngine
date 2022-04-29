// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using Horde.Build.Server;
using Horde.Build.Utilities;

namespace Horde.Build.Models
{
	using WorkflowId = StringId<WorkflowConfig>;

	/// <summary>
	/// Set of annotations for a node
	/// </summary>
	public class NodeAnnotations : CaseInsensitiveDictionary<string>, IReadOnlyNodeAnnotations
	{
		/// <summary>
		/// Empty annotation dictionary
		/// </summary>
		public static IReadOnlyNodeAnnotations Empty { get; } = new NodeAnnotations();

		/// <summary>
		/// The issue workflow to use, as defined in the stream configuration file
		/// </summary>
		public const string WorkflowKeyName = "Workflow";

		/// <summary>
		/// Constructor
		/// </summary>
		public NodeAnnotations()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="annotations"></param>
		public NodeAnnotations(IReadOnlyDictionary<string, string> annotations)
		{
			Merge(annotations);
		}

		/// <inheritdoc/>
		public WorkflowId? WorkflowId
		{
			get
			{
				string? workflowName;
				if (!TryGetValue(WorkflowKeyName, out workflowName))
				{
					return null;
				}
				return new WorkflowId(workflowName);
			}
			set => SetOrUnset(WorkflowKeyName, value?.ToString());
		}

		private void SetOrUnset(string key, string? value)
		{
			if (value == null)
			{
				Remove(key);
			}
			else
			{
				this[key] = value;
			}
		}

		/// <summary>
		/// Merge in entries from another set of annotation
		/// </summary>
		/// <param name="other"></param>
		public void Merge(IReadOnlyDictionary<string, string> other)
		{
			foreach ((string key, string value) in other)
			{
				this[key] = value;
			}
		}
	}

	/// <summary>
	/// Interface which wraps a generic key/value dictionary to provide specific node annotations
	/// </summary>
#pragma warning disable CA1710 // Identifiers should have correct suffix
	public interface IReadOnlyNodeAnnotations : IReadOnlyDictionary<string, string>
#pragma warning restore CA1710 // Identifiers should have correct suffix
	{
		/// <summary>
		/// Workflow to use for triaging issues from this node
		/// </summary>
		WorkflowId? WorkflowId { get; }
	}
}
