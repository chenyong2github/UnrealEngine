# Copyright Epic Games, Inc. All Rights Reserved.
from switchboard import message_protocol
from switchboard.config import CONFIG, Setting, SETTINGS
from switchboard.devices.device_base import Device
from switchboard.devices.device_widget_base import AddDeviceDialog
from switchboard.devices.unreal.plugin_unreal import DeviceUnreal, DeviceWidgetUnreal
from switchboard.switchboard_logging import LOGGER

from PySide2 import QtWidgets

import os
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
            DevicenDisplay.setting_ndisplay_config_file.update_value(self.config_file_field.text())
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
        cfg_file = self.config_file_field.text()
        try:
            devices = self._parse_config(cfg_file)
            if len(devices) == 0:
                LOGGER.error(f"Could not read any devices from nDisplay config file {cfg_file}")
            return devices
        except (IndexError, KeyError):
            LOGGER.error(f"Error parsing nDisplay config file {cfg_file}")
            return []

    def _parse_config(self, cfg_file):
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

                    kwargs["window_position"] = (int(winx), int(winy))
                    kwargs["window_resolution"] = (int(resx), int(resy))
                    break

            addr = line.split("addr=")[1]
            addr = addr.split(' ', 1)[0]
            addr = addr.replace('"', '')
            nodes.append({"name": name, "ip_address": addr, "kwargs": kwargs})
        return nodes

    def devices_to_remove(self):
        return self.existing_ndisplay_devices

class DeviceWidgetnDisplay(DeviceWidgetUnreal):
    pass

class DevicenDisplay(DeviceUnreal):

    add_device_dialog = AddnDisplayDialog
    setting_ndisplay_config_file = Setting("ndisplay_cfg_file", "nDisplay Config File", "", tool_tip="Path to nDisplay config file")
    setting_use_all_available_cores = Setting("use_all_available_cores", "Use All Available Cores", False)
    setting_texture_streaming = Setting("texture_streaming", "Texture Streaming", True)
    setting_render_api = Setting("render_api", "Render API", "dx12", possible_values=["dx11", "dx12"])
    setting_render_mode = Setting("render_mode", "Render Mode", "Mono", possible_values=["Mono", "Frame sequential", "Side-by-Side", "Top-bottom"])

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.setting_ue_command_line = Setting("ue_command_line", "UE Command Line", kwargs["ue_command_line"])
        self.setting_window_position = Setting("window_position", "Window Position", tuple(kwargs["window_position"]))
        self.setting_window_resolution = Setting("window_resolution", "Window Resolution", tuple(kwargs["window_resolution"]))

        self.render_mode_cmdline_opts = {"Mono": "-dc_dev_mono", "Frame sequential": "-quad_buffer_stereo", "Side-by-Side": "-dc_dev_side_by_side", "Top-bottom": "-dc_dev_top_bottom"}
        self.path_to_config_on_host = DevicenDisplay.setting_ndisplay_config_file.get_value()
        if len(self.setting_ue_command_line.get_value()) == 0:
            self.generate_unreal_command_line()

        super().setting_command_line_arguments.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        CONFIG.ENGINE_DIR.signal_setting_overriden.connect(self.on_cmdline_affecting_override)
        CONFIG.ENGINE_DIR.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        CONFIG.UPROJECT_PATH.signal_setting_overriden.connect(self.on_cmdline_affecting_override)
        CONFIG.UPROJECT_PATH.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        self.setting_ndisplay_config_file.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        self.setting_use_all_available_cores.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        self.setting_texture_streaming.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        self.setting_render_api.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())
        self.setting_render_mode.signal_setting_changed.connect(lambda: self.generate_unreal_command_line())

        self.unreal_client.send_file_completed_delegate = self.on_ndisplay_config_transfer_complete

    @staticmethod
    def plugin_settings():
        return [DeviceUnreal.setting_port,
            DeviceUnreal.setting_command_line_arguments,
            DevicenDisplay.setting_ndisplay_config_file,
            DevicenDisplay.setting_use_all_available_cores,
            DevicenDisplay.setting_texture_streaming,
            DevicenDisplay.setting_render_api,
            DevicenDisplay.setting_render_mode]

    def device_settings(self):
        return super().device_settings() + [self.setting_ue_command_line, self.setting_window_resolution, self.setting_window_position]

    def setting_overrides(self):
        return [CONFIG.ENGINE_DIR, CONFIG.BUILD_ENGINE, CONFIG.SOURCE_CONTROL_WORKSPACE, CONFIG.UPROJECT_PATH]

    def on_cmdline_affecting_override(self, device_name):
        if self.name == device_name:
            self.generate_unreal_command_line()

    @property
    def category_name(self):
        return "nDisplay"

    def generate_unreal_command_line(self, map_name=""):
        uproject = os.path.normpath(CONFIG.UPROJECT_PATH.get_value(self.name))
        additional_args = self.setting_command_line_arguments.get_value(self.name)
        cfg_file = self.path_to_config_on_host
        win_pos = self.setting_window_position.get_value()
        win_res = self.setting_window_resolution.get_value()
        render_mode = self.render_mode_cmdline_opts[self.setting_render_mode.get_value()]
        render_api = f"-{self.setting_render_api.get_value()}"
        use_all_cores = " -useallavailablecores" if self.setting_use_all_available_cores.get_value() else ""
        no_texture_streaming = " -notexturestreaming" if not self.setting_texture_streaming.get_value() else ""

        args = f'"{uproject}" {additional_args} -game -messaging -dc_cluster -nosplash -fixedseed dc_cfg="{cfg_file}" {render_api} {render_mode}{use_all_cores}{no_texture_streaming} '
        args += f'-windowed WinX={win_pos[0]} WinY={win_pos[1]} ResX={win_res[0]} ResY={win_res[1]} dc_node={self.name} Log={self.name}.log '
        if CONFIG.MUSERVER_AUTO_JOIN:
            args += f'-CONCERTAUTOCONNECT -CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME} -CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME} -CONCERTDISPLAYNAME={self.name}'
        args += f'-LogCmds="LogDisplayClusterPlugin Log, LogDisplayClusterEngine Log, LogDisplayClusterConfig Log, LogDisplayClusterCluster Log, LogDisplayClusterGame Log, LogDisplayClusterGameMode Log, LogDisplayClusterInput Log, LogDisplayClusterInputVRPN Log, LogDisplayClusterNetwork Log, LogDisplayClusterNetworkMsg Log, LogDisplayClusterRender Log, LogDisplayClusterRenderSync Log, LogDisplayClusterBlueprint Log"'

        path_to_exe = self.generate_unreal_exe_path()
        self.setting_ue_command_line.update_value(f"{path_to_exe} {args}")
        return path_to_exe, args

    def on_ndisplay_config_transfer_complete(self, destination):
        LOGGER.info(f"{self.name}: nDisplay config file was successfully transferred to {destination} on host")
        self.path_to_config_on_host = destination
        map_name = ""
        super().launch(map_name)

    def launch(self, map_name):
        destination = "%TEMP%/ndisplay/%RANDOM%.cfg"
        source = self.setting_ndisplay_config_file.get_value()
        if os.path.exists(source):
            self._last_issued_command_id, msg = message_protocol.create_send_file_message(source, destination)
            self.unreal_client.send_message(msg)
        else:
            LOGGER.error(f"{self.name}: Could not find nDisplay config file at {source}")
            self.widget._close()
