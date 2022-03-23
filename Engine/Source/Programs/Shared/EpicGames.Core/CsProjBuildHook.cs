using System;
using System.Collections.Generic;

namespace EpicGames.Core
{

	/// <summary>
	/// This interface exists to allow the CsProjBuilder in EpicGames.MsBuild to call back out
	/// into EpicGames.Build.  It is EXTREMELY important that any type definitions that must be 
	/// referenced or implemented in EpicGames.Build for use by EpicGame.MsBuild *NOT* be defined
	/// in EpicGames.MsBuild.  If they are, there is a strong chance of running into an issue 
	/// gathering types (Assembly.GetTypes()) on EpicGames.Build.  
	/// </summary>
	public interface CsProjBuildHook
	{

		/// <summary>
		/// Test the cache for a given file to get the last write time of the given file 
		/// </summary>
		/// <param name="BasePath">Base path of the file.</param>
		/// <param name="RelativeFilePath">Relative path of the file</param>
		/// <returns>Last write time of the file.</returns>
		DateTime GetLastWriteTime(DirectoryReference BasePath, string RelativeFilePath);

		/// <summary>
		/// Test the cache for a given file to get the last write time of the given file 
		/// </summary>
		/// <param name="BasePath">Base path of the file.</param>
		/// <param name="RelativeFilePath">Relative path of the file</param>
		/// <returns>Last write time of the file.</returns>
		DateTime GetLastWriteTime(string BasePath, string RelativeFilePath);

		/// <summary>
		/// Validate the given build records splitting them up into valid and invalid records.
		/// The invalid ones will need to be build.
		/// </summary>
		/// <param name="ValidBuildRecords">Output collection of up-to-date build records</param>
		/// <param name="InvalidBuildRecords">Output collection of build records needing to be built.</param>
		/// <param name="BuildRecords">Top level build records to test.  Dependencies are also checked so the
		/// of valid and invalid build records will usually be larger than the number of input records</param>
		/// <param name="ProjectPath">Path of the project</param>
		void ValidateRecursively(
			Dictionary<FileReference, CsProjBuildRecord> ValidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> InvalidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference?)> BuildRecords,
			FileReference ProjectPath);

		/// <summary>
		/// Test to see if the given file spec has any wild cards
		/// </summary>
		/// <param name="FileSpec">File spec to test</param>
		/// <returns>True if wildcards are present</returns>
		bool HasWildcards(string FileSpec);

		/// <summary>
		/// Unreal engine directory
		/// </summary>
		DirectoryReference EngineDirectory { get; }

		/// <summary>
		/// Dotnet directory shipped with the engine
		/// </summary>
		DirectoryReference DotnetDirectory { get; }

		/// <summary>
		/// Dotnet program
		/// </summary>
		FileReference DotnetPath { get; }
	}
}
