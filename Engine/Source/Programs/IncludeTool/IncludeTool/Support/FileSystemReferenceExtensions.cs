using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace IncludeTool.Support
{
	/// <summary>
	/// Extension methods to smooth the transition from IncludeTool.Support.*Reference to EpicGames.Core.*Reference
	/// Don't add to this; rather, remove it and fix up the call sites for consistency across projects.
	/// </summary>
	internal static class FileSystemReferenceExtensions
	{
		public static bool Exists(this FileReference File) => FileReference.Exists(File);
		public static void Delete(this FileReference File) => FileReference.Delete(File);
		public static void CreateDirectory(this DirectoryReference Directory) => DirectoryReference.CreateDirectory(Directory);

		public static bool Exists(this DirectoryReference Directory) => DirectoryReference.Exists(Directory); 
		public static IEnumerable<FileReference> EnumerateFileReferences(this DirectoryReference Directory) => DirectoryReference.EnumerateFiles(Directory);
		public static IEnumerable<FileReference> EnumerateFileReferences(this DirectoryReference Directory, string Pattern) => DirectoryReference.EnumerateFiles(Directory, Pattern);
		public static IEnumerable<DirectoryReference> EnumerateDirectoryReferences(this DirectoryReference Directory, string Pattern, System.IO.SearchOption Option) => DirectoryReference.EnumerateDirectories(Directory, Pattern, Option);
	}
}
