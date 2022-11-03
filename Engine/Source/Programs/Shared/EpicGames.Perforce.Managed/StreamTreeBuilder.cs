// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using EpicGames.Core;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Utility class to efficiently track changes to a StreamTree object in memory
	/// </summary>
	public class StreamTreeBuilder
	{
		/// <summary>
		/// Map from name to mutable tree
		/// </summary>
		public Dictionary<Utf8String, StreamFile> NameToFile { get; }

		/// <summary>
		/// Map from name to mutable tree
		/// </summary>
		public Dictionary<Utf8String, StreamTreeRef> NameToTree { get; }

		/// <summary>
		/// Map from name to mutable tree
		/// </summary>
		public Dictionary<Utf8String, StreamTreeBuilder> NameToTreeBuilder { get; } = new Dictionary<Utf8String, StreamTreeBuilder>(FileUtils.PlatformPathComparerUtf8);

		/// <summary>
		/// Tests whether the tree is empty
		/// </summary>
		public bool IsEmpty => NameToFile.Count == 0 && NameToTree.Count == 0 && NameToTreeBuilder.Count == 0;

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamTreeBuilder()
		{
			NameToFile = new Dictionary<Utf8String, StreamFile>(FileUtils.PlatformPathComparerUtf8);
			NameToTree = new Dictionary<Utf8String, StreamTreeRef>(FileUtils.PlatformPathComparerUtf8);
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="tree"></param>
		public StreamTreeBuilder(StreamTree tree)
		{
			NameToFile = new Dictionary<Utf8String, StreamFile>(tree.NameToFile, FileUtils.PlatformPathComparerUtf8);
			NameToTree = new Dictionary<Utf8String, StreamTreeRef>(tree.NameToTree, FileUtils.PlatformPathComparerUtf8);
		}

		/// <summary>
		/// Encodes the current tree state and returns a reference to it
		/// </summary>
		/// <param name="Objects">Dictionary of encoded objects</param>
		/// <returns></returns>
		public StreamTree Encode(Func<StreamTree, IoHash> writeTree)
		{
			// Recursively serialize all the child items
			EncodeChildren(writeTree);

			// Find the common base path for all items in this tree.
			Dictionary<Utf8String, int> basePathToCount = new Dictionary<Utf8String, int>();
			foreach ((Utf8String name, StreamFile file) in NameToFile)
			{
				AddBasePath(basePathToCount, file.Path, name);
			}
			foreach ((Utf8String name, StreamTreeRef tree) in NameToTree)
			{
				AddBasePath(basePathToCount, tree.Path, name);
			}

			// Create the new tree
			Utf8String basePath = (basePathToCount.Count == 0) ? Utf8String.Empty : basePathToCount.MaxBy(x => x.Value).Key;
			return new StreamTree(basePath, NameToFile, NameToTree);
		}

		/// <summary>
		/// Encodes a StreamTreeRef from this tree
		/// </summary>
		/// <param name="writeTree"></param>
		/// <returns>The new tree ref</returns>
		public StreamTreeRef EncodeRef(Func<StreamTree, IoHash> writeTree)
		{
			StreamTree tree = Encode(writeTree);
			return new StreamTreeRef(tree.Path, writeTree(tree));
		}

		/// <summary>
		/// Collapses all of the builders underneath this node
		/// </summary>
		/// <param name="writeTree"></param>
		public void EncodeChildren(Func<StreamTree, IoHash> writeTree)
		{
			foreach ((Utf8String subTreeName, StreamTreeBuilder subTreeBuilder) in NameToTreeBuilder)
			{
				StreamTree subTree = subTreeBuilder.Encode(writeTree);
				if (subTree.NameToFile.Count > 0 || subTree.NameToTree.Count > 0)
				{
					IoHash hash = writeTree(subTree);
					NameToTree[subTreeName] = new StreamTreeRef(subTree.Path, hash);
				}
			}
			NameToTreeBuilder.Clear();
		}

		/// <summary>
		/// Adds the base path of the given item to the count of similar items
		/// </summary>
		/// <param name="basePathToCount"></param>
		/// <param name="path"></param>
		/// <param name="name"></param>
		static void AddBasePath(Dictionary<Utf8String, int> basePathToCount, Utf8String path, Utf8String name)
		{
			if (path.EndsWith(name) && path[^(name.Length + 1)] == '/')
			{
				Utf8String basePath = path[..^(name.Length + 1)];
				basePathToCount.TryGetValue(basePath, out int count);
				basePathToCount[basePath] = count + 1;
			}
		}
	}

	/// <summary>
	/// Variant of StreamTreeBuilder which caches path fragments lookups for faster building
	/// when there's lots of files.
	/// </summary>
	public class DepotStreamTreeBuilder : StreamTreeBuilder
	{
		/// <summary>
		/// List of the last path fragments
		/// Since file records that are returned are typically sorted by their position in the tree,
		/// we can save quite a lot of processing by reusing as many fragments as possible
		/// </summary>
		private readonly List<(Utf8String, StreamTreeBuilder)> _fragments = new ();

		private readonly Utf8String _expectedPathPrefix;

		public DepotStreamTreeBuilder(string expectedPathPrefix)
		{
			if (expectedPathPrefix[^1] != '/')
			{
				throw new ArgumentException("Prefix must end with a slash");
			}
			_expectedPathPrefix = expectedPathPrefix;
		}

		public void AddDepotFile(StreamFile file)
		{
			if (!file.Path.StartsWith(_expectedPathPrefix))
			{
				throw new InvalidDataException($"File path ('{file.Path}') does not begin with prefix ('{_expectedPathPrefix}')");
			}

			ReadOnlySpan<byte> pathSpan = file.Path.Span;

			// Parse out the data
			StreamTreeBuilder lastStreamDirectory = this;

			// Try to match up as many fragments from the last file.
			int fragmentMinIdx = _expectedPathPrefix.Length;
			for (int fragmentIdx = 0; ; fragmentIdx++)
			{
				// Find the next directory separator
				int fragmentMaxIdx = fragmentMinIdx;
				while (fragmentMaxIdx < pathSpan.Length && pathSpan[fragmentMaxIdx] != '/')
				{
					fragmentMaxIdx++;
				}
				if (fragmentMaxIdx == pathSpan.Length)
				{
					_fragments.RemoveRange(fragmentIdx, _fragments.Count - fragmentIdx);
					break;
				}

				// Get the fragment text
				Utf8String fragment = new (file.Path.Memory.Slice(fragmentMinIdx, fragmentMaxIdx - fragmentMinIdx));

				// If this fragment matches the same fragment from the previous iteration, take the last stream directory straight away
				if (fragmentIdx < _fragments.Count)
				{
					if (_fragments[fragmentIdx].Item1 == fragment)
					{
						lastStreamDirectory = _fragments[fragmentIdx].Item2;
					}
					else
					{
						_fragments.RemoveRange(fragmentIdx, _fragments.Count - fragmentIdx);
					}
				}

				// Otherwise, find or add a directory for this fragment into the last directory
				if (fragmentIdx >= _fragments.Count)
				{
					Utf8String unescapedFragment = PerforceUtils.UnescapePath(fragment);

					StreamTreeBuilder? nextStreamDirectory;
					if (!lastStreamDirectory.NameToTreeBuilder.TryGetValue(unescapedFragment, out nextStreamDirectory))
					{
						nextStreamDirectory = new StreamTreeBuilder();
						lastStreamDirectory.NameToTreeBuilder.Add(unescapedFragment, nextStreamDirectory);
					}
					lastStreamDirectory = nextStreamDirectory;

					_fragments.Add((fragment, lastStreamDirectory));
				}

				// Move to the next fragment
				fragmentMinIdx = fragmentMaxIdx + 1;
			}
			
			Utf8String fileName = PerforceUtils.UnescapePath(file.Path.Slice(fragmentMinIdx));
			lastStreamDirectory.NameToFile.Add(fileName, file);
		}
	}
}
