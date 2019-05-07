// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace MetadataTool
{
	abstract class CommandHandler
	{
		public readonly string Name;

		public CommandHandler(string Name)
		{
			this.Name = Name;
		}

		public abstract void Exec(CommandLineArguments Arguments);
		public abstract void Help();
	}
}
