# Copyright Epic Games, Inc. All Rights Reserved.

import gym
from gym.envs.registration import register
from .platformer import PlatformerGame
from .action_rpg import ActionRPG
from ..runner import UE4Params

__all__ = ['PlatformerGame', 'ActionRPG']

fast_kwargs = {
    'ue4params': UE4Params(rendering=False, sound=False, single_thread=True)
}

default_kwargs = {
    'ue4params': UE4Params(sound=False)
}

real_time_kwargs = {
    'ue4params': UE4Params(sound=False),
    'realtime': True
}

# no rendering, no sound, single thread, deterministic with fixed time step
register(
    id='UE4-ActionRPGFast-v0',
    entry_point='ue4ml.envs:ActionRPG',
    kwargs=fast_kwargs
)

register(
    id='UE4-ActionRPG-v0',
    entry_point='ue4ml.envs:ActionRPG',
    kwargs=default_kwargs
)

register(
    id='UE4-ActionRPGRealTime-v0',
    entry_point='ue4ml.envs:ActionRPG',
    kwargs=real_time_kwargs
)

register(
    id='UE4-PlatformerFast-v0',
    entry_point='ue4ml.envs:PlatformerGame',
    kwargs=fast_kwargs
)

register(
    id='UE4-Platformer-v0',
    entry_point='ue4ml.envs:PlatformerGame',
    kwargs=default_kwargs
)

register(
    id='UE4-PlatformerRealTime-v0',
    entry_point='ue4ml.envs:PlatformerGame',
    kwargs=real_time_kwargs
)
