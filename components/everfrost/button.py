import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from .climate import EverFrostClimate
from .const import CONF_EVERFROST_ID, CONF_REFRESH

everfrost_ns = cg.esphome_ns.namespace("everfrost")
EverFrostRefreshButton = everfrost_ns.class_(
    "EverFrostRefreshButton",
    button.Button,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_EVERFROST_ID): cv.use_id(EverFrostClimate),
        cv.Optional(CONF_REFRESH): button.button_schema(
            EverFrostRefreshButton,
            icon="mdi:refresh",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_EVERFROST_ID])

    if CONF_REFRESH in config:
        var = cg.new_Pvariable(config[CONF_REFRESH][CONF_ID])
        await button.register_button(var, config[CONF_REFRESH])
        cg.add(var.set_parent(parent))
