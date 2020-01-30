// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent
{
	abstract class ProgramMode
	{
		public virtual void Configure(CommandLineArguments Arguments)
		{
			Arguments.ApplyTo(this);
		}

		public virtual List<KeyValuePair<string, string>> GetParameters(CommandLineArguments Arguments)
		{
			return CommandLineArguments.GetParameters(GetType());
		}

		public abstract int Execute();
	}
}
