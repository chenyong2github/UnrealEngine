# Copyright Epic Games, Inc. All Rights Reserved.
from switchboard import config_osc as osc
from switchboard import message_protocol, p4_utils
from switchboard.config import CONFIG, Setting, SETTINGS, DEFAULT_MAP_TEXT
from switchboard.devices.device_base import Device, DeviceStatus
from switchboard.devices.device_widget_base import DeviceWidget
from switchboard.listener_client import ListenerClient
from switchboard.switchboard_logging import LOGGER

from PySide2 import QtWidgets, QtGui, QtCore

import os, base64
from pathlib import Path


class DeviceUnreal(Device):

    csettings = {
        'buffer_size': Setting(
            attr_name="buffer_size", 
            nice_name="Buffer Size", 
            value=1024, 
            tool_tip="Buffer size used for communication with SwitchboardListener",
        ),
        'command_line_arguments': Setting(
            attr_name="command_line_arguments", 
            nice_name='Command Line Arguments', 
            value="", 
            tool_tip=f'Additional command line arguments for the engine',
        ),
        'exec_cmds': Setting(
            attr_name="exec_cmds", 
            nice_name='ExecCmds', 
            value="", 
            tool_tip=f'ExecCmds to be passed. No need for outer double quotes.',
        ),
        'port': Setting(
            attr_name="port", 
            nice_name="Listener Port", 
            value=2980, 
            tool_tip="Port of SwitchboardListener"
        ),
        'roles_filename': Setting(
            attr_name="roles_filename", 
            nice_name="Roles Filename", 
            value="VPRoles.ini", 
            tool_tip="File that stores VirtualProduction roles. Default: Config/Tags/VPRoles.ini",
        ),
        'stage_session_id': Setting(
            attr_name="stage_session_id", 
            nice_name="Stage Session ID",
            value=0,
        ),
        'ue4_exe': Setting(
            attr_name="editor_exe", 
            nice_name="UE4 Editor filename", 
            value="UE4Editor.exe",
        ),
    }

    unreal_started_signal = QtCore.Signal()

    def __init__(self, name, ip_address, **kwargs):
        super().__init__(name, ip_address, **kwargs)

        self.unreal_client = ListenerClient(
            ip_address=self.ip_address, 
            port=DeviceUnreal.csettings['port'].get_value(self.name), 
            buffer_size=DeviceUnreal.csettings['buffer_size'].get_value(self.name)
        )

        roles = kwargs.get("roles", [])

        self.setting_roles = Setting(
            attr_name="roles", 
            nice_name="Roles", 
            value=roles, 
            possible_values=[], 
            tool_tip="List of roles for this device"
        )

        self.setting_ip_address.signal_setting_changed.connect(self.on_setting_ip_address_changed)
        DeviceUnreal.csettings['port'].signal_setting_changed.connect(self.on_setting_port_changed)
        CONFIG.BUILD_ENGINE.signal_setting_changed.connect(self.on_build_engine_changed)

        self.auto_connect = False
        self.start_build_after_sync = False

        self.inflight_project_cl = None
        self.inflight_engine_cl = None
        self.scheduled_sync_operations = {}

        # Set a delegate method if the device gets a disconnect signal
        self.unreal_client.disconnect_delegate = self.on_listener_disconnect
        self.unreal_client.program_started_delegate = self.on_program_started
        self.unreal_client.program_start_failed_delegate = self.on_program_start_failed
        self.unreal_client.program_ended_delegate = self.on_program_ended
        self.unreal_client.program_kill_failed_delegate = self.on_program_kill_failed
        self.unreal_client.receive_file_completed_delegate = self.on_file_received
        self.unreal_client.receive_file_failed_delegate = self.on_file_receive_failed
        self.unreal_client.delegates["state"] = self.on_listener_state
        self.unreal_client.delegates["kill"] = self.on_listener_kill

        self._remote_programs_start_queue = {} # key: message_id, name
        self._running_remote_programs = {} # key: program id, value: program name

        self.osc_connection_timer = QtCore.QTimer(self)
        self.osc_connection_timer.timeout.connect(self._try_connect_osc)

        # on_program_started is called from inside the connection thread but QTimer can only be used from the main thread.
        # so we connect to a signal sent on the connection threat and can then start the timer on the main thread.
        self.unreal_started_signal.connect(self.on_unreal_started)

    @classmethod
    def plugin_settings(cls):
        return Device.plugin_settings() + list(DeviceUnreal.csettings.values())

    def setting_overrides(self):
        return super().setting_overrides() + [
            Device.csettings['is_recording_device'],
            DeviceUnreal.csettings['command_line_arguments'],
            DeviceUnreal.csettings['exec_cmds'],
            CONFIG.ENGINE_DIR,
            CONFIG.SOURCE_CONTROL_WORKSPACE,
            CONFIG.UPROJECT_PATH,
        ]

    def device_settings(self):
        return super().device_settings() + [self.setting_roles]

    @property
    def category_name(self):
        if self.is_recording_device and self.status >= DeviceStatus.READY:
            return "Recording"
        return "Multiuser"

    def on_setting_ip_address_changed(self, _, new_address):
        LOGGER.info(f"Updating IP address for ListenerClient to {new_address}")
        self.unreal_client.ip_address = new_address

    def on_setting_port_changed(self, _, new_port):
        if not DeviceUnreal.csettings['port'].is_overriden(self.name):
            LOGGER.info(f"Updating Port for ListenerClient to {new_port}")
            self.unreal_client.port = new_port

    def on_build_engine_changed(self, _, build_engine):
        if build_engine:
            self.widget.engine_changelist_label.show()
            if self.status != DeviceStatus.DISCONNECTED:
                self._request_engine_changelist_number()
        else:
            self.widget.engine_changelist_label.hide()

    def set_slate(self, value):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.SLATE, value)

    def set_take(self, value):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.TAKE, value)

    def record_stop(self):
        if not self.is_recording_device:
            return

        self.send_osc_message(osc.RECORD_STOP, 1)

    def connect_listener(self):
        is_connected = self.unreal_client.connect()

        if not is_connected:
            self.device_qt_handler.signal_device_connect_failed.emit(self)
            return

        super().connect_listener()

        self._request_roles_file()
        self._request_project_changelist_number()
        if CONFIG.BUILD_ENGINE.get_value():
            self._request_engine_changelist_number()

    def _request_roles_file(self):
        uproject_path = CONFIG.UPROJECT_PATH.get_value(self.name)
        roles_filename = DeviceUnreal.csettings["roles_filename"].get_value(self.name)
        roles_file_path = os.path.join(os.path.dirname(uproject_path), "Config", "Tags", roles_filename)
        _, msg = message_protocol.create_copy_file_from_listener_message(roles_file_path)
        self.unreal_client.send_message(msg)

    def _request_project_changelist_number(self):
        if not CONFIG.P4_ENABLED.get_value():
            return
        client_name = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)
        p4_path = CONFIG.P4_PATH.get_value()

        formatstring = "%change%"
        args = f'-F "{formatstring}" -c {client_name} cstat {p4_path}/...#have'

        program_name = "cstat_project"
        mid, msg = message_protocol.create_start_process_message(prog_path="p4", prog_args=args, prog_name=program_name, caller=self.name)
        self._remote_programs_start_queue[mid] = program_name

        self.unreal_client.send_message(msg)

    def _request_engine_changelist_number(self):
        if not CONFIG.P4_ENABLED.get_value():
            return
        client_name = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)
        p4_path = p4_utils.p4_where(client_name, CONFIG.ENGINE_DIR.get_value(self.name))
        if not p4_path:
            LOGGER.warning(f'{self.name}: "Build Engine" is enabled in the settings but the engine does not seem to be under perforce control.')
            LOGGER.warning("Please check your perforce settings.")
            return

        formatstring = "%change%"
        args = f'-F "{formatstring}" -c {client_name} cstat {p4_path}/...#have'

        program_name = "cstat_engine"
        mid, msg = message_protocol.create_start_process_message(prog_path="p4", prog_args=args, prog_name=program_name, caller=self.name)
        self._remote_programs_start_queue[mid] = program_name

        self.unreal_client.send_message(msg)

    def disconnect_listener(self):
        super().disconnect_listener()
        self.unreal_client.disconnect()

    def sync(self, engine_cl, project_cl):
        if not engine_cl and not project_cl:
            LOGGER.warning(f"Neither project nor engine changelist is selected. There is nothing to sync!")
            return

        if engine_cl:
            self._sync_engine(engine_cl)
            self.status = DeviceStatus.SYNCING

        if project_cl:
            self._sync_project(project_cl)
            if self.status != DeviceStatus.SYNCING:
                self.status = DeviceStatus.SYNCING

    def _sync_engine(self, engine_cl):
        project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
        LOGGER.info(f"{self.name}: Syncing engine of project {project_name} to revision {engine_cl}")
        self.inflight_engine_cl = engine_cl
        # when no uproject is specified to the UAT SyncProject script, it will go into EngineOnly mode and only sync the engine
        # as well as all loose files one directoy up from the engine directory.
        sync_tool = f'{os.path.normpath(os.path.join(CONFIG.ENGINE_DIR.get_value(self.name), "Build", "BatchFiles", "RunUAT.bat"))}'
        sync_args = f'-P4 SyncProject -cl={engine_cl} -threads=8 -generate'

        LOGGER.info(f"{self.name}: Sending engine sync command: {sync_tool} {sync_args}")
        program_name = "sync_engine"
        mid, msg = message_protocol.create_start_process_message(prog_path=sync_tool, prog_args=sync_args, prog_name=program_name, caller=self.name)
        self._remote_programs_start_queue[mid] = program_name
        self.unreal_client.send_message(msg)

    def _sync_project(self, project_cl):
        project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
        LOGGER.info(f"{self.name}: Syncing project {project_name} to revision {project_cl}")
        self.inflight_project_cl = project_cl

        sync_tool = ""
        sync_args = ""

        # todo: UAT has been fixed to work from vanilla engine now. though it requires help finding the correct workspace.
        # the only way this can be done is by setting the environment variable uebp_CLIENT to whatever we have in CONFIG.SOURCE_CONTROL_WORKSPACE.
        # however that needs to happen on the listener machines, so we would need a way to (platform-independently) set env variables when
        # running commands and extend the protocol to include setting env vars as well.
        # alternatively we could try running commands through cmd /V /C "set uebp_CLIENT=CONFIG.SOURCE_CONTROL_WORKSPACE&& EXE ARGS"
        # but that would only work for Windows and thus require us to know what the listener machine's OS is.
        engine_under_source_control = CONFIG.BUILD_ENGINE.get_value()

        if engine_under_source_control:
            sync_tool = f'{os.path.normpath(os.path.join(CONFIG.ENGINE_DIR.get_value(self.name), "Build", "BatchFiles", "RunUAT.bat"))}'
            sync_args = f'-P4 SyncProject -project="{CONFIG.UPROJECT_PATH.get_value(self.name)}" -projectonly -cl={project_cl} -threads=8 -generate'
        else:
            # for installed/vanilla engine we directly call p4 to sync the project itself.
            p4_path = CONFIG.P4_PATH.get_value()
            workspace = CONFIG.SOURCE_CONTROL_WORKSPACE.get_value(self.name)
            sync_tool = "p4"
            sync_args = f"-c{workspace} sync {p4_path}/...@{project_cl}"

        program_name = "sync_project"
        if self.status == DeviceStatus.SYNCING: # engine is already syncing
            LOGGER.info(f"{self.name}: Project sync will start once engine is done syncing")
            self.scheduled_sync_operations[program_name] = [sync_tool, sync_args]
        else:
            LOGGER.info(f"{self.name}: Sending project sync command: {sync_tool} {sync_args}")
            mid, msg = message_protocol.create_start_process_message(prog_path=sync_tool, prog_args=sync_args, prog_name=program_name, caller=self.name)
            self._remote_programs_start_queue[mid] = program_name
            self.unreal_client.send_message(msg)

    def build(self):

        if self.status == DeviceStatus.SYNCING:
            self.start_build_after_sync = True
            LOGGER.info(f"{self.name}: Build scheduled to start after successful sync operation")
            return

        engine_path = CONFIG.ENGINE_DIR.get_value(self.name)
        build_tool = os.path.join(engine_path, "Binaries", "DotNET", "UnrealBuildTool")
        build_args = f'Win64 Development -project="{CONFIG.UPROJECT_PATH.get_value(self.name)}" -TargetType=Editor -Progress -NoHotReloadFromIDE' 
        program_name = "build"

        mid, msg = message_protocol.create_start_process_message(prog_path=build_tool, prog_args=build_args, prog_name=program_name, caller=self.name)
        self._remote_programs_start_queue[mid] = program_name

        LOGGER.info(f"{self.name}: Sending build command: {build_tool} {build_args}")

        self.unreal_client.send_message(msg)
        self.status = DeviceStatus.BUILDING

    def close(self, force=False):

        # This call only refers to "unreal" programs.

        num_found = 0

        for prog_id, prog_name in self._running_remote_programs.items(): 
            if prog_name == 'unreal':
                _, msg = message_protocol.create_kill_process_message(prog_id)
                self.unreal_client.send_message(msg)
                num_found += 1

        if not num_found:
            self.status = DeviceStatus.CLOSED

    def generate_unreal_exe_path(self):
        return CONFIG.engine_path(CONFIG.ENGINE_DIR.get_value(self.name), DeviceUnreal.csettings["ue4_exe"].get_value())

    def generate_unreal_command_line_args(self, map_name):

        command_line_args = f'{DeviceUnreal.csettings["command_line_arguments"].get_value(self.name)}'
        if CONFIG.MUSERVER_AUTO_JOIN:
            command_line_args += f' -CONCERTRETRYAUTOCONNECTONERROR -CONCERTAUTOCONNECT -CONCERTSERVER={CONFIG.MUSERVER_SERVER_NAME} -CONCERTSESSION={SETTINGS.MUSERVER_SESSION_NAME} -CONCERTDISPLAYNAME={self.name}'
        
        exec_cmds = f'{DeviceUnreal.csettings["exec_cmds"].get_value(self.name)}'.strip()
        command_line_args += f' ExecCmds={exec_cmds} '

        selected_roles = self.setting_roles.get_value()
        unsupported_roles = [role for role in selected_roles if role not in self.setting_roles.possible_values]
        supported_roles = [role for role in selected_roles if role not in unsupported_roles]

        if supported_roles:
            command_line_args += ' -VPRole=' + '|'.join(supported_roles)
        if unsupported_roles:
            LOGGER.error(f"{self.name}: Omitted unsupported roles: {'|'.join(unsupported_roles)}")

        session_id = DeviceUnreal.csettings["stage_session_id"].get_value()
        if session_id > 0:
            command_line_args += f" -StageSessionId={session_id}"
        command_line_args += f" -StageFriendlyName={self.name}"

        args = f'"{CONFIG.UPROJECT_PATH.get_value(self.name)}" {map_name} {command_line_args}'
        return args

    def generate_unreal_command_line(self, map_name):
        return self.generate_unreal_exe_path(), self.generate_unreal_command_line_args(map_name)

    def launch(self, map_name, program_name="unreal"):

        if map_name == DEFAULT_MAP_TEXT:
            map_name = ''

        engine_path, args = self.generate_unreal_command_line(map_name)
        LOGGER.info(f"Launching UE4: {engine_path} {args}")
        mid, msg = message_protocol.create_start_process_message(prog_path=engine_path, prog_args=args, prog_name=program_name, caller=self.name)
        self._remote_programs_start_queue[mid] = program_name

        self.unreal_client.send_message(msg)

    def _try_connect_osc(self):
        if self.status == DeviceStatus.OPEN:
            self.send_osc_message(osc.OSC_ADD_SEND_TARGET, [SETTINGS.IP_ADDRESS, CONFIG.OSC_SERVER_PORT.get_value()])
        else:
            self.osc_connection_timer.stop()

    def on_listener_disconnect(self, unexpected=False, exception=None):
        if unexpected:
            self.device_qt_handler.signal_device_client_disconnected.emit(self)

    def programs_ids_with_name(self, program_name):
        return [prog_id for prog_id, prog_name in self._running_remote_programs.items() if prog_name == program_name]

    def remove_program_ids(self, prog_ids):
        for prog_id in prog_ids:
            self._running_remote_programs.pop(prog_id)

    def do_program_running_update(self, program_name, program_id):

        if program_name == "unreal":
            self.status = DeviceStatus.OPEN
            self.unreal_started_signal.emit()

        self._running_remote_programs[program_id] = program_name

    def on_program_started(self, program_id, message_id):

        program_name = self._remote_programs_start_queue.pop(message_id)
        LOGGER.info(f"{self.name}: {program_name} with id {program_id} was successfully started")
        self.do_program_running_update(program_name=program_name, program_id=program_id)

    def on_unreal_started(self):
        if self.is_recording_device:
            sleep_time_in_ms = 1000
            self.osc_connection_timer.start(sleep_time_in_ms)

    def on_program_start_failed(self, error, message_id):

        program_name = self._remote_programs_start_queue.pop(message_id)

        LOGGER.error(f"Could not start {program_name}: {error}")
        
        if program_name in ["sync", "build"]:
            self.status = DeviceStatus.CLOSED
            self.project_changelist = self.project_changelist # force to show existing project_changelist to hide building/syncing
        elif program_name == 'unreal':
            self.status = DeviceStatus.CLOSED

    def on_program_ended(self, program_id, returncode, output):

        LOGGER.info(f"{self.name}: Program with id {program_id} exited with returncode {returncode}")

        try:
            program_name = self._running_remote_programs.pop(program_id)
        except KeyError:
            LOGGER.error(f"{self.name}: on_program_ended with unknown id {program_id}")
            return

        # check if there are remaining programs named the same but with different ids, which is not normal.
        for prog_id in self.programs_ids_with_name(program_name):
            LOGGER.warning(f'{self.name}: But ({prog_id}) with the same name "{program_name}" is still in the list, which is unexpected')

        if program_name == "unreal":
            self.status = DeviceStatus.CLOSED

        elif "sync" in program_name:
            if returncode != 0:
                LOGGER.error(f"{self.name}: Project was not synced successfully!")
                for line in output.splitlines():
                    LOGGER.error(f"{self.name}: {line}")
                self.device_qt_handler.signal_device_sync_failed.emit(self)
            else:
                if "sync_engine" == program_name:
                    LOGGER.info(f"{self.name}: Engine was synced successfully")
                    if len(self.scheduled_sync_operations) > 0:
                        op = self.scheduled_sync_operations.pop("sync_project")
                        sync_tool = op[0]
                        sync_args = op[1]
                        LOGGER.info(f"{self.name}: Sending sync command: {sync_tool} {sync_args}")
                        mid, msg = message_protocol.create_start_process_message(prog_path=sync_tool, prog_args=sync_args, prog_name="sync_project", caller=self.name)
                        self._remote_programs_start_queue[mid] = "sync_project"
                        self.unreal_client.send_message(msg)
                    else:
                        self.engine_changelist = self.inflight_engine_cl
                        self.inflight_engine_cl = None
                        self.project_changelist = self.project_changelist

                elif "sync_project" == program_name:
                    LOGGER.info(f"{self.name}: Project was synced successfully")
                    self.project_changelist = self.inflight_project_cl
                    self.inflight_project_cl = None
                    if CONFIG.BUILD_ENGINE.get_value():
                        self.engine_changelist = self.inflight_engine_cl if self.inflight_engine_cl != None else self.engine_changelist
                        self.inflight_engine_cl = None

            if self.inflight_project_cl == None and self.inflight_engine_cl == None:
                # everything is done syncing
                self.status = DeviceStatus.CLOSED
                if self.start_build_after_sync:
                    self.start_build_after_sync = False
                    self.build()

        elif program_name == "build":
            if returncode == 0:
                LOGGER.info(f"{self.name}: Project was built successfully!")
            else:
                LOGGER.error(f"{self.name}: Project was not built successfully!")
                for line in output.splitlines():
                    # error lines from UBT usually look like this: $SourceFile: error C4430: missing type specifier - int assumed...
                    if ": error " in line:
                        LOGGER.error(f"{self.name}: {line}")

            self.status = DeviceStatus.CLOSED
            self.project_changelist = self.project_changelist # forces an update to the changelist field (to hide the Building state)
            if CONFIG.BUILD_ENGINE.get_value():
                self.engine_changelist = self.engine_changelist

        elif "cstat" in program_name:

            changelists = [line.strip() for line in output.split()]

            try:
                current_changelist = str(int(changelists[-1]))

                # when not connected to p4, you get a message similar to:
                #  "Perforce client error: Connect to server failed; check $P4PORT. TCP connect to perforce:1666 failed. No such host is known."
                if 'Perforce client error:' in output:
                    raise ValueError

            except (ValueError,IndexError):
                LOGGER.error(f"{self.name}: Could not retrieve changelists for project. Are the Source Control Settings correctly configured?")
                return

            if program_name.endswith("project"):
                project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
                LOGGER.info(f"{self.name}: Project {project_name} is on revision {current_changelist}")
                self.project_changelist = current_changelist
            elif program_name.endswith("engine"):
                project_name = os.path.basename(os.path.dirname(CONFIG.UPROJECT_PATH.get_value(self.name)))
                LOGGER.info(f"{self.name}: Engine used for project {project_name} is on revision {current_changelist}")
                self.engine_changelist = current_changelist

    def on_program_kill_failed(self, program_id, error):

        # remove from list of program_ids (if it exists)
        program_name = self._running_remote_programs.pop(program_id, "unknown")

        LOGGER.error(f"Unable to close program with id {str(program_id)} name {program_name}")
        LOGGER.error(f"Error: {error}")
        self.status = DeviceStatus.CLOSED

    def on_file_received(self, source_path, content):
        if source_path.endswith(DeviceUnreal.csettings["roles_filename"].get_value(self.name)):
            decoded_content = base64.b64decode(content).decode()
            tags = parse_unreal_tag_file(decoded_content.splitlines())
            self.setting_roles.possible_values = tags
            LOGGER.info(f"{self.name}: All possible roles: {tags}")
            unsupported_roles = [role for role in self.setting_roles.get_value() if role not in tags]
            if len(unsupported_roles) > 0:
                LOGGER.error(f"{self.name}: Found unsupported roles: {unsupported_roles}")
                LOGGER.error(f"{self.name}: Please change the roles for this device in the settings or in the unreal project settings!")

    def on_file_receive_failed(self, source_path, error):
        roles = self.setting_roles.get_value()
        if len(roles) > 0:
            LOGGER.error(f"{self.name}: Error receiving role file from listener and device claims to have these roles: {' | '.join(roles)}")
            LOGGER.error(f"Error: {error}")

    def on_listener_kill(self, message):
        ''' Handles errors in kill messages
        '''

        error = message.get('error', None)

        if error:
            LOGGER.error(f'Command "{message["command"]}" error "{error}"')

    def on_listener_state(self, message):
        ''' Message expected to be received upon connection with the listener.
        It contains the state of the listener. Particularly useful when Switchboard reconnects.
        '''
        
        self._running_remote_programs = {}

        for process in message['runningProcesses']:
            program_name = process['name']
            program_id = process['uuid']
            program_caller = process['caller']

            if program_caller == self.name:
                self.do_program_running_update(program_name=program_name, program_id=program_id)        

    def transport_paths(self, device_recording):
        """
        Do not transport UE4 paths as they will be checked into source control
        """
        return []


def parse_unreal_tag_file(file_content):
    tags = []
    for line in file_content:
        if line.startswith("GameplayTagList"):
            tag = line.split("Tag=")[1]
            tag = tag.split(',', 1)[0]
            tag = tag.strip('"')
            tags.append(tag)
    return tags


class DeviceWidgetUnreal(DeviceWidget):
    def __init__(self, name, device_hash, ip_address, icons, parent=None):
        super().__init__(name, device_hash, ip_address, icons, parent=parent)

        CONFIG.P4_ENABLED.signal_setting_changed.connect(lambda _, enabled: self.sync_button.setVisible(enabled))

    def _add_control_buttons(self):
        super()._add_control_buttons()

        changelist_layout = QtWidgets.QVBoxLayout()
        self.engine_changelist_label = QtWidgets.QLabel()
        self.engine_changelist_label.setFont(QtGui.QFont("Roboto", 10))
        self.engine_changelist_label.setToolTip("Current Engine Changelist")
        changelist_layout.addWidget(self.engine_changelist_label)

        self.project_changelist_label = QtWidgets.QLabel()
        self.project_changelist_label.setFont(QtGui.QFont("Roboto", 10))
        self.project_changelist_label.setToolTip("Current Project Changelist")
        changelist_layout.addWidget(self.project_changelist_label)

        spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)

        self.layout.addLayout(changelist_layout)

        self.sync_button = self.add_control_button(':/icons/images/icon_sync.png',
                                                    icon_disabled=':/icons/images/icon_sync_disabled.png',
                                                    icon_hover=':/icons/images/icon_sync_hover.png',
                                                    checkable=False, tool_tip='Sync Changelist')

        self.build_button = self.add_control_button(':/icons/images/icon_build.png',
                                                    icon_disabled=':/icons/images/icon_build_disabled.png',
                                                    icon_hover=':/icons/images/icon_build_hover.png',
                                                    icon_size=QtCore.QSize(21, 21),
                                                    checkable=False, tool_tip='Build Changelist')

        self.layout.addItem(spacer)

        self.open_button = self.add_control_button(':/icons/images/icon_open.png',
                                                    icon_hover=':/icons/images/icon_open_hover.png',
                                                    icon_disabled=':/icons/images/icon_open_disabled.png',
                                                    icon_on=':/icons/images/icon_close.png',
                                                    icon_hover_on=':/icons/images/icon_close_hover.png',
                                                    icon_disabled_on=':/icons/images/icon_close_disabled.png',
                                                    tool_tip='Start Unreal')

        self.connect_button = self.add_control_button(':/icons/images/icon_connect.png',
                                                        icon_hover=':/icons/images/icon_connect_hover.png',
                                                        icon_disabled=':/icons/images/icon_connect_disabled.png',
                                                        icon_on=':/icons/images/icon_connected.png',
                                                        icon_hover_on=':/icons/images/icon_connected_hover.png',
                                                        icon_disabled_on=':/icons/images/icon_connected_disabled.png',
                                                        tool_tip='Connect to listener')

        self.sync_button.clicked.connect(self.sync_button_clicked)
        self.build_button.clicked.connect(self.build_button_clicked)
        self.connect_button.clicked.connect(self.connect_button_clicked)
        self.open_button.clicked.connect(self.open_button_clicked)

        # Disable UI when not connected
        self.open_button.setDisabled(True)
        self.sync_button.setDisabled(True)
        self.build_button.setDisabled(True)

        self.project_changelist_label.hide()
        self.engine_changelist_label.hide()
        self.sync_button.hide()

    def can_sync(self):
        return self.sync_button.isEnabled()

    def can_build(self):
        return self.build_button.isEnabled()

    def _open(self):
        # Make sure the button is in the correct state
        self.open_button.setChecked(True)
        # Emit Signal to Switchboard
        self.signal_device_widget_open.emit(self)

    def _close(self):
        # Make sure the button is in the correct state
        self.open_button.setChecked(False)
        # Emit Signal to Switchboard
        self.signal_device_widget_close.emit(self)

    def _connect(self):
        # Make sure the button is in the correct state
        self.connect_button.setChecked(True)
        self.connect_button.setToolTip("Disconnect from listener")

        self.open_button.setDisabled(False)
        self.sync_button.setDisabled(False)
        self.build_button.setDisabled(False)

        # Emit Signal to Switchboard
        self.signal_device_widget_connect.emit(self)

    def _disconnect(self):
        # Make sure the button is in the correct state
        self.connect_button.setChecked(False)
        self.connect_button.setToolTip("Connect to listener")

        # Don't show the changelist
        self.project_changelist_label.hide()
        self.engine_changelist_label.hide()
        self.sync_button.hide()

        # Disable the buttons
        self.open_button.setDisabled(True)
        self.sync_button.setDisabled(True)
        self.build_button.setDisabled(True)

        # Emit Signal to Switchboard
        self.signal_device_widget_disconnect.emit(self)

    def update_status(self, status, previous_status):
        super().update_status(status, previous_status)

        if status == DeviceStatus.CLOSED:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(False)
            self.sync_button.setDisabled(False)
            self.build_button.setDisabled(False)
        elif status == DeviceStatus.SYNCING:
            self.open_button.setDisabled(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
            self.engine_changelist_label.hide()
            self.project_changelist_label.setText('Syncing')
        elif status == DeviceStatus.BUILDING:
            self.open_button.setDisabled(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
            self.engine_changelist_label.hide()
            self.project_changelist_label.setText('Building')
        elif status == DeviceStatus.OPEN:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)
        elif status == DeviceStatus.READY:
            self.open_button.setDisabled(False)
            self.open_button.setChecked(True)
            self.sync_button.setDisabled(True)
            self.build_button.setDisabled(True)

        if self.open_button.isChecked():
            self.open_button.setToolTip("Stop Unreal")
        else:
            self.open_button.setToolTip("Start Unreal")

    def update_project_changelist(self, value):
        self.project_changelist_label.setText(f"P: {value}")

        self.project_changelist_label.show()
        self.sync_button.show()
        self.build_button.show()

    def update_engine_changelist(self, value):
        self.engine_changelist_label.setText(f"E: {value}")
        self.engine_changelist_label.show()

        self.sync_button.show()
        self.build_button.show()

    def project_changelist_display_warning(self, b):
        if b:
            self.project_changelist_label.setProperty("error", True)
        else:
            self.project_changelist_label.setProperty("error", False)
        self.project_changelist_label.setStyle(self.project_changelist_label.style())

    def engine_changelist_display_warning(self, b):
        if b:
            self.engine_changelist_label.setProperty("error", True)
        else:
            self.engine_changelist_label.setProperty("error", False)
        self.engine_changelist_label.setStyle(self.engine_changelist_label.style())

    def sync_button_clicked(self):
        self.signal_device_widget_sync.emit(self)

    def build_button_clicked(self):
        self.signal_device_widget_build.emit(self)

    def open_button_clicked(self):
        if self.open_button.isChecked():
            self._open()
        else:
            self._close()

    def connect_button_clicked(self):
        if self.connect_button.isChecked():
            self._connect()
        else:
            self._disconnect()

