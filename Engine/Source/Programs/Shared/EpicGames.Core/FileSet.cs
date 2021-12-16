// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Describes a tree of files represented by some arbitrary type. Allows manipulating files/directories in a functional manner; 
	/// filtering a view of a certain directory, mapping files from one location to another, etc... before actually realizing those changes on disk.
	/// </summary>
	public abstract class FileSet : IEnumerable<FileReference>
	{
		/// <summary>
		/// An empty fileset
		/// </summary>
		public static FileSet Empty { get; } = new FileSetFromFiles(Enumerable.Empty<(string, FileReference)>());
		
		/// <summary>
		/// Path of this tree
		/// </summary>
		public string Path { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Path">Relative path within the tree</param>
		public FileSet(string Path)
		{
			this.Path = Path;
		}

		/// <summary>
		/// Enumerate files in the current tree
		/// </summary>
		/// <returns>Sequence consisting of names and file objects</returns>
		public abstract IEnumerable<KeyValuePair<string, FileReference>> EnumerateFiles();

		/// <summary>
		/// Enumerate subtrees in the current tree
		/// </summary>
		/// <returns>Sequence consisting of names and subtree objects</returns>
		public abstract IEnumerable<KeyValuePair<string, FileSet>> EnumerateDirectories();

		/// <summary>
		/// Creates a file tree from a given set of files
		/// </summary>
		/// <param name="Files"></param>
		/// <returns>Tree containing the given files</returns>
		public static FileSet FromFile(DirectoryReference Directory, string File)
		{
			return FromFiles(new[] { (File, FileReference.Combine(Directory, File)) });
		}

		/// <summary>
		/// Creates a file tree from a given set of files
		/// </summary>
		/// <param name="Files"></param>
		/// <returns>Tree containing the given files</returns>
		public static FileSet FromFiles(IEnumerable<(string, FileReference)> Files)
		{
			return new FileSetFromFiles(Files);
		}

		/// <summary>
		/// Creates a file tree from a given set of files
		/// </summary>
		/// <param name="Files"></param>
		/// <returns>Tree containing the given files</returns>
		public static FileSet FromFile(DirectoryReference Directory, FileReference File)
		{
			return FromFiles(Directory, new[] { File });
		}

		/// <summary>
		/// Creates a file tree from a given set of files
		/// </summary>
		/// <param name="Files"></param>
		/// <returns>Tree containing the given files</returns>
		public static FileSet FromFiles(DirectoryReference Directory, IEnumerable<FileReference> Files)
		{
			return new FileSetFromFiles(Files.Select(x => (x.MakeRelativeTo(Directory), x)));
		}

		/// <summary>
		/// Creates a file tree from a folder on disk
		/// </summary>
		/// <param name="Directory"></param>
		/// <returns></returns>
		public static FileSet FromDirectory(DirectoryReference Directory)
		{
			return new FileSetFromDirectory(new DirectoryInfo(Directory.FullName));
		}

		/// <summary>
		/// Creates a file tree from a folder on disk
		/// </summary>
		/// <param name="DirectoryInfo"></param>
		/// <returns></returns>
		public static FileSet FromDirectory(DirectoryInfo DirectoryInfo)
		{
			return new FileSetFromDirectory(DirectoryInfo);
		}

		/// <summary>
		/// Create a tree containing files filtered by any of the given wildcards
		/// </summary>
		/// <param name="Rules"></param>
		/// <returns></returns>
		public FileSet Filter(string Rules)
		{
			return Filter(Rules.Split(';'));
		}

		/// <summary>
		/// Create a tree containing files filtered by any of the given wildcards
		/// </summary>
		/// <param name="Rules"></param>
		/// <returns></returns>
		public FileSet Filter(params string[] Rules)
		{
			return new FileSetFromFilter(this, new FileFilter(Rules));
		}

		/// <summary>
		/// Create a tree containing files filtered by any of the given file filter objects
		/// </summary>
		/// <param name="Filters"></param>
		/// <returns></returns>
		public FileSet Filter(params FileFilter[] Filters)
		{
			return new FileSetFromFilter(this, Filters);
		}

		/// <summary>
		/// Create a tree containing the exception of files with another tree
		/// </summary>
		/// <param name="Filter">Files to exclude from the filter</param>
		/// <returns></returns>
		public FileSet Except(string Filter)
		{
			return Except(Filter.Split(';'));
		}

		/// <summary>
		/// Create a tree containing the exception of files with another tree
		/// </summary>
		/// <param name="Filter">Files to exclude from the filter</param>
		/// <returns></returns>
		public FileSet Except(params string[] Rules)
		{
			return new FileSetFromFilter(this, new FileFilter(Rules.Select(x => $"-{x}")));
		}

		/// <summary>
		/// Create a tree containing the union of files with another tree
		/// </summary>
		/// <param name="Lhs"></param>
		/// <param name="Rhs"></param>
		/// <returns></returns>
		public static FileSet Union(FileSet Lhs, FileSet Rhs)
		{
			return new FileSetFromUnion(Lhs, Rhs);
		}

		/// <summary>
		/// Create a tree containing the exception of files with another tree
		/// </summary>
		/// <param name="Lhs"></param>
		/// <param name="Rhs"></param>
		/// <returns></returns>
		public static FileSet Except(FileSet Lhs, FileSet Rhs)
		{
			return new FileSetFromExcept(Lhs, Rhs);
		}

		/// <inheritdoc cref="Union(FileSet, FileSet)"/>
		public static FileSet operator +(FileSet Lhs, FileSet Rhs)
		{
			return Union(Lhs, Rhs);
		}

		/// <inheritdoc cref="Except(FileSet, FileSet)"/>
		public static FileSet operator -(FileSet Lhs, FileSet Rhs)
		{
			return Except(Lhs, Rhs);
		}

		/// <summary>
		/// Flatten to a map of files in a target directory
		/// </summary>
		/// <returns></returns>
		public Dictionary<string, FileReference> Flatten()
		{
			Dictionary<string, FileReference> PathToSourceFile = new Dictionary<string, FileReference>(StringComparer.OrdinalIgnoreCase);
			FlattenInternal(String.Empty, PathToSourceFile);
			return PathToSourceFile;
		}

		private void FlattenInternal(string PathPrefix, Dictionary<string, FileReference> PathToSourceFile)
		{
			foreach ((string Path, FileReference File) in EnumerateFiles())
			{
				PathToSourceFile[PathPrefix + Path] = File;
			}
			foreach((string Path, FileSet FileSet) in EnumerateDirectories())
			{
				FileSet.FlattenInternal(PathPrefix + Path + "/", PathToSourceFile);
			}
		}

		/// <summary>
		/// Flatten to a map of files in a target directory
		/// </summary>
		/// <returns></returns>
		public Dictionary<FileReference, FileReference> Flatten(DirectoryReference OutputDir)
		{
			Dictionary<FileReference, FileReference> TargetToSourceFile = new Dictionary<FileReference, FileReference>();
			foreach ((string Path, FileReference SourceFile) in Flatten())
			{
				FileReference TargetFile = FileReference.Combine(OutputDir, Path);
				TargetToSourceFile[TargetFile] = SourceFile;
			}
			return TargetToSourceFile;
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		/// <inheritdoc/>
		public IEnumerator<FileReference> GetEnumerator() => Flatten().Values.GetEnumerator();
	}

	/// <summary>
	/// File tree from a known set of files
	/// </summary>
	class FileSetFromFiles : FileSet
	{
		Dictionary<string, FileReference> Files = new Dictionary<string, FileReference>();
		Dictionary<string, FileSetFromFiles> SubTrees = new Dictionary<string, FileSetFromFiles>();

		/// <summary>
		/// Private constructor
		/// </summary>
		/// <param name="Path"></param>
		private FileSetFromFiles(string Path)
			: base(Path)
		{
		}

		/// <summary>
		/// Creates a tree from a given set of files
		/// </summary>
		/// <param name="InputFiles"></param>
		public FileSetFromFiles(IEnumerable<(string, FileReference)> InputFiles)
			: this(String.Empty)
		{
			foreach ((string Path, FileReference File) in InputFiles)
			{
				string[] Fragments = Path.Split(new[] { '/', '\\' }, StringSplitOptions.RemoveEmptyEntries);

				FileSetFromFiles Current = this;
				for (int Idx = 0; Idx < Fragments.Length - 1; Idx++)
				{
					FileSetFromFiles? Next;
					if (!Current.SubTrees.TryGetValue(Fragments[Idx], out Next))
					{
						Next = new FileSetFromFiles(Current.Path + Fragments[Idx] + "/");
						Current.SubTrees.Add(Fragments[Idx], Next);
					}
					Current = Next;
				}

				Current.Files.Add(Fragments[^1], File);
			}
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileReference>> EnumerateFiles() => Files;

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileSet>> EnumerateDirectories() => SubTrees.Select(x => new KeyValuePair<string, FileSet>(x.Key, x.Value));
	}

	/// <summary>
	/// File tree enumerated from the contents of an existing directory
	/// </summary>
	sealed class FileSetFromDirectory : FileSet
	{
		DirectoryInfo DirectoryInfo;

		/// <summary>
		/// Constructor
		/// </summary>
		public FileSetFromDirectory(DirectoryInfo DirectoryInfo)
			: this(DirectoryInfo, "/")
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public FileSetFromDirectory(DirectoryInfo DirectoryInfo, string Path)
			: base(Path)
		{
			this.DirectoryInfo = DirectoryInfo;
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileReference>> EnumerateFiles() => DirectoryInfo.EnumerateFiles().Select(x => new KeyValuePair<string, FileReference>(x.Name, new FileReference(x)));

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileSet>> EnumerateDirectories() => DirectoryInfo.EnumerateDirectories().Select(x => KeyValuePair.Create<string, FileSet>(x.Name, new FileSetFromDirectory(x)));
	}

	/// <summary>
	/// File tree enumerated from the combination of two separate trees
	/// </summary>
	class FileSetFromUnion : FileSet
	{
		FileSet Lhs;
		FileSet Rhs;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Lhs">First file tree for the union</param>
		/// <param name="Rhs">Other file tree for the union</param>
		public FileSetFromUnion(FileSet Lhs, FileSet Rhs)
			: base(Lhs.Path)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileReference>> EnumerateFiles()
		{
			Dictionary<string, FileReference> Files = new Dictionary<string, FileReference>(Lhs.EnumerateFiles(), StringComparer.OrdinalIgnoreCase);
			foreach ((string Name, FileReference File) in Rhs.EnumerateFiles())
			{
				FileReference? ExistingFile;
				if (!Files.TryGetValue(Name, out ExistingFile))
				{
					Files.Add(Name, File);
				}
				else if (ExistingFile == null || !ExistingFile.Equals(File))
				{
					throw new InvalidOperationException($"Conflict for contents of {Path}{Name} - could be {ExistingFile} or {File}");
				}
			}
			return Files;
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileSet>> EnumerateDirectories()
		{
			Dictionary<string, FileSet> NameToSubTree = new Dictionary<string, FileSet>(Lhs.EnumerateDirectories(), StringComparer.OrdinalIgnoreCase);
			foreach ((string Name, FileSet SubTree) in Rhs.EnumerateDirectories())
			{
				FileSet? ExistingSubTree;
				if (NameToSubTree.TryGetValue(Name, out ExistingSubTree))
				{
					NameToSubTree[Name] = new FileSetFromUnion(ExistingSubTree, SubTree);
				}
				else
				{
					NameToSubTree[Name] = SubTree;
				}
			}
			return NameToSubTree;
		}
	}

	/// <summary>
	/// File tree enumerated from the combination of two separate trees
	/// </summary>
	class FileSetFromExcept : FileSet
	{
		FileSet Lhs;
		FileSet Rhs;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Lhs">First file tree for the union</param>
		/// <param name="Rhs">Other file tree for the union</param>
		public FileSetFromExcept(FileSet Lhs, FileSet Rhs)
			: base(Lhs.Path)
		{
			this.Lhs = Lhs;
			this.Rhs = Rhs;
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileReference>> EnumerateFiles()
		{
			HashSet<string> RhsFiles = new HashSet<string>(Rhs.EnumerateFiles().Select(x => x.Key), StringComparer.OrdinalIgnoreCase);
			return Lhs.EnumerateFiles().Where(x => !RhsFiles.Contains(x.Key));
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileSet>> EnumerateDirectories()
		{
			Dictionary<string, FileSet> RhsDirs = new Dictionary<string, FileSet>(Rhs.EnumerateDirectories(), StringComparer.OrdinalIgnoreCase);
			foreach ((string Name, FileSet LhsSet) in Lhs.EnumerateDirectories())
			{
				FileSet? RhsSet;
				if (RhsDirs.TryGetValue(Name, out RhsSet))
				{
					yield return KeyValuePair.Create<string, FileSet>(Name, new FileSetFromExcept(LhsSet, RhsSet));
				}
				else
				{
					yield return KeyValuePair.Create(Name, LhsSet);
				}
			}
		}
	}

	/// <summary>
	/// File tree which includes only those files which match any given filter
	/// </summary>
	/// <typeparam name="T">Class containing information about a file</typeparam>
	class FileSetFromFilter : FileSet
	{
		FileSet Inner;
		FileFilter[] Filters;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The tree to filter</param>
		/// <param name="Filters"></param>
		public FileSetFromFilter(FileSet Inner, params FileFilter[] Filters)
			: base(Inner.Path)
		{
			this.Inner = Inner;
			this.Filters = Filters;
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileReference>> EnumerateFiles()
		{
			foreach (KeyValuePair<string, FileReference> Item in Inner.EnumerateFiles())
			{
				string FilterName = Inner.Path + Item.Key;
				if (Filters.Any(x => x.Matches(FilterName)))
				{
					yield return Item;
				}
			}
		}

		/// <inheritdoc/>
		public override IEnumerable<KeyValuePair<string, FileSet>> EnumerateDirectories()
		{
			foreach (KeyValuePair<string, FileSet> Item in Inner.EnumerateDirectories())
			{
				string FilterName = Inner.Path + Item.Key;

				FileFilter[] PossibleFilters = Filters.Where(x => x.PossiblyMatches(FilterName)).ToArray();
				if (PossibleFilters.Length > 0)
				{
					FileSetFromFilter SubTreeFilter = new FileSetFromFilter(Item.Value, PossibleFilters);
					yield return new KeyValuePair<string, FileSet>(Item.Key, SubTreeFilter);
				}
			}
		}
	}
}
