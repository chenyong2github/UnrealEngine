// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

#nullable enable

namespace UnrealGameSync
{
	partial class SelectProjectFromWorkspaceWindow : Form
	{
		static class EnumerateWorkspaceProjectsTask
		{
			static readonly string[] Patterns =
			{
				"....uproject",
				"....uprojectdirs",
			};

			public static async Task<List<string>> RunAsync(IPerforceConnection Perforce, string ClientName, CancellationToken CancellationToken)
			{
				ClientRecord ClientSpec = await Perforce.GetClientAsync(Perforce.Settings.ClientName, CancellationToken);

				string ClientRoot = ClientSpec.Root.TrimEnd(Path.DirectorySeparatorChar);
				if (String.IsNullOrEmpty(ClientRoot))
				{
					throw new UserErrorException($"Client '{ClientName}' does not have a valid root directory.");
				}

				List<FStatRecord> FileRecords = new List<FStatRecord>();
				foreach(string Pattern in Patterns)
				{
					string Filter = String.Format("//{0}/{1}", ClientName, Pattern);

					List<FStatRecord> WildcardFileRecords = await Perforce.FStatAsync(Filter, CancellationToken).ToListAsync(CancellationToken);
					WildcardFileRecords.RemoveAll(x => x.HeadAction == FileAction.Delete || x.HeadAction == FileAction.MoveDelete);

					FileRecords.AddRange(WildcardFileRecords);
				}

				string ClientPrefix = ClientRoot;
				if (!ClientPrefix.EndsWith(Path.DirectorySeparatorChar))
				{
					ClientPrefix += Path.DirectorySeparatorChar;
				}

				List<string> Paths = new List<string>();
				foreach(FStatRecord FileRecord in FileRecords)
				{
					if(FileRecord.ClientFile != null && FileRecord.ClientFile.StartsWith(ClientPrefix, StringComparison.OrdinalIgnoreCase))
					{
						Paths.Add(FileRecord.ClientFile.Substring(ClientRoot.Length).Replace(Path.DirectorySeparatorChar, '/'));
					}
				}

				return Paths;
			}
		}

		[DllImport("Shell32.dll", EntryPoint = "ExtractIconExW", CharSet = CharSet.Unicode, ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
		private static extern int ExtractIconEx(string sFile, int iIndex, IntPtr piLargeVersion, out IntPtr piSmallVersion, int amountIcons);

		List<string> ProjectFiles;
		string SelectedProjectFile;

		class ProjectNode
		{
			public string FullName;
			public string Folder;
			public string Name;

			public ProjectNode(string FullName)
			{
				this.FullName = FullName;

				int SlashIdx = FullName.LastIndexOf('/');
				Folder = FullName.Substring(0, SlashIdx);
				Name = FullName.Substring(SlashIdx + 1);
			}
		}
		
		public SelectProjectFromWorkspaceWindow(string WorkspaceName, List<string> ProjectFiles, string SelectedProjectFile)
		{
			InitializeComponent();
			
			this.ProjectFiles = ProjectFiles;
			this.SelectedProjectFile = SelectedProjectFile;

			// Make the image strip containing icons for nodes in the tree
			IntPtr FolderIconPtr;
			ExtractIconEx("imageres.dll", 3, IntPtr.Zero, out FolderIconPtr, 1);

			Icon[] Icons = new Icon[]{ Icon.FromHandle(FolderIconPtr), Properties.Resources.Icon };

			Bitmap TypeImageListBitmap = new Bitmap(Icons.Length * 16, 16, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
			using(Graphics Graphics = Graphics.FromImage(TypeImageListBitmap))
			{
				for(int IconIdx = 0; IconIdx < Icons.Length; IconIdx++)
				{
					Graphics.DrawIcon(Icons[IconIdx], new Rectangle(IconIdx * 16, 0, 16, 16));
				}
			}

			ImageList TypeImageList = new ImageList();
			TypeImageList.ImageSize = new Size(16, 16);
			TypeImageList.ColorDepth = ColorDepth.Depth32Bit;
			TypeImageList.Images.AddStrip(TypeImageListBitmap);
			ProjectTreeView.ImageList = TypeImageList;

			// Create the root node
			TreeNode RootNode = new TreeNode();
			RootNode.Text = WorkspaceName;
			RootNode.Expand();
			ProjectTreeView.Nodes.Add(RootNode);

			// Populate the tree
			Populate();
		}

		private void Populate()
		{
			// Clear out the existing nodes
			TreeNode RootNode = ProjectTreeView.Nodes[0];
			RootNode.Nodes.Clear();

			// Filter the project files
			List<string> FilteredProjectFiles = new List<string>(ProjectFiles); 
			if(!ShowProjectDirsFiles.Checked)
			{
				FilteredProjectFiles.RemoveAll(x => x.EndsWith(".uprojectdirs", StringComparison.OrdinalIgnoreCase));
			}

			// Sort by paths, then files
			List<ProjectNode> ProjectNodes = FilteredProjectFiles.Select(x => new ProjectNode(x)).OrderBy(x => x.Folder).ThenBy(x => x.Name).ToList();

			// Add the folders for each project
			TreeNode[] ProjectParentNodes = new TreeNode[ProjectNodes.Count];
			for(int Idx = 0; Idx < ProjectNodes.Count; Idx++)
			{
				TreeNode ParentNode = RootNode;
				if(ProjectNodes[Idx].Folder.Length > 0)
				{
					string[] Fragments = ProjectNodes[Idx].Folder.Split(new char[]{ '/' }, StringSplitOptions.RemoveEmptyEntries);
					foreach(string Fragment in Fragments)
					{
						ParentNode = FindOrAddChildNode(ParentNode, Fragment, 0);
					}
				}
				ProjectParentNodes[Idx] = ParentNode;
			}

			// Add the actual project nodes themselves
			for(int Idx = 0; Idx < ProjectNodes.Count; Idx++)
			{
				TreeNode Node = FindOrAddChildNode(ProjectParentNodes[Idx], ProjectNodes[Idx].Name, 1);
				Node.Tag = ProjectNodes[Idx].FullName;

				if(String.Compare(ProjectNodes[Idx].FullName, SelectedProjectFile, StringComparison.InvariantCultureIgnoreCase) == 0)
				{
					ProjectTreeView.SelectedNode = Node;
					for(TreeNode ParentNode = Node.Parent; ParentNode != RootNode; ParentNode = ParentNode.Parent)
					{
						ParentNode.Expand();
					}
				}
			}
		}

		static TreeNode FindOrAddChildNode(TreeNode ParentNode, string Text, int ImageIndex)
		{
			foreach(TreeNode? ChildNode in ParentNode.Nodes)
			{
				if(ChildNode != null && String.Compare(ChildNode.Text, Text, StringComparison.InvariantCultureIgnoreCase) == 0)
				{
					return ChildNode;
				}
			}

			TreeNode NextNode = new TreeNode(Text);
			NextNode.ImageIndex = ImageIndex;
			NextNode.SelectedImageIndex = ImageIndex;
			ParentNode.Nodes.Add(NextNode);
			return NextNode;
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings Perforce, string WorkspaceName, string WorkspacePath, IServiceProvider ServiceProvider, [NotNullWhen(true)] out string? NewWorkspacePath)
		{
			Perforce = new PerforceSettings(Perforce) { ClientName = WorkspaceName };

			ILogger Logger = ServiceProvider.GetRequiredService<ILogger<SelectProjectFromWorkspaceWindow>>();

			ModalTask<List<string>>? PathsTask = PerforceModalTask.Execute(Owner, "Finding Projects", "Finding projects, please wait...", Perforce, (p, c) => EnumerateWorkspaceProjectsTask.RunAsync(p, WorkspaceName, c), Logger);
			if(PathsTask == null || !PathsTask.Succeeded)
			{
				NewWorkspacePath = null;
				return false;
			}

			SelectProjectFromWorkspaceWindow SelectProjectWindow = new SelectProjectFromWorkspaceWindow(WorkspaceName, PathsTask.Result, WorkspacePath);
			if(SelectProjectWindow.ShowDialog() == DialogResult.OK && !String.IsNullOrEmpty(SelectProjectWindow.SelectedProjectFile))
			{
				NewWorkspacePath = SelectProjectWindow.SelectedProjectFile;
				return true;
			}
			else
			{
				NewWorkspacePath = null;
				return false;
			}
		}

		private void ProjectTreeView_AfterSelect(object sender, TreeViewEventArgs e)
		{
			OkBtn.Enabled = (e.Node.Tag != null);
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			if(ProjectTreeView.SelectedNode != null && ProjectTreeView.SelectedNode.Tag != null)
			{
				SelectedProjectFile = (string)ProjectTreeView.SelectedNode.Tag;
				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void ShowProjectDirsFiles_CheckedChanged(object sender, EventArgs e)
		{
			Populate();
		}
	}
}
