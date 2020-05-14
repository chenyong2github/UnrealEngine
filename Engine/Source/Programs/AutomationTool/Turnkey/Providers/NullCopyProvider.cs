using System.IO;
using System.Collections.Generic;
using System.Linq;

namespace Turnkey
{
	class NullCopyProvider : CopyProvider
	{
		public override string ProviderToken { get { return "file"; } }

		public override string Execute(string Operation, CopyExecuteSpecialMode SpecialMode, string SpecialModeHint)
		{
			Operation = Operation.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

			// this provider can use the file directly, so just return the input after expanding variables, if it exists
			string OutputPath = TurnkeyUtils.ExpandVariables(Operation);

			int WildcardLocation = OutputPath.IndexOf('*');
			if (WildcardLocation >= 0)
			{
				// chop down to the last / before the wildcard
				int LastSlashLocation = OutputPath.Substring(0, WildcardLocation).LastIndexOf(Path.DirectorySeparatorChar);
				OutputPath = OutputPath.Substring(0, LastSlashLocation);
			}

			if (File.Exists(OutputPath) == false && Directory.Exists(OutputPath) == false)
			{
				TurnkeyUtils.Log("Reqeusted local path {0} does not exist!", OutputPath);
				return null;
			}
			
			return OutputPath;
		}

		private void ExpandWildcards(string Prefix, string PathString, Dictionary<string, List<string>> Output, List<string> ExpansionSet)
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
					Output.Add(ProviderToken + ":" + PathString.TrimEnd("/\\".ToCharArray()) + Slash, ExpansionSet);
				}
				else if (File.Exists(PathString))
				{
					Output.Add(ProviderToken + ":" + PathString, ExpansionSet);
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
				// get the path component that has a wildcard
				string Wildcard = (NextSlash == -1) ? PathString.Substring(PrevSlash + 1) : PathString.Substring(PrevSlash + 1, (NextSlash - PrevSlash) - 1);

				// track what's before and after the * to return what it expanded to
				int StarLoc = Wildcard.IndexOf('*');
				int PrefixLen = StarLoc;
				int SuffixLen = Wildcard.Length - (StarLoc + 1);
				foreach (string Dirname in Directory.EnumerateDirectories(FullPathBeforeWildcard, Wildcard))
				{
					List<string> NewExpansionSet = null;
					if (ExpansionSet != null)
					{
						NewExpansionSet = new List<string>(ExpansionSet);
						string PathComponent = Path.GetFileName(Dirname);
						// the match is the part of the filename that was not the *, so removing that will give us what we wanted to match
						NewExpansionSet.Add(PathComponent.Substring(PrefixLen, PathComponent.Length - (PrefixLen + SuffixLen)));
					}

					if (bIsLastComponent)
					{
						// indicate directories with a slash at the end
						Output.Add(ProviderToken + ":" + Dirname + Slash, NewExpansionSet);
					}
					// recurse
					else
					{
						ExpandWildcards(Dirname + Slash, PathString.Substring(NextSlash + 1), Output, NewExpansionSet);
					}
				}

				// if the path ends with a slash, then we are only looking for directories (D:\\Sdks\*\ would only want to return directories)
				if (bIsLastComponent && NextSlash == -1)
				{
					foreach (string Filename in Directory.EnumerateFiles(FullPathBeforeWildcard, Wildcard))
					{
						List<string> NewExpansionSet = null;
						if (ExpansionSet != null)
						{
							NewExpansionSet = new List<string>(ExpansionSet);
							string PathComponent = Path.GetFileName(Filename);
							// the match is the part of the filename that was not the *, so removing that will give us what we wanted to match
							NewExpansionSet.Add(PathComponent.Substring(PrefixLen, PathComponent.Length - (PrefixLen + SuffixLen)));
						}

						Output.Add(ProviderToken + ":" + Filename, NewExpansionSet);
					}
				}
			}
		}

		public override string[] Enumerate(string Operation, List<List<string>> Expansions)
		{
			Dictionary<string, List<string>> Output = new Dictionary<string, List<string>>();

			// we want consistent slashes
			Operation = Operation.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

			ExpandWildcards("", Operation, Output, Expansions == null ? null : new List<string>());

			if (Expansions != null)
			{
				Expansions.AddRange(Output.Values);
			}
			return Output.Keys.ToArray();
		}
	}
}
