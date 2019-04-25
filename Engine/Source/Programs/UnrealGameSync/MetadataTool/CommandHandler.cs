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
		public abstract void Exec(CommandLineArguments Arguments);
		public abstract void Help();
	}
}
