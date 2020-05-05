using System.IO;
using System.Collections.Generic;

namespace Turnkey
{
	class NullCopyProvider : CopyProvider
	{
		public override string ProviderToken { get { return "file"; } }

		public override string Execute(string Operation, CopyExecuteSpecialMode SpecialMode, string SpecialModeHint)
		{
			Operation = Operation.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

			// this provider can use the file directly, so just return the input after expanding variables, if it exists
			string FinalPath = TurnkeyUtils.ExpandVariables(Operation);

			if (File.Exists(FinalPath) == false && Directory.Exists(FinalPath) == false)
			{
				TurnkeyUtils.Log("Reqeusted local path {0} does not exist!", FinalPath);
				return null;
			}

			// @todo turnkey : if wildcard, do like p4 does
			
			return FinalPath ;
		}

		private void ExpandWildcards(string Prefix, string PathString, List<string> Output)
		{
			char Slash = Path.DirectorySeparatorChar;

			// look through for *'s
			int StarLocation = PathString.IndexOf('*');

			// if this has no wildcard, it's a set file, just use it directly
			if (StarLocation == -1)
			{
				PathString = Prefix + PathString;
				if (Directory.Exists(PathString))
				{
					// make sure we end with a single slash
					Output.Add(ProviderToken + ":" + PathString.TrimEnd("/\\".ToCharArray()) + Slash);
				}
				else if (File.Exists(PathString))
				{
					Output.Add(ProviderToken + ":" + PathString);
				}
				return;
			}

			// now go backwards looking for a Slash
			int PrevSlash = PathString.LastIndexOf(Slash, StarLocation);

			// current wildcard is the path segment up to next slash or the end
			int NextSlash = PathString.IndexOf(Slash, StarLocation);

			// if this are no more slashes, then this is the final expansion, and we can add to the result, and look for files
			bool bIsLastComponent = NextSlash == -1 || NextSlash == PathString.Length - 1;

			// get the wildcard path component
			string FullPathBeforeWildcard = Prefix + (PrevSlash >= 0 ? PathString.Substring(0, PrevSlash) : "");

			if (Directory.Exists(FullPathBeforeWildcard))
			{
				string Wildcard = (NextSlash == -1) ? PathString.Substring(PrevSlash + 1) : PathString.Substring(PrevSlash + 1, (NextSlash - PrevSlash) - 1);
				foreach (string Dirname in Directory.EnumerateDirectories(FullPathBeforeWildcard, Wildcard))
				{
					if (bIsLastComponent)
					{
						// indicate directories with a slash at the end
						Output.Add(ProviderToken + ":" + Dirname + Slash);
					}
					// recurse
					else
					{
						ExpandWildcards(Dirname + Slash, PathString.Substring(NextSlash + 1), Output);
					}
				}

				// if the path ends with a slash, then we are only looking for directories (D:\\Sdks\*\ would only want to return directories)
				if (bIsLastComponent && NextSlash == -1)
				{
					foreach (string Filename in Directory.EnumerateFiles(FullPathBeforeWildcard, Wildcard))
					{
						Output.Add(ProviderToken + ":" + Filename);
					}
				}
			}
		}

		public override string[] Enumerate(string Operation)
		{
			List<string> Output = new List<string>();

			// we want consistent slashes
			Operation = Operation.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

			ExpandWildcards("", Operation, Output);

			return Output.ToArray();
		}
	}
}
