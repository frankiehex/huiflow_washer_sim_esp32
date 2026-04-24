# HuiFlow 洗車機模擬板 — sim_fsm ESPHome component 註冊
# Phase 1 骨架：只暴露 set_led(color) + UART2 RX 原始 byte log
# Phase 2 再加 scenario 腳本引擎 + UART2 frame parser
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@huiflow"]
MULTI_CONF = False

sim_fsm_ns = cg.esphome_ns.namespace("sim_fsm")
SimFsmComponent = sim_fsm_ns.class_("SimFsmComponent", cg.Component)

CONF_LED_BLUE_PIN = "led_blue_pin"
CONF_LED_GREEN_PIN = "led_green_pin"
CONF_LED_RED_PIN = "led_red_pin"
CONF_UART_RX_PIN = "uart_rx_pin"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SimFsmComponent),
        cv.Required(CONF_LED_BLUE_PIN): cv.int_range(min=0, max=48),
        cv.Required(CONF_LED_GREEN_PIN): cv.int_range(min=0, max=48),
        cv.Required(CONF_LED_RED_PIN): cv.int_range(min=0, max=48),
        cv.Required(CONF_UART_RX_PIN): cv.int_range(min=0, max=48),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_led_pins(
        config[CONF_LED_BLUE_PIN],
        config[CONF_LED_GREEN_PIN],
        config[CONF_LED_RED_PIN],
    ))
    cg.add(var.set_uart_rx_pin(config[CONF_UART_RX_PIN]))
