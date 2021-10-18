// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a particular compile error
	/// </summary>
	class DefaultIssueHandler : IIssueHandler
	{
		/// <summary>
		/// Name of the handler
		/// </summary>
		public const string Type = "Default";

		/// <inheritdoc/>
		string IIssueHandler.Type => Type;

		/// <inheritdoc/>
		public int Priority => 0;

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob Job, INode Node, ILogEventData EventData, [NotNullWhen(true)] out NewIssueFingerprint? Fingerprint)
		{
			Fingerprint = new NewIssueFingerprint(Type, new[] { Node.Name }, null);
			return true;
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint Fingerprint, List<SuspectChange> Suspects)
		{
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint Fingerprint, IssueSeverity Severity)
		{
			string NodeName = Fingerprint.Keys.FirstOrDefault() ?? "(unknown)";
			if(Severity == IssueSeverity.Warning)
			{
				return $"Warnings in {NodeName}";
			}
			else
			{
				return $"Errors in {NodeName}";
			}
		}
	}
}
