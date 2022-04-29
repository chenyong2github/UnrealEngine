// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Horde.Build.Server;
using Horde.Build.Utilities;

namespace Horde.Build.Models
{
	using WorkflowId = StringId<WorkflowConfig>;

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

		/// <summary>
		/// Whether to create issues for this node
		/// </summary>
		bool? CreateIssues { get; }
	}

	/// <summary>
	/// Set of annotations for a node
	/// </summary>
	public class NodeAnnotations : CaseInsensitiveDictionary<string>, IReadOnlyNodeAnnotations
	{
		/// <summary>
		/// Empty annotation dictionary
		/// </summary>
		public static IReadOnlyNodeAnnotations Empty { get; } = new NodeAnnotations();

		/// <inheritdoc cref="IReadOnlyNodeAnnotations.WorkflowId"/>
		public const string WorkflowKeyName = "Workflow";

		/// <inheritdoc cref="IReadOnlyNodeAnnotations.CreateIssues"/>
		public const string CreateIssuesKeyName = "CreateIssues";

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
			set => SetValue(WorkflowKeyName, value?.ToString());
		}

		/// <inheritdoc/>
		public bool? CreateIssues
		{
			get => GetBoolValue(CreateIssuesKeyName);
			set => SetBoolValue(CreateIssuesKeyName, value);
		}

		private bool? GetBoolValue(string key)
		{
			string? value = GetValue(key);
			if (value != null)
			{
				if (value.Equals("0", StringComparison.Ordinal) || value.Equals("false", StringComparison.OrdinalIgnoreCase))
				{
					return false;
				}
				if (value.Equals("1", StringComparison.Ordinal) || value.Equals("true", StringComparison.OrdinalIgnoreCase))
				{
					return true;
				}
			}
			return null;
		}

		private void SetBoolValue(string key, bool? value)
		{
			if (value == null)
			{
				Remove(key);
			}
			else
			{
				SetValue(key, value.Value ? "1" : "0");
			}
		}

		private string? GetValue(string key)
		{
			TryGetValue(key, out string? value);
			return value;
		}

		private void SetValue(string key, string? value)
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
}
