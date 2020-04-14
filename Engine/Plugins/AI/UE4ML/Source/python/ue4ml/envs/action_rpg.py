# Copyright Epic Games, Inc. All Rights Reserved.

from ..core import UnrealEnv, AgentConfig
from ..utils import dict_from_json


class ActionRPG(UnrealEnv):

    def __init__(self, ue4params=None, **kwargs):
        self._project_name = 'ActionRPG'
        self.health_sensor_index = None
        self._last_obs = None

        if ue4params is not None:
            ue4params.set_default_map_name('ActionRPG_P')
            
        super().__init__(ue4params=ue4params, **kwargs)

        response = self.conn.desc_observation_space(self._agent_id)
        if type(response) == str or type(response) == bytes:
            response = dict_from_json(response)

        observations_desc = dict_from_json(response)
        self.health_sensor_index = list(observations_desc.keys()).index('Attribute')  
        
    def _get_observation(self):
        self._last_obs = super()._get_observation()
        return self._last_obs
    
    def reset(self, wait_action=None, skip_time=1):
        self.skip(5)    
        ret = super().reset(wait_action, skip_time)
        #while self.game_over:
        #    ret = super().reset(wait_action, skip_time)
        #    self.skip(5)
        # this delay is required due to how ActionRPG gets reset
        #self.skip(90)
        return ret

    @staticmethod
    def default_agent_config():
        agent_config = AgentConfig()
        agent_config.add_sensor('AIPerception',
                                {'mode': 'rotator', 'count': 3, 'sort': 'distance', 'peripheral_angle': 180})
        agent_config.add_sensor('Attribute', {'attributes': 'health, mana'})
        agent_config.avatarClassName = 'bp_playercharacter_c'
        return agent_config
