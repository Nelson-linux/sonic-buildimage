"""
    fan_drawer_base.py

    Abstract base class for implementing a platform-specific class with which
    to interact with a fan drawer module in SONiC
"""

try:
    from sonic_platform_base.fan_drawer_base import FanDrawerBase
    from .fan import Fan
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

NUM_FAN_TRAY = 7
FANS_PER_TRAY = 2

class FanDrawer(FanDrawerBase):
    """
    Abstract base class for interfacing with a fan drawer
    """
    # Device type definition. Note, this is a constant.
    DEVICE_TYPE = "fan_drawer"
    def __init__(self, fan_drawer_index=0):
        self._fan_list = []
        self.fan_drawer_index = fan_drawer_index
        for fant_index in range(0, FANS_PER_TRAY):
            for fan_index in range(0, NUM_FAN_TRAY):
                fan = Fan(fant_index, fan_index)
                self._fan_list.append(fan)


    def set_status_led(self, color):
        """
        Sets the state of the fan drawer status LED

        Args:
            color: A string representing the color with which to set the
                   fan drawer status LED

        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        return True

    def get_status_led(self):
        """
        Gets the state of the fan drawer LED

        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        return None