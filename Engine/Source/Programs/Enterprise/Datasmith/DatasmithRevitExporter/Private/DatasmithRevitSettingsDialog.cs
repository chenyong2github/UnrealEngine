// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace DatasmithRevitExporter
{
	public class DatasmithRevitParamGroupsDialog : Form
	{
		public static SortedList<string, int> AllBuiltinParamGroups { get; private set; }
		public CheckedListBox GroupsList { get; private set; }

		public DatasmithRevitParamGroupsDialog()
		{
			GroupsList = new CheckedListBox();
			GroupsList.Size = new Size(310, 350);

			if (AllBuiltinParamGroups == null)
			{
				AllBuiltinParamGroups = new SortedList<string, int>();

				foreach (Autodesk.Revit.DB.BuiltInParameterGroup BIP in Enum.GetValues(typeof(Autodesk.Revit.DB.BuiltInParameterGroup)))
				{
					string GroupLabel = Autodesk.Revit.DB.LabelUtils.GetLabelFor(BIP);

					if (!AllBuiltinParamGroups.ContainsKey(GroupLabel))
					{
						AllBuiltinParamGroups.Add(GroupLabel, (int)BIP);
					}
				}
			}

			foreach (var KV in AllBuiltinParamGroups)
			{
				GroupsList.Items.Add(KV.Key);
			}

			Button OKButton = new Button();
			OKButton.Name = "OKButton";
			OKButton.Text = DatasmithRevitResources.Strings.ExportOptionsDialog_ButtonOK;
			OKButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			OKButton.DialogResult = DialogResult.OK;
			OKButton.Location = new Point(211, 68);
			OKButton.Margin = new Padding(78, 0, 3, 3);
			OKButton.Size = new Size(75, 23);
			OKButton.TabIndex = 4;
			OKButton.UseVisualStyleBackColor = true;

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 1;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 2;
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.AutoSize = true;
			DialogLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.Size = new Size(300, 100);
			DialogLayout.TabIndex = 0;

			DialogLayout.Controls.Add(GroupsList, 0,0);
			DialogLayout.Controls.Add(OKButton, 0,1);

			Name = "AddGroups";
			Text = DatasmithRevitResources.Strings.SettingsDialog_AddGroupsDialogTitle; // "Add Groups";
			AcceptButton = OKButton;
			AutoScaleDimensions = new SizeF(6F, 13F);
			AutoScaleMode = AutoScaleMode.Font;
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			ClientSize = new Size(309, 114);
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			Padding = new Padding(10);
			SizeGripStyle = SizeGripStyle.Hide;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);
		}
	}

	public class DatasmithRevitSettingsDialog : Form
	{
		private ListBox GroupsList;
		private NumericUpDown LevelOfTessellation;
		private FSettings Settings;
		private Autodesk.Revit.DB.Document Document;
		private SortedList<string, int> AddedBuiltinParamGroups = new SortedList<string, int>();
		private List<string> AddedParamNames = new List<string>();

		public DatasmithRevitSettingsDialog(Autodesk.Revit.DB.Document InDocument)
		{
			string FormatTooltip(string InText)
			{
				string Tooltip = InText;
				return Tooltip.Replace("\\n", Environment.NewLine);
			}

			Document = InDocument;
			FormClosing += OnClosing;

			Settings = FSettingsManager.CurrentSettings;

			ToolTip OptionToolTip = new ToolTip();
			OptionToolTip.AutoPopDelay = 10000; // milliseconds

			FlowLayoutPanel LevelOfTesselationPanel = new FlowLayoutPanel();
			LevelOfTesselationPanel.FlowDirection = FlowDirection.LeftToRight;
			LevelOfTesselationPanel.WrapContents = false;
			LevelOfTesselationPanel.AutoSize = true;
			LevelOfTesselationPanel.Margin = new Padding(0, 0, 0, 30);
			{
				Label LevelOfTessellationLabel = new Label();
				LevelOfTessellationLabel.Name = "LevelOfTessellationLabel";
				LevelOfTessellationLabel.Text = DatasmithRevitResources.Strings.SettingsDialog_LevelOfTesselation;
				LevelOfTessellationLabel.Anchor = AnchorStyles.Left;
				LevelOfTessellationLabel.AutoSize = true;
				LevelOfTessellationLabel.Margin = new Padding(0, 0, 3, 0);
				LevelOfTessellationLabel.TabIndex = 2;
				LevelOfTessellationLabel.TextAlign = ContentAlignment.MiddleLeft;

				LevelOfTessellation = new NumericUpDown();
				LevelOfTessellation.Name = "LevelOfTessellation";
				LevelOfTessellation.Minimum = 1;
				LevelOfTessellation.Maximum = 15;
				LevelOfTessellation.Value = 8;
				LevelOfTessellation.Anchor = AnchorStyles.Left;
				LevelOfTessellation.AutoSize = true;
				LevelOfTessellation.TabIndex = 3;
				LevelOfTessellation.TextAlign = HorizontalAlignment.Right;

				OptionToolTip.SetToolTip(LevelOfTessellationLabel, FormatTooltip(DatasmithRevitResources.Strings.SettingsDialog_LevelOfTesselationTooltip));

				LevelOfTesselationPanel.Controls.Add(LevelOfTessellationLabel);
				LevelOfTesselationPanel.Controls.Add(LevelOfTessellation);
			}

			Label GroupsLabel = new Label();
			GroupsLabel.Name = "GroupsLabel";
			GroupsLabel.Text = DatasmithRevitResources.Strings.SettingsDialog_LabelMatchGroups;
			GroupsLabel.Anchor = AnchorStyles.Left;
			GroupsLabel.AutoSize = true;

			GroupsList = new ListBox();
			GroupsList.Size = new Size(450, 250);
			GroupsList.SelectionMode = SelectionMode.MultiExtended;

			OptionToolTip.SetToolTip(GroupsLabel, FormatTooltip(DatasmithRevitResources.Strings.SettingsDialog_MetadataFilterTooltip));
	
			FlowLayoutPanel ButtonsPanel = new FlowLayoutPanel();
			ButtonsPanel.FlowDirection = FlowDirection.TopDown;
			ButtonsPanel.WrapContents = false;
			ButtonsPanel.AutoSize = true;
			ButtonsPanel.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			ButtonsPanel.Padding = new Padding(0);
			{
				Button AddGroupButton = new Button();
				AddGroupButton.Name = "Add";
				AddGroupButton.Text = DatasmithRevitResources.Strings.SettingsDialog_ButtonAddGroups;
				AddGroupButton.Margin = new Padding(0);
				AddGroupButton.AutoSizeMode = AutoSizeMode.GrowAndShrink;
				AddGroupButton.AutoSize = true;
				AddGroupButton.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
				AddGroupButton.UseVisualStyleBackColor = true;
				AddGroupButton.Click += (s, e) =>
				{
					DatasmithRevitParamGroupsDialog Dlg = new DatasmithRevitParamGroupsDialog();
					if (DialogResult.OK == Dlg.ShowDialog())
					{
						bool bAddedNewGroups = false;
						foreach (int Index in Dlg.GroupsList.CheckedIndices)
						{
							string GroupLabel = DatasmithRevitParamGroupsDialog.AllBuiltinParamGroups.Keys[Index];
							if (!AddedBuiltinParamGroups.ContainsKey(GroupLabel))
							{
								int GroupId;
								if (DatasmithRevitParamGroupsDialog.AllBuiltinParamGroups.TryGetValue(GroupLabel, out GroupId))
								{
									AddedBuiltinParamGroups.Add(GroupLabel, GroupId);
									bAddedNewGroups = true;
								}
							}
						}

						if (bAddedNewGroups)
						{
							ReloadAddedGroupsList();
						}
					}
				};

				Button RemoveGroupButton = new Button();
				RemoveGroupButton.Name = "Remove";
				RemoveGroupButton.Text = DatasmithRevitResources.Strings.SettingsDialog_ButtonRemoveGroups;
				RemoveGroupButton.Margin = new Padding(0, 10, 0, 0);
				RemoveGroupButton.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
				RemoveGroupButton.AutoSizeMode = AutoSizeMode.GrowAndShrink;
				RemoveGroupButton.AutoSize = true;
				RemoveGroupButton.UseVisualStyleBackColor = true;
				RemoveGroupButton.Click += (s, e) =>
				{
					if (GroupsList.SelectedIndices.Count > 0)
					{
						int[] IndicesToRemove = new int[GroupsList.SelectedIndices.Count];
						GroupsList.SelectedIndices.CopyTo(IndicesToRemove, 0);
						Array.Sort(IndicesToRemove, (A, B) => B.CompareTo(A));

						foreach (int Index in IndicesToRemove)
						{
							GroupsList.Items.RemoveAt(Index);
							AddedBuiltinParamGroups.RemoveAt(Index);
						}
					}
				};

				ButtonsPanel.Controls.Add(AddGroupButton);
				ButtonsPanel.Controls.Add(RemoveGroupButton);
			}

			Button CloseButton = new Button();
			CloseButton.Name = "Close";
			CloseButton.Text = DatasmithRevitResources.Strings.SettingsDialog_ButtonClose;
			CloseButton.Anchor = AnchorStyles.Right;
			CloseButton.UseVisualStyleBackColor = true;
			CloseButton.Margin = new Padding(0, 7, 0, 0);
			CloseButton.Click += (s, e) =>
			{
				Close();
			};

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 2;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 3;
			DialogLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
			DialogLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
			DialogLayout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
			DialogLayout.AutoSize = true;
			DialogLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.TabIndex = 0;
		
			DialogLayout.Controls.Add(LevelOfTesselationPanel, 0, 0);
			DialogLayout.Controls.Add(GroupsLabel, 0, 1);
			DialogLayout.Controls.Add(GroupsList, 0, 2);
			DialogLayout.Controls.Add(ButtonsPanel, 1, 2);

			DialogLayout.SetColumnSpan(LevelOfTesselationPanel, 2);
			DialogLayout.SetColumnSpan(GroupsLabel, 2);

			Name = "MetadataExportFilter";
			Text = DatasmithRevitResources.Strings.SettingsDialog_DialogTitle;
			AutoScaleMode = AutoScaleMode.Font;
			AutoSize = true;
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			//Padding = new Padding(7);
			SizeGripStyle = SizeGripStyle.Hide;
			StartPosition = FormStartPosition.CenterParent;

			Panel ParentPanel = new Panel();
			ParentPanel.BorderStyle = BorderStyle.FixedSingle;
			ParentPanel.AutoSize = true;
			ParentPanel.Margin = new Padding(0);
			ParentPanel.Padding = new Padding(7);
			ParentPanel.Controls.Add(DialogLayout);
			//ParentPanel.Location = new Point(7, 7);

			FlowLayoutPanel TopLayout = new FlowLayoutPanel();
			TopLayout.FlowDirection = FlowDirection.TopDown;
			TopLayout.Padding = new Padding(7);
			TopLayout.AutoSize = true;
			TopLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;

			TopLayout.Controls.Add(ParentPanel);
			TopLayout.Controls.Add(CloseButton);

			Controls.Add(TopLayout);

			if (Settings == null)
			{
				Settings = new FSettings();
			}
			else
			{
				LevelOfTessellation.Value = Settings.LevelOfTesselation;

				foreach (int Group in Settings.MetadataParamGroupsFilter)
				{
					string GroupLabel = Autodesk.Revit.DB.LabelUtils.GetLabelFor((Autodesk.Revit.DB.BuiltInParameterGroup)Group);
					AddedBuiltinParamGroups.Add(GroupLabel, Group);
				}

				ReloadAddedGroupsList();
			}
		}

		private void ReloadAddedGroupsList()
		{
			GroupsList.Items.Clear();

			foreach (var KV in AddedBuiltinParamGroups)
			{
				GroupsList.Items.Add(KV.Key);
			}
		}

		private void OnClosing(object InSender, FormClosingEventArgs InArgs)
		{
			Settings.LevelOfTesselation = Decimal.ToInt32(LevelOfTessellation.Value);
			Settings.MetadataParamGroupsFilter = AddedBuiltinParamGroups.Values;
			FSettingsManager.WriteSettings(Document, Settings);
		}
	}
}
