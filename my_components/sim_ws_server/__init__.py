# HuiFlow 洗車機模擬板 — sim_ws_server ESPHome component 註冊
# Phase 1：port 8090 WebSocket server（RFC 6455），接受 1 個 client（主板）
# Phase 2：擴充 EVENT parser + 應用層 CMD 派送 + assertion 觸發
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT

CODEOWNERS = ["@huiflow"]
MULTI_CONF = False

sim_ws_server_ns = cg.esphome_ns.namespace("sim_ws_server")
SimWsServerComponent = sim_ws_server_ns.class_("SimWsServerComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SimWsServerComponent),
        cv.Optional(CONF_PORT, default=8090): cv.int_range(min=1, max=65535),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_port(config[CONF_PORT]))
