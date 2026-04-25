# HuiFlow 洗車機模擬板 — sim_assertion ESPHome component 註冊
# Phase 3：assertion 引擎，9 條規則的時序窗比對 + 結果累積
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@huiflow"]
MULTI_CONF = False

sim_assertion_ns = cg.esphome_ns.namespace("sim_assertion")
SimAssertionComponent = sim_assertion_ns.class_("SimAssertionComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SimAssertionComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
