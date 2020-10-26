# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard import message_protocol
from switchboard.config import CONFIG, Setting, SETTINGS
from switchboard.devices.device_base import Device
from switchboard.devices.device_widget_base import AddDeviceDialog
from switchboard.devices.unreal.plugin_unreal import DeviceUnreal, DeviceWidgetUnreal
from switchboard.switchboard_logging import LOGGER

from .ndisplay_monitor_ui import nDisplayMonitorUI
from .ndisplay_monitor    import nDisplayMonitor

from PySide2 import QtWidgets

import os, traceback
from pathlib import Path


class AddnDisplayDialog(AddDeviceDialog):
    def __init__(self, existing_devices, parent=None):
        super().__init__(device_type="nDisplay", existing_devices=existing_devices, parent=parent)

        self.config_file_field = QtWidgets.QLineEdit(self)
        self.config_file_button = QtWidgets.QPushButton(self, text="Browse")
        self.config_file_button.clicked.connect(lambda: self.on_clicked_browse_file())
        file_selection_layout = QtWidgets.QHBoxLayout()
        file_selection_layout.addWidget(self.config_file_field)
        file_selection_layout.addItem(QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum))
        file_selection_layout.addWidget(self.config_file_button)

        # remove these rows as we don't need them
        self.form_layout.removeRow(self.name_field)
        self.form_layout.removeRow(self.ip_field)
        self.name_field = None
        self.ip_field = None

        self.existing_ndisplay_devices = []
        for device in existing_devices:
            if device.device_type == "nDisplay":
                self.existing_ndisplay_devices.append(device)

        if self.existing_ndisplay_devices:
            self.layout().addWidget(QtWidgets.QLabel("Warning! All existing nDisplay devices will be replaced."))

        self.form_layout.addRow("nDisplay Config", file_selection_layout)

    def result(self):
        res = super().result()
        if res == QtWidgets.QDialog.Accepted:
            config_path = self.config_file_field.text().replace('"', '').strip()
            DevicenDisplay.csettings['ndisplay_config_file'].update_value(config_path)
        return res

    def on_clicked_browse_file(self):
        start_path = str(Path.home())
        if SETTINGS.LAST_BROWSED_PATH and os.path.exists(SETTINGS.LAST_BROWSED_PATH):
            start_path = SETTINGS.LAST_BROWSED_PATH
        cfg_path, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select nDisplay config file", start_path, "nDisplay Config (*.cfg)")
        if len(cfg_path) > 0 and os.path.exists(cfg_path):
            self.config_file_field.setText(cfg_path)
            SETTINGS.LAST_BROWSED_PATH = os.path.dirname(cfg_path)
            SETTINGS.save()

    def devices_to_add(self):
        cfg_file = self.config_file_field.text().replace('"', '')
        try:
            devices = DevicenDisplay.parse_config(cfg_file)
            if len(devices) == 0:
                LOGGER.error(f"Could not read any devices from nDisplay config file {cfg_file}")
            return devices
        except (IndexError, KeyError):
            LOGGER.error(f"Error parsing nDisplay config file {cfg_file}")
            return []

    def devices_to_remove(self):
        return self.existing_ndisplay_devices

class DeviceWidgetnDisplay(DeviceWidgetUnreal):
    pass

class DevicenDisplay(DeviceUnreal):

    add_device_dialog = AddnDisplayDialog
    
    csettings = {
        'ndisplay_config_file': Setting(
            attr_name="ndisplay_cfg_file", 
            nice_name="nDisplay Config File", 
            value="", 
            tool_tip="Path to nDisplay config file"
        ),
        'use_all_available_cores': Setting(
            attr_name="use_all_available_cores", 
            nice_name="Use All Available Cores", 
            value=False,
        ),
        'texture_streaming': Setting(
            attr_name="texture_streaming", 
            nice_name="Texture Streaming", 
            value=True,
        ),
        'render_api': Setting(
            attr_name="render_api", 
            nice_name="Render API", 
            value="dx12", 
            possible_values=["dx11", "dx12"],
        ),
        'render_mode': Setting(
            attr_name="render_mode", 
            nice_name="Render Mode", 
            value="Mono", 
            possible_values=["Mono", "Frame sequential", "Side-by-Side", "Top-bottom"]
        ),
        'ndisplay_cmd_args': Setting(
            attr_name="ndisplay_cmd_args", 
            nice_name="Extra Cmd Line Args", 
            value="",
        ),
        'ndisplay_exec_cmds': Setting(
            attr_name="ndisplay_exec_cmds", 
            nice_name='ExecCmds', 
            value="", 
            tool_tip=f'ExecCmds to be passed. No need for outer double quotes.',
        ),
    }

    ndisplay_monitor_ui = None
    ndisplay_monitor = None

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.settings = {
            'ue_command_line': Setting(
                attr_name="ue_command_line", 
                nice_name="UE Command Line", 
                value=kwargs.get("ue_command_line", ''), 
                show_ui=True
            ),
            'window_position': Setting(
                attr_name="window_position", 
                nice_name="Window Position", 
                value=tuple(kwargs.get("window_position", (0,0))), 
                show_ui=False
            ),
            'window_resolution': Setting(
                attr_name="window_resolution", 
                nice_name="Window Resolution", 
                value=tuple(kwargs.get("window_resolution", (100,100))), show_ui=False
            ),
            'fullscreen': Setting(
                attr_name="fullscreen", 
                nice_name="fullscreen", 
                value=kwargs.get("fullscreen", False), show_ui=False
            ),
        }

        self.render_mode_cmdline_opts = {
            "Mono"            : "-dc_dev_mono", 
            "Frame sequential": "-quad_buffer_stereo", 
            "Side-by-Side"    : "-dc_dev_side_by_side", 
            "Top-bottom"      : "-dc_dev_top_bottom"
        }

        self.path_to_config_on_host = DevicenDisplay.csettings['ndisplay_config_file'].get_value()

        if len(self.settings['ue_command_line'].get_value()) == 0:
            self.generate_unreal_command_line()

        CONFIG.ENGINE_DIR.signal_setting_overriden.connect(self.on_cmdline_affecting_override)
        CONFIG.ENGINE_DIR.signal_setting_changed.connect(self.on_change_setting_affecting_command_line)
        CONFIG.UPROJECT_PATH.signal_setting_overriden.connect(self.on_cmdline_affecting_override)
        CONFIG.UPROJECT_PATH.signal_setting_changed.connect(self.on_change_setting_affecting_command_line)

        # common settings affect instance command line
        for csetting in self.__class__.plugin_settings():
            csetting.signal_setting_changed.connect(self.on_change_setting_affecting_command_line)

        self.unreal_client.send_file_completed_delegate = self.on_ndisplay_config_transfer_complete
        self.unreal_client.delegates['get sync status'] = self.on_get_sync_status

        # create monitor if it doesn't exist
        self.__class__.create_monitor_if_necessary()

    @classmethod
    def create_monitor_if_necessary(cls):
        ''' Creates the nDisplay Monitor if it doesn't exist yet.
        '''
        if not cls.ndisplay_monitor:
            cls.ndisplay_monitor = nDisplayMonitor(None)

        return cls.ndisplay_monitor

    def on_change_setting_affecting_command_line(self, oldval, newval):
        self.generate_unreal_command_line()

    @classmethod
    def plugin_settings(cls):
        ''' Returns common settings that belong to all devices of this class.
        '''
        return [DeviceUnreal.csettings['port'], DeviceUnreal.csettings['roles_filename']] + list(cls.csettings.values())

    def device_settings(self):
        ''' This is given to the config, so that it knows to save them when they change.
        settings_dialog.py will try to create a UI for each setting if setting.show_ui is True.
        '''
        return super().device_settings() + list(self.settings.values())

    def setting_overrides(self):
        return [
            DevicenDisplay.csettings['ndisplay_cmd_args'],
            DevicenDisplay.csettings['ndisplay_exec_cmds'],
            CONFIG.ENGINE_DIR, 
            CONFIG.SOURCE_CONTROL_WORKSPACE, 
            CONFIG.UPROJECT_PATH,
        ]

    def on_cmdline_affecting_override(self, device_name):
        if self.name == device_name:
            self.generate_unreal_command_line()

    @property
    def category_name(self):
        return "nDisplay"

    def generate_unreal_command_line(self, map_name=""):

        uproject = os.path.normpath(CONFIG.UPROJECT_PATH.get_value(self.name))
        additional_args = self.csettings['ndisplay_cmd_args'].get_value(self.name)

        cfg_file = self.path_to_config_on_host

        win_pos = self.settings['window_position'].get_value()
        win_res = self.settings['window_resolution'].get_value()
        fullscreen = self.settings['fullscreen'].get_value()

        render_mode = self.render_mode_cmdline_opts[DevicenDisplay.csettings['render_mode'].get_value(self.name)]
        render_api = f"-{DevicenDisplay.csettings['render_api'].get_value(self.name)}"
        use_all_cores = "-useallavailablecores" if DevicenDisplay.csettings['use_all_available_cores'].get_value(self.name) else ""
        no_texture_streaming = "-notexturestreaming" if not DevicenDisplay.csettings['texture_streaming'].get_value(self.name) else ""

        ini_engine = "-ini:Engine"\
            ":[/Script/Engine.Engine]:GameEngine=/Script/DisplayCluster.DisplayClusterGameEngine"\
            ",[/Script/Engine.Engine]:GameViewportClientClassName=/Script/DisplayCluster.DisplayClusterViewportClient"

        ini_game = "-ini:Game:[/Script/EngineSettings.GeneralProjectSettings]:bUseBorderlessWindow=True"

        # fill in fixed arguments

        args = [
            f'"{uproject}"',
            f'{map_name}',              # map to open
            "-game",                    # render nodes run in -game
            "-messaging",               # enables messaging, needed for MultiUser
            "-dc_cluster",              # this is a cluster node
            "-nosplash",                # avoids splash screen
            "-fixedseed",               # for determinism
            "-NoVerifyGC",              # improves performance
            "-noxrstereo",              # avoids a conflict with steam/oculus
            f'{additional_args}',       # specified in settings
            f'-dc_cfg="{cfg_file}"',    # nDisplay config file
            f'{render_api}',            # dx11/12
            f'{render_mode}',           # mono/...
            f'{use_all_cores}',         # -useallavailablecores
            f'{no_texture_streaming}',  # -notexturestreaming
            f'-dc_node={self.name}',    # name of this node in the nDisplay cluster
            f'Log={self.name}.log',     # log file
            f'{ini_engine}',            # Engine ini injections
            f'{ini_game}',              # Game ini injections
        ]

        # fill in ExecCmds

        exec_cmds = f'{self.csettings["ndisplay_exec_cmds"].get_value(self.name)}'.strip().split(';')
        exec_cmds.append('DisableAllScreenMessages')
        exec_cmds = [cmd for cmd in exec_cmds if len(cmd.strip())]

        if len(exec_cmds):
            exec_cmds_expanded = ';'.join(exec_cmds)
            args.append(f'ExecCmds="{exec_cmds_expanded}"')

        # when in fullscreen, the window parameters should not be passed

        if fullscreen:
            args.extend([
                'fullscreen=true',
            ])
        else:
            args.extend([
                f'-windowed',
                f'-forceres',
                f'WinX={win_pos[0]}',
                f'WinY={win_pos[1]}',
                f'ResX={win_res[0]}',
                f'ResY={win_res[1]}',
            ])

        # MultiUser parameters

        if CONFIG.MUSERVER_AUTO_JOIN:
            args.extend([
                f'-CONCERTRETRYAUTOCONNECTONERROR',
                f'-CONCERTAUTOCONNECT', 
                f'-CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME}',
                f'-CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME}', 
                f'-CONCERTDISPLAYNAME={self.name}',
                f'-CONCERTISHEADLESS',
            ])

        # TODO: Make logs optional and selectable
        #args += f' -LogCmds="LogDisplayClusterPlugin Log, LogDisplayClusterEngine Log, LogDisplayClusterConfig Log, LogDisplayClusterCluster Log, LogDisplayClusterGame Log, LogDisplayClusterGameMode Log, LogDisplayClusterInput Log, LogDisplayClusterInputVRPN Log, LogDisplayClusterNetwork Log, LogDisplayClusterNetworkMsg Log, LogDisplayClusterRender Log, LogDisplayClusterRenderSync Log, LogDisplayClusterBlueprint Log" '

        path_to_exe = self.generate_unreal_exe_path()
        args_expanded = ' '.join(args)
        self.settings['ue_command_line'].update_value(f"{path_to_exe} {args_expanded}")

        return path_to_exe, args_expanded

    def on_ndisplay_config_transfer_complete(self, destination):
        LOGGER.info(f"{self.name}: nDisplay config file was successfully transferred to {destination} on host")
        self.path_to_config_on_host = destination
        super().launch(map_name=self.map_name_to_launch)

    def on_get_sync_status(self, message):
        ''' Called when 'get sync status' is received.
        '''
        self.__class__.ndisplay_monitor.on_get_sync_status(device=self, message=message)

    @classmethod
    def parse_config(cls, cfg_file):

        nodes = []

        cluster_node_lines = []
        window_lines = []
        with open(cfg_file, 'r') as cfg:
            for line in cfg:
                line = line.strip().lower()
                if not line.startswith('#'):
                    if "[cluster_node]" in line:
                        cluster_node_lines.append(line)
                    elif "[window]" in line:
                        window_lines.append(line)

        for line in cluster_node_lines:
            name = line.split("id=")[1]
            name = name.split(' ', 1)[0]
            name = name.replace('"', '')
            
            node_window = line.split("window=")[1]
            node_window = node_window.split(' ', 1)[0]
            node_window = node_window.replace('"', '')

            kwargs = {"ue_command_line": ""}

            for window_line in window_lines:
                if node_window in window_line:
                    try:
                        winx = window_line.split("winx=")[1]
                        winx = winx.split(' ', 1)[0]
                        winx = winx.replace('"', '')
                    except IndexError:
                        winx = 0

                    try:
                        winy = window_line.split("winy=")[1]
                        winy = winy.split(' ', 1)[0]
                        winy = winy.replace('"', '')
                    except IndexError:
                        winy = 0

                    try:
                        resx = window_line.split("resx=")[1]
                        resx = resx.split(' ', 1)[0]
                        resx = resx.replace('"', '')
                    except IndexError:
                        resx = 0

                    try:
                        resy = window_line.split("resy=")[1]
                        resy = resy.split(' ', 1)[0]
                        resy = resy.replace('"', '')
                    except IndexError:
                        resy = 0

                    try:
                        fullscreen = window_line.split("fullscreen=")[1]
                        fullscreen = fullscreen.split(' ', 1)[0]
                        fullscreen = True if (('true' in fullscreen) or (fullscreen == "1")) else False
                    except IndexError:
                        fullscreen = False

                    kwargs["window_position"] = (int(winx), int(winy))
                    kwargs["window_resolution"] = (int(resx), int(resy))
                    kwargs["fullscreen"] = fullscreen

                    break

            addr = line.split("addr=")[1]
            addr = addr.split(' ', 1)[0]
            addr = addr.replace('"', '')

            nodes.append({"name": name, "ip_address": addr, "kwargs": kwargs})

        return nodes

    def update_settings_controlled_by_config(self, cfg_file):
        ''' Updates settings that are exclusively controlled by the config file
        '''

        nodes = self.__class__.parse_config(cfg_file)

        # find which node is self:
        menode = next(node for node in nodes if node['name'] == self.name)

        if not menode:
            LOGGER.error(f"{self.name} not found in config file {cfg_file}")
            return

        self.settings['window_position'].update_value(menode['kwargs'].get("window_position", (0.0)))
        self.settings['window_resolution'].update_value(menode['kwargs'].get("window_resolution", (100,100)))
        self.settings['fullscreen'].update_value(menode['kwargs'].get("fullscreen", False))

    def launch(self, map_name):

        self.map_name_to_launch = map_name

        # Update settings controlled exclusively by the nDisplay config file.
        try:
            cfg_file = DevicenDisplay.csettings['ndisplay_config_file'].get_value(self.name)
            self.update_settings_controlled_by_config(cfg_file)
        except:
            LOGGER.error(f"{self.name}: Could not update from '{cfg_file}' before launch. \n\n=== Traceback BEGIN ===\n{traceback.format_exc()}=== Traceback END ===\n")
            self.widget._close()
            return

        # Transfer config file

        source = cfg_file
        destination = "%TEMP%/ndisplay/%RANDOM%.cfg"

        if os.path.exists(source):
            self._last_issued_command_id, msg = message_protocol.create_send_file_message(source, destination)
            self.unreal_client.send_message(msg)
        else:
            LOGGER.error(f"{self.name}: Could not find nDisplay config file at {source}")
            self.widget._close()

    @classmethod
    def plug_into_ui(cls, menubar, tabs):
        ''' Implementation of base class function that allows plugin to inject UI elements.
        '''

        cls.create_monitor_if_necessary()

        # Create Monitor UI if it doesn't exist
        if not cls.ndisplay_monitor_ui:
            cls.ndisplay_monitor_ui = nDisplayMonitorUI(parent=tabs, monitor=cls.ndisplay_monitor)

        # Add our monitor UI to the main tabs in the UI
        tabs.addTab(cls.ndisplay_monitor_ui, 'nDisplay &Monitor')

    @classmethod
    def added_device(cls, device):
        ''' Implementation of base class function. Called when one of our plugin devices has een added.
        '''

        if not cls.ndisplay_monitor:
            return

        cls.ndisplay_monitor.added_device(device)

    @classmethod
    def removed_device(cls, device):
        ''' Implementation of base class function. Called when one of our plugin devices has been removed.
        '''

        if not cls.ndisplay_monitor:
            return

        cls.ndisplay_monitor.removed_device(device)

