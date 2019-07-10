// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MetadataTool
{
	[AttributeUsage(AttributeTargets.Field)]
	class OptionalAttribute : Attribute
	{
	}

	static class CommandTypes
	{
		public const int MaxIssueDetailsLength = 8192;

		public enum Outcome
		{
			Success = 1,
			Error = 2,
			Warning = 3,
		}

		public class AddIssue
		{
			public string Project;
			public string Summary;
			[Optional] public string Owner;
		}

		public class AddIssueResponse
		{
			public long Id;
		}

		public class UpdateIssue
		{
			[Optional] public bool? Acknowledged;
			[Optional] public int? FixChange;
			[Optional] public string Owner;
			[Optional] public string NominatedBy;
			[Optional] public bool? Resolved;
			[Optional] public string Summary;
			[Optional] public string Url;
		}

		public class AddBuild
		{
			public string Stream;
			public int Change;
			public string JobName;
			public string JobUrl;
			public string JobStepName;
			public string JobStepUrl;
			[Optional] public string ErrorUrl;
			public Outcome Outcome;
		}

		public class AddBuildResponse
		{
			public long Id;
		}

		public class UpdateBuild
		{
			[Optional] public int? Outcome;
		}

		public class AddDiagnostic
		{
			[Optional] public long? BuildId;
			public string Message;
			public string Url;
		}

		public class AddDiagnosticResponse
		{
			public long Id;
		}

		public class Watcher
		{
			public string UserName;
		}
	}
}
