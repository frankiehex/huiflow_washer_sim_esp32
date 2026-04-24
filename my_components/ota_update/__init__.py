import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_URL

CODEOWNERS = ["@huiflow"]
MULTI_CONF = False

ota_update_ns = cg.esphome_ns.namespace("ota_update")
OTAUpdateComponent = ota_update_ns.class_("OTAUpdateComponent", cg.Component)

CONF_MANIFEST_URL = "manifest_url"
CONF_CURRENT_VERSION = "current_version"
CONF_CHECK_ON_BOOT = "check_on_boot"
CONF_MANIFEST_TOKEN = "manifest_token"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OTAUpdateComponent),
        cv.Required(CONF_MANIFEST_URL): cv.string,
        cv.Required(CONF_CURRENT_VERSION): cv.string,
        cv.Optional(CONF_CHECK_ON_BOOT, default=True): cv.boolean,
        cv.Optional(CONF_MANIFEST_TOKEN, default=""): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_manifest_url(config[CONF_MANIFEST_URL]))
    cg.add(var.set_current_version(config[CONF_CURRENT_VERSION]))
    cg.add(var.set_check_on_boot(config[CONF_CHECK_ON_BOOT]))
    if config[CONF_MANIFEST_TOKEN]:
        cg.add(var.set_manifest_token(config[CONF_MANIFEST_TOKEN]))
