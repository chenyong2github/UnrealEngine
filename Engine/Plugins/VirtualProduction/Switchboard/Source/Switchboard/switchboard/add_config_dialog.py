# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard.config import CONFIG
from PySide2 import QtCore, QtWidgets

import os, sys, subprocess, shlex, pathlib


class AddConfigDialog(QtWidgets.QDialog):
    def __init__(self, stylesheet, uproject_search_path, previous_engine_dir, parent):
        super().__init__(parent=parent, f=QtCore.Qt.WindowCloseButtonHint)

        self.config_name = None
        self.uproject = None
        self.engine_dir = None

        self.setStyleSheet(stylesheet)

        self.setWindowTitle("Add new Switchboard Configuration")

        self.form_layout =  QtWidgets.QFormLayout()
        self.name_line_edit = QtWidgets.QLineEdit()
        self.name_line_edit.textChanged.connect(self.on_name_changed)
        self.form_layout.addRow("Name", self.name_line_edit)

        self.uproject_line_edit = QtWidgets.QLineEdit()
        self.uproject_line_edit.textChanged.connect(self.on_uproject_changed)
        self.uproject_browse_button = QtWidgets.QPushButton("Browse")
        self.uproject_browse_button.clicked.connect(lambda: self.on_browse_uproject_path(uproject_search_path))

        uproject_layout = QtWidgets.QHBoxLayout()
        uproject_layout.addWidget(self.uproject_line_edit)
        uproject_layout.addWidget(self.uproject_browse_button)
        self.form_layout.addRow("uProject", uproject_layout)

        self.engine_dir_line_edit = QtWidgets.QLineEdit()
        if os.path.exists(previous_engine_dir): # re-use previous engine dir
            self.engine_dir_line_edit.setText(previous_engine_dir)
            self.engine_dir = previous_engine_dir
        self.engine_dir_line_edit.textChanged.connect(self.on_engine_dir_changed)
        self.engine_dir_browse_button = QtWidgets.QPushButton("Browse")
        self.engine_dir_browse_button.clicked.connect(self.on_browse_engine_dir)

        engine_dir_layout = QtWidgets.QHBoxLayout()
        engine_dir_layout.addWidget(self.engine_dir_line_edit)
        engine_dir_layout.addWidget(self.engine_dir_browse_button)
        self.form_layout.addRow("Engine Dir", engine_dir_layout)

        layout = QtWidgets.QVBoxLayout()
        layout.insertLayout(0, self.form_layout)

        self.p4_group = QtWidgets.QGroupBox("Perforce")
        self.p4_group.setCheckable(True)
        self.p4_group.setChecked(bool(CONFIG.P4_ENABLED.get_value()))

        self.p4_project_path_line_edit = QtWidgets.QLineEdit(CONFIG.P4_PROJECT_PATH.get_value())
        self.p4_engine_path_line_edit = QtWidgets.QLineEdit(CONFIG.P4_ENGINE_PATH.get_value())
        self.p4_workspace_line_edit = QtWidgets.QLineEdit(CONFIG.SOURCE_CONTROL_WORKSPACE.get_value())
        p4_layout = QtWidgets.QFormLayout()
        p4_layout.addRow("P4 Project Path", self.p4_project_path_line_edit)
        p4_layout.addRow("P4 Engine Path", self.p4_engine_path_line_edit)
        p4_layout.addRow("Workspace Name", self.p4_workspace_line_edit)
        self.p4_group.setLayout(p4_layout)
        layout.addWidget(self.p4_group)

        self.button_box = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        self.button_box.button(QtWidgets.QDialogButtonBox.Ok).setEnabled(False)
        self.button_box.accepted.connect(self.accept)
        self.button_box.rejected.connect(self.reject)
        layout.addWidget(self.button_box)

        self.setLayout(layout)
        self.setMinimumWidth(450)

        self.populate_initial_best_project_guess()

    def find_upstream_path_with_name(self, path, name, includeSiblings=False):
        ''' Goes up the folder chain until it finds the desired name, optionally considering sibling folders
        '''
        # ensure we have a pathlib.PurePath object
        path = pathlib.PurePath(path)

        # if the folder name coincides, we're done
        if path.name == name:
            return str(path)

        # If including siblings, check those as well.
        if includeSiblings:
            if name in next(os.walk(path.parent))[1]:
                return str(path.parent/name)

        # detect if we already reached the root folder
        if path == path.parent:
            raise FileNotFoundError(f'Could not find {name} in path')

        # go one up, recursively
        return self.find_upstream_path_with_name(path.parent, name, includeSiblings)

    def populate_initial_best_project_guess(self):
        ''' Populates the editor and project with a best guess based on the running processes '''

        # perform the detection
        editors,projects = self.detect_running_projects()

        editorfolder = ''
        projectpath = ''
        
        # find a suitable candidate
        for idx, editor in enumerate(editors):
            try:
                editorfolder = self.find_upstream_path_with_name(editor, "Engine")
            except FileNotFoundError:
                continue

            projectpath = projects[idx]

            # prefer when we have both editor and project paths
            if editorfolder and projectpath:
                break

        # If process detection didn't work, try to find the Engine associated with the running Switchboard
        if not editorfolder:
            try:
                editorfolder = self.find_upstream_path_with_name(os.path.abspath(__file__), "Engine")
            except FileNotFoundError:
                pass

        # If we detected an engine, populate from it.
        if editorfolder:
            self.engine_dir_line_edit.setText(editorfolder)
            self.uproject_line_edit.setText(projectpath)

        # Suggest the config name based on the project name
        if projectpath:
            self.name_line_edit.setText(pathlib.PurePath(projectpath).stem)


    def detect_running_projects(self):
        ''' Detects a running UnrealEngine editor and its project '''

        editors = []
        projects = []

        # Not detecting Debug runs since it is not common and would increase detection time
        # At some point this might need UE6Editor added.
        UEnames = ['UE4Editor', 'UE5Editor']

        if sys.platform.startswith('win'):
            for UEname in UEnames:
                # Windows UE Editors will have .exe extension
                UEname += '.exe'

                # Relying on wmic for this. Another option is to use psutil which is cross-platform, and slower.
                cmd = f'wmic process where caption="{UEname}" get commandline'

                for line in subprocess.check_output(cmd).decode().splitlines():
                    if UEname.lower() not in line.lower():
                        continue
                    
                    # split the cmdline as a list of the original arguments
                    try:
                        argv = shlex.split(line)
                    except ValueError:
                        continue

                    # There should be at least 2 arguments, the executable and the project.
                    if len(argv) < 2:
                        continue
                    
                    editorpath = argv[0]
                    projectpath = argv[1]

                    if editorpath.lower().endswith(UEname.lower()):
                        editors.append(editorpath)
                    else:
                        editors.append('')

                    if projectpath.lower().endswith('.uproject'):
                        projects.append(projectpath)
                    else:
                        projects.append('')

        return editors,projects

    def p4_settings(self):
        settings = {}
        settings['p4_enabled'] = self.p4_group.isChecked()
        settings['p4_workspace_name'] = self.p4_workspace_line_edit.text() if self.p4_group.isChecked() else None
        settings['p4_project_path'] = self.p4_project_path_line_edit.text() if self.p4_group.isChecked() else None
        settings['p4_engine_path'] = self.p4_engine_path_line_edit.text() if self.p4_group.isChecked() else None
        return settings

    def on_name_changed(self, text):
        self.config_name = text
        self.update_button_box()

    def on_uproject_changed(self, text):
        self.uproject = os.path.normpath(text)

        # Update the Engine directory with a best guess, if it is empty
    
        # Try going up the path until you find Engine in a sibling folder. This will custom build p4 setups
        if not self.engine_dir_line_edit.text():
            try:
                self.engine_dir_line_edit.setText(self.find_upstream_path_with_name(self.uproject, 'Engine', includeSiblings=True))
            except FileNotFoundError:
                pass

        self.update_button_box()

    def on_browse_uproject_path(self, uproject_search_path):
        self.uproject, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select uProject file", self.engine_dir, "uProject (*.uproject)")
        self.uproject = os.path.normpath(self.uproject)
        self.uproject_line_edit.setText(self.uproject)

    def on_engine_dir_changed(self, text):
        self.engine_dir = os.path.normpath(text)
        self.update_button_box()

    def on_browse_engine_dir(self):
        self.engine_dir = QtWidgets.QFileDialog.getExistingDirectory(self, "Select UE4 engine directory")
        self.engine_dir = os.path.normpath(self.engine_dir)
        self.engine_dir_line_edit.setText(self.engine_dir)

    def update_button_box(self):
        self.button_box.button(QtWidgets.QDialogButtonBox.Ok).setEnabled(False)
        if self.config_name and self.uproject and self.engine_dir:
            if os.path.exists(self.uproject) and os.path.exists(self.engine_dir):
                self.button_box.button(QtWidgets.QDialogButtonBox.Ok).setEnabled(True)
